#include "BrPickup.h"
#include "BrAudioSubsystem.h"
#include "BrCharacter.h"
#include "BrDeveloperSettings.h"
#include "BrGameMode.h"
#include "BrPickupManager.h"
#include "BrHealthComponent.h"
#include "BrLifeComponent.h"
#include "BrLog.h"
#include "BrPlayerController.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ABrPickup::ABrPickup()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->InitSphereRadius(50);
	// A dedicated object channel keeps item triggers out of bullet/world-geometry sweeps.
	Trigger->SetCollisionObjectType(ECC_GameTraceChannel1);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(false);
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Trigger);
	Visual->SetCollisionProfileName(TEXT("NoCollision"));
	Visual->SetCastShadow(false);
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Trigger);
	Label->SetAbsolute(false, true, false);
	Label->SetRelativeRotation(FRotator(90, 0, 0));
	Label->SetRelativeLocation(FVector(0, 0, 65));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(25);
	Label->SetCollisionProfileName(TEXT("NoCollision"));
}

ABrPickup* ABrPickup::Spawn(UWorld* World, const FTransform& Transform, UBrPickupDefinition* Definition,
	const FBrPickupPayload& Payload, bool bManaged)
{
	if (!World || World->GetNetMode() == NM_Client || !Definition)
	{
		UE_LOG(LogBr, Error, TEXT("Pickup spawn refused: missing world/definition or no authority."));
		return nullptr;
	}
	ABrPickupManager* DropManager = nullptr;
	if (Payload.bDropped)
	{
		ABrGameMode* Mode = World->GetAuthGameMode<ABrGameMode>();
		DropManager = Mode ? Mode->GetPickupManager() : nullptr;
		if (bManaged || !IsValid(DropManager) || DropManager->IsActorBeingDestroyed())
		{
			UE_LOG(LogBr, Error, TEXT("Dropped weapon spawn refused: no active round pickup manager or invalid managed drop."));
			return nullptr; // Swapping must not leave an untracked persistent actor.
		}
	}
	ABrPickup* Pickup = World->SpawnActorDeferred<ABrPickup>(StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup) { UE_LOG(LogBr, Error, TEXT("Pickup actor allocation failed.")); return nullptr; }
	Pickup->State.Definition = Definition;
	Pickup->State.Payload = Payload;
	Pickup->State.bAvailable = true;
	Pickup->bManagedSlot = bManaged;
	UGameplayStatics::FinishSpawningActor(Pickup, Transform);
	if (DropManager) DropManager->RegisterRoundDrop(Pickup);
	if (Payload.bDropped && UBrDeveloperSettings::Get()->DroppedWeaponLifetime > 0)
		Pickup->SetLifeSpan(UBrDeveloperSettings::Get()->DroppedWeaponLifetime);
	return Pickup;
}

void ABrPickup::BeginPlay()
{
	Super::BeginPlay();
	OnRep_State();
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ABrPickup::Overlap);
		// Let the manager bind its slot delegate before an initial overlap can consume this actor.
		ActivationTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ABrPickup::EnableCollection);
	}
}

void ABrPickup::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(ActivationTimer);
	Trigger->OnComponentBeginOverlap.RemoveDynamic(this, &ABrPickup::Overlap);
	OnConsumed.Clear();
	Super::EndPlay(Reason);
}

void ABrPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrPickup, State);
}

void ABrPickup::EnableCollection()
{
	if (HasAuthority() && State.bAvailable)
	{
		Trigger->SetGenerateOverlapEvents(true);
		Trigger->UpdateOverlaps();
	}
}

FText ABrPickup::GetPickupLabel() const
{
	return State.Definition ? State.Definition->GetLabel(State.Payload) : FText::GetEmpty();
}

void ABrPickup::OnRep_State()
{
	SetActorHiddenInGame(!State.bAvailable);
	Trigger->SetCollisionEnabled(State.bAvailable ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	SetActorTickEnabled(!HasAuthority() && State.bAvailable); // Purely local bob/spin, no server item tick.
	if (!State.Definition)
	{
		if (State.bAvailable) UE_LOG(LogBr, Error, TEXT("Available pickup has no definition: %s"), *GetName());
		return;
	}
	Visual->SetStaticMesh(State.Definition->Mesh);
	Visual->SetRelativeScale3D(State.Definition->VisualScale);
	if (State.Definition->Material)
	{
		if (UMaterialInstanceDynamic* MID = Visual->CreateDynamicMaterialInstance(0, State.Definition->Material))
			MID->SetVectorParameterValue(TEXT("PickupColor"), State.Definition->Color);
	}
	else UE_LOG(LogBr, Warning, TEXT("Pickup cosmetic material missing: %s"), *GetNameSafe(State.Definition));
	if (!State.Definition->Mesh) UE_LOG(LogBr, Warning, TEXT("Pickup cosmetic mesh missing: %s"), *GetNameSafe(State.Definition));
	Label->SetText(GetPickupLabel());
	Label->SetTextRenderColor(State.Definition->Color.ToFColor(true));
}

void ABrPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Visual->AddLocalRotation(FRotator(0, 30 * DeltaSeconds, 0));
	Visual->SetRelativeLocation(FVector(0, 0, FMath::Sin(GetWorld()->GetTimeSeconds() * 2) * 6));
}

bool ABrPickup::CanCollect(const ABrCharacter* Character) const
{
	return State.bAvailable && !bCollecting && IsValid(Character) && Character->GetController() &&
		Character->GetLife()->IsAlive() && Character->GetHealth()->Health > 0 && State.Definition &&
		State.Definition->CanApply(Character, State.Payload);
}

bool ABrPickup::TryCollect(ABrCharacter* Character)
{
	if (!HasAuthority() || !CanCollect(Character)) return false;
	bCollecting = true;
	const bool bApplied = State.Definition->Apply(Character, State.Payload);
	bCollecting = false;
	if (!bApplied) return false;
	State.bAvailable = false;
	Trigger->SetGenerateOverlapEvents(false);
	OnRep_State();
	ForceNetUpdate();
	if (ABrPlayerController* PC = Cast<ABrPlayerController>(Character->GetController()))
	{
		PC->ClientPickupFeedback(State.Payload.Feedback);
		if (UBrAudioSubsystem::ListenerPawn(PC))
			PC->ClientSound(EBrSoundKind::Pickup, State.Definition->PickupSound.IsNull() ?
				UBrDeveloperSettings::Get()->DefaultPickupSound : State.Definition->PickupSound, Character->GetActorLocation());
	}
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>()) Mode->RecordPlayer(Character->GetController());
	UE_LOG(LogBr, Display, TEXT("Pickup collected: %s Player=%s %s"),
		*GetPickupLabel().ToString(), *Character->GetName(), *State.Payload.Feedback);
	if (bManagedSlot) OnConsumed.Broadcast(this);
	else Destroy(); // Drops and cheat-spawned pickups never respawn.
	return true;
}

bool ABrPickup::TryAutoCollect(ABrCharacter* Character)
{
	return HasAuthority() && State.Definition && !State.Definition->bRequiresInteract && TryCollect(Character);
}

void ABrPickup::Overlap(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent,
	int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (ABrCharacter* Character = Cast<ABrCharacter>(Other)) TryAutoCollect(Character);
}

void ABrPickup::ResetPickup(UBrPickupDefinition* Definition)
{
	if (!HasAuthority() || !Definition) return;
	Trigger->SetGenerateOverlapEvents(false);
	State.Definition = Definition;
	State.Payload = FBrPickupPayload();
	State.bAvailable = true;
	OnRep_State();
	ForceNetUpdate();
	EnableCollection();
}

ABrPickup* ABrPickup::FindInteractPickup(const ABrCharacter* Character)
{
	if (!IsValid(Character) || !Character->GetLife()->IsAlive()) return nullptr;
	const float Radius = FMath::Max(1.0f, UBrDeveloperSettings::Get()->InteractRadius);
	FCollisionObjectQueryParams Objects(ECC_GameTraceChannel1);
	TArray<FOverlapResult> Overlaps;
	Character->GetWorld()->OverlapMultiByObjectType(Overlaps, Character->GetActorLocation(), FQuat::Identity,
		Objects, FCollisionShape::MakeSphere(Radius));
	ABrPickup* Best = nullptr;
	double BestDistance = FMath::Square(Radius);
	for (const FOverlapResult& Result : Overlaps)
	{
		ABrPickup* Pickup = Cast<ABrPickup>(Result.GetActor());
		if (!Pickup || !Pickup->State.Definition || !Pickup->State.Definition->bRequiresInteract || !Pickup->CanCollect(Character)) continue;
		const double Distance = FVector::DistSquared(Character->GetActorLocation(), Pickup->GetActorLocation());
		if (Distance <= BestDistance) { Best = Pickup; BestDistance = Distance; }
	}
	return Best;
}
