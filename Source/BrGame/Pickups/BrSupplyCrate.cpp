#include "BrSupplyCrate.h"
#include "BrPickupManager.h"
#include "BrDeveloperSettings.h"
#include "BrAudioSubsystem.h"
#include "BrLog.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ABrSupplyCrate::ABrSupplyCrate()
{
	bReplicates = true;
	bAlwaysRelevant = true; // Not a fog-managed character/grenade; visible to every connection.
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Crate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Crate"));
	Crate->SetupAttachment(RootComponent);
	Crate->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Crate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Crate->SetCanEverAffectNavigation(false);
	Crate->SetRenderCustomDepth(true);
	Crate->SetCustomDepthStencilValue(1); // Existing fog material's always-visible stencil.
	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LandingMarker"));
	Marker->SetupAttachment(RootComponent);
	Marker->SetCollisionProfileName(TEXT("NoCollision"));
	Marker->SetCanEverAffectNavigation(false);
	Marker->SetRenderCustomDepth(true);
	Marker->SetCustomDepthStencilValue(1);
	Marker->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/BrGame/Pickups/M_Pickup.M_Pickup"));
	if (Cube.Succeeded()) Crate->SetStaticMesh(Cube.Object);
	if (Cylinder.Succeeded()) Marker->SetStaticMesh(Cylinder.Object);
	if (Material.Succeeded())
	{
		Crate->SetMaterial(0, Material.Object);
		Marker->SetMaterial(0, Material.Object);
	}
	Crate->SetRelativeScale3D(FVector(1.2));
	Marker->SetAbsolute(true, true, true);
	Marker->SetWorldScale3D(FVector(2, 2, 0.02));
}

void ABrSupplyCrate::Initialize(ABrPickupManager* Manager)
{
	DropManager = Manager;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	StartTime = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
	Height = FMath::Max(100.f, S->CrateDropHeight);
	Duration = FMath::Max(0.1f, S->CrateFallDuration);
}

void ABrSupplyCrate::BeginPlay()
{
	Super::BeginPlay();
	// Actor's spawn transform is the landing center, never moved/replicated per frame.
	Marker->SetWorldLocation(GetActorLocation() - FVector(0, 0, 58));
	if (!Crate->GetStaticMesh() || !Marker->GetStaticMesh())
		UE_LOG(LogBr, Error, TEXT("Supply crate placeholder meshes are missing"));
	if (UMaterialInstanceDynamic* MID = Crate->CreateDynamicMaterialInstance(0))
		MID->SetVectorParameterValue(TEXT("PickupColor"), FLinearColor(0.35f, 0.18f, 0.04f));
	if (UMaterialInstanceDynamic* MID = Marker->CreateDynamicMaterialInstance(0))
		MID->SetVectorParameterValue(TEXT("PickupColor"), FLinearColor::Yellow);
	if (HasAuthority())
		GetWorld()->GetTimerManager().SetTimer(LandingTimer, this, &ThisClass::Land, Duration, false);
	SetActorTickEnabled(GetNetMode() != NM_DedicatedServer && !bLanded);
	UpdateVisual();
	UE_LOG(LogBr, Verbose, TEXT("Crate snapshot: Actor=%s Authority=%d Landing=%s Start=%.2f Duration=%.2f Landed=%d"),
		*GetName(), HasAuthority(), *GetActorLocation().ToString(), StartTime, Duration, bLanded);
}

void ABrSupplyCrate::UpdateVisual()
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : StartTime;
	const float Alpha = bLanded ? 1.f : static_cast<float>(FMath::Clamp((Now - StartTime) / Duration, 0.0, 1.0));
	// Move mesh geometry, not root transform: late join uses the original spawn position.
	Crate->SetRelativeLocation(FVector(0, 0, Height * (1 - Alpha)));
}

void ABrSupplyCrate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateVisual();
}

void ABrSupplyCrate::Land()
{
	if (!HasAuthority()) return;
	ABrPickupManager* Manager = DropManager.Get();
	if (!Manager || !Manager->LandCrate(this))
	{
		UE_LOG(LogBr, Warning, TEXT("Crate landing cancelled: ground, current zone or pawn clearance changed"));
		Destroy();
		return;
	}
	bLanded = true;
	OnRep_Landed();
	ForceNetUpdate();
	SetLifeSpan(FMath::Max(0.1f, UBrDeveloperSettings::Get()->CrateCoverLifetime));
	if (UBrDeveloperSettings::Get()->CrateLandingSound.IsNull())
	{
		UE_LOG(LogBr, Warning, TEXT("Crate landing sound is not configured"));
	}
	else if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
	{
		Audio->Emit(this, EBrSoundKind::Explosion, UBrDeveloperSettings::Get()->CrateLandingSound, GetActorLocation());
	}
}

void ABrSupplyCrate::OnRep_Landed()
{
	UpdateVisual();
	Marker->SetVisibility(!bLanded);
	Crate->SetCollisionEnabled(bLanded ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	SetActorTickEnabled(!bLanded);
	UE_LOG(LogBr, Verbose, TEXT("Crate presentation: Actor=%s Authority=%d Landed=%d"),
		*GetName(), HasAuthority(), bLanded);
}

void ABrSupplyCrate::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(LandingTimer);
	Super::EndPlay(Reason);
}

void ABrSupplyCrate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrSupplyCrate, StartTime);
	DOREPLIFETIME(ABrSupplyCrate, Height);
	DOREPLIFETIME(ABrSupplyCrate, Duration);
	DOREPLIFETIME(ABrSupplyCrate, bLanded);
}
