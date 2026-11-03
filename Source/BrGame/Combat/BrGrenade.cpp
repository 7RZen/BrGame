#include "BrGrenade.h"
#include "BrAudioSubsystem.h"
#include "BrVisibilitySubsystem.h"
#include "BrCharacter.h"
#include "BrCombatDefinitions.h"
#include "BrCombatVisual.h"
#include "BrDeveloperSettings.h"
#include "BrLog.h"
#include "BrGameMode.h"
#include "BrPlayerState.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ABrGrenade::ABrGrenade()
{
	bReplicates = true;
	SetReplicateMovement(true);
	InitialLifeSpan = 10;
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->InitSphereRadius(8);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCollisionResponseToChannel(UBrVisibilitySubsystem::VisionChannel, ECR_Ignore);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	Mesh->SetRelativeScale3D(FVector(0.16f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Mesh->SetStaticMesh(Sphere.Object);
	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->InitialSpeed = 0;
	Movement->MaxSpeed = 0;
	Movement->ProjectileGravityScale = 1;
	Movement->bShouldBounce = false;
	Movement->bForceSubStepping = true;
}

bool ABrGrenade::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	if (const UBrVisibilitySubsystem* Visibility = GetWorld()->GetSubsystem<UBrVisibilitySubsystem>())
		if (UBrVisibilitySubsystem::Active(GetWorld())) return Visibility->Relevant(this, RealViewer);
	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}

void ABrGrenade::BeginPlay()
{
	Super::BeginPlay();
	Collision->IgnoreActorWhenMoving(GetOwner(), true);
	if (HasAuthority())
	{
		Movement->OnProjectileStop.AddDynamic(this, &ABrGrenade::Impact);
	}
	else
	{
		Movement->Deactivate();
		Movement->SetComponentTickEnabled(false);
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void ABrGrenade::EndPlay(const EEndPlayReason::Type Reason)
{
	Movement->OnProjectileStop.RemoveDynamic(this, &ABrGrenade::Impact);
	Super::EndPlay(Reason);
}

bool ABrGrenade::Launch(UBrThrowableDefinition* InDefinition, const FVector& Velocity)
{
	if (!HasAuthority() || !InDefinition) return false;
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	DamageSource = Mode ? Mode->CaptureDamageSource(GetInstigatorController()) : FBrDamageSource();
	if (!DamageSource.IsValid())
	{
		UE_LOG(LogBr, Error, TEXT("Grenade launch rejected: no admitted damage source for %s"), *GetName());
		return false;
	}
	Definition = InDefinition;
	OnRep_Definition();
	Movement->Velocity = Velocity;
	return true;
}

void ABrGrenade::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrGrenade, Definition);
	DOREPLIFETIME(ABrGrenade, bExploded);
	DOREPLIFETIME(ABrGrenade, ExplosionCenter);
}

void ABrGrenade::OnRep_Definition()
{
	if (Definition && Definition->Mesh) Mesh->SetStaticMesh(Definition->Mesh);
}

void ABrGrenade::OnRep_Exploded()
{
	if (!bExploded) return;
	Mesh->SetVisibility(false);
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Movement->Deactivate();
	if (GetNetMode() != NM_DedicatedServer && Definition)
	{
		if (const ABrCharacter* Thrower = Cast<ABrCharacter>(GetInstigator()))
			if (Thrower->IsLocallyControlled())
				if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
					Audio->Play(EBrSoundKind::Explosion, Definition->ExplosionSound, ExplosionCenter);
		if (ABrCombatVisual* Visual = GetWorld()->SpawnActor<ABrCombatVisual>(ExplosionCenter, FRotator::ZeroRotator))
		{
			Visual->InitExplosion(Definition->BlastRadius);
		}
	}
}

void ABrGrenade::Impact(const FHitResult& Hit)
{
	if (!HasAuthority() || bExploded) return;
	if (!Definition)
	{
		UE_LOG(LogBr, Error, TEXT("Grenade impact without definition: %s"), *GetName());
		Destroy();
		return;
	}
	bExploded = true;
	ExplosionCenter = Hit.ImpactPoint + Hit.ImpactNormal * 3.0f;
	// Sole transient audio path, independent of grenade channels and OnRep/late joining.
	if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
		Audio->Emit(this, EBrSoundKind::Explosion, Definition->ExplosionSound, ExplosionCenter,
			DamageSource.PlayerState.IsValid() ? Cast<AController>(DamageSource.PlayerState->GetOwner()) : nullptr);
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Pawns(ECC_Pawn);
	GetWorld()->OverlapMultiByObjectType(Overlaps, ExplosionCenter, FQuat::Identity, Pawns,
		FCollisionShape::MakeSphere(FMath::Max(1.0f, Definition->BlastRadius)));
	TSet<ABrCharacter*> Targets;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (ABrCharacter* Target = Cast<ABrCharacter>(Overlap.GetActor())) Targets.Add(Target);
	}
	FCollisionObjectQueryParams Walls;
	Walls.AddObjectTypesToQuery(ECC_WorldStatic);
	Walls.AddObjectTypesToQuery(ECC_WorldDynamic);
	for (ABrCharacter* Target : Targets)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(BrExplosionLOS), false, this);
		Query.AddIgnoredActor(Target);
		Query.AddIgnoredActor(GetOwner());
		FHitResult Block;
		if (GetWorld()->LineTraceSingleByObjectType(Block, ExplosionCenter, Target->GetActorLocation(), Walls, Query)) continue;
		float Damage = Definition->Damage;
		if (Definition->bDamageFalloff)
		{
			Damage *= FMath::Clamp(1.0f - FVector::Distance(ExplosionCenter, Target->GetActorLocation()) /
				FMath::Max(1.0f, Definition->BlastRadius), 0.0f, 1.0f);
		}
		const FBrDamageEvent Event(DamageSource, UBrExplosionDamageType::StaticClass(), ExplosionCenter);
		AController* Controller = DamageSource.PlayerState.IsValid() ? Cast<AController>(DamageSource.PlayerState->GetOwner()) : nullptr;
		Target->TakeDamage(Damage, Event, Controller, this);
	}
	UE_LOG(LogBr, Display, TEXT("Grenade explosion: %s Candidates=%d"), *GetName(), Targets.Num());
	OnRep_Exploded();
	ForceNetUpdate();
	SetLifeSpan(0.5f);
}
