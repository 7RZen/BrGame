#include "BrSafeZone.h"
#include "BrZoneBounds.h"
#include "BrPlayableGround.h"
#include "BrZoneDamageType.h"
#include "BrDeveloperSettings.h"
#include "BrCharacter.h"
#include "BrLifeComponent.h"
#include "BrGameState.h"
#include "BrLog.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"

ABrSafeZone::ABrSafeZone()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	CurrentRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CurrentRing"));
	SetRootComponent(CurrentRing);
	TargetRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TargetRing"));
	TargetRing->SetupAttachment(CurrentRing);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
	for (UStaticMeshComponent* Ring : {CurrentRing.Get(), TargetRing.Get()})
	{
		Ring->SetStaticMesh(Plane.Object);
		Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ring->SetCanEverAffectNavigation(false);
		Ring->SetCastShadow(false);
		Ring->SetAbsolute(true, true, true);
	}
}

double ABrSafeZone::ServerTime() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void ABrSafeZone::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
		Stages = Settings->ZoneStages;
		if (FBrZoneRules::RepairStages(Stages))
		{
			UE_LOG(LogBr, Warning, TEXT("Zone repaired unusable stage settings: finite nonnegative waits/DPS, positive shrink durations, final collapse and final damage >= 1 HP/s required."));
		}
		TimeScale = FMath::IsFinite(Settings->ZoneTimeScale) ? FMath::Clamp(Settings->ZoneTimeScale, 0.01f, 1000.0f) : 1;
		State.bDisableRespawns = Settings->bDisableRespawnDuringFinalStage;
		Stream.Initialize(FMath::Rand());
		FindInitialBounds();
		FBrZoneCircle Initial;
		Initial.Center = PlayableBounds.GetCenter();
		FHitResult Floor;
		if (GetWorld()->LineTraceSingleByChannel(Floor, Initial.Center + FVector(0, 0, 10000),
			Initial.Center - FVector(0, 0, 10000), ECC_WorldStatic))
			Initial.Center.Z = Floor.ImpactPoint.Z;
		Initial.Radius = PlayableBounds.GetExtent().Size2D();
		StartStage(0, Initial, ServerTime());
		GetWorldTimerManager().SetTimer(ServiceTimer, this, &ABrSafeZone::Service, 0.5f, true);
	}
	SetActorTickEnabled(GetNetMode() != NM_DedicatedServer);
	if (GetNetMode() != NM_DedicatedServer)
	{
		UMaterialInterface* Material = UBrDeveloperSettings::Get()->ZoneRingMaterial.LoadSynchronous();
		if (!Material)
		{
			UE_LOG(LogBr, Error, TEXT("Zone ring material missing; run Scripts/Content/create_zone_assets.py."));
		}
		else
		{
			for (UStaticMeshComponent* Ring : {CurrentRing.Get(), TargetRing.Get()})
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this);
				MID->SetVectorParameterValue(TEXT("Color"), Ring == CurrentRing ?
					FLinearColor(0.05f, 0.4f, 1) : FLinearColor(1, 1, 1));
				MID->SetScalarParameterValue(TEXT("Width"), Ring == CurrentRing ? 0.012f : 0.004f);
				Ring->SetMaterial(0, MID);
			}
		}
	}
	UpdateVisuals();
}

void ABrSafeZone::FindInitialBounds()
{
	PlayableBounds = ABrZoneBounds::FindPlayableBounds(GetWorld());
}

void ABrSafeZone::StartStage(int32 Index, FBrZoneCircle Initial, double StartTime)
{
	State.StageIndex = Index;
	State.Current = Initial;
	State.Target = {Initial.Center, Initial.Radius * Stages[Index].TargetRadiusFraction};
	PendingTargetRadius = State.Target.Radius;
	State.bPaused = false;
	bool bNavVerified = false;
	const bool bPlayable = SelectPlayableTarget(Initial, State.Target.Radius, State.Target, bNavVerified);
	bTargetSelectionPending = !bNavVerified;
	bNavigationHold = !bPlayable;
	TargetSearchDeadline = ServerTime() + 10;
	State.WaitEnd = StartTime + Stages[Index].WaitTime / TimeScale;
	State.ShrinkEnd = State.WaitEnd + Stages[Index].ShrinkTime / TimeScale;
	State.DamagePerSecond = Stages[Index].DamagePerSecond;
	State.bFinal = Index == Stages.Num() - 1;
	if (bNavigationHold) { State.Target = Initial; State.bPaused = true; State.PauseTime = StartTime; }
	if (!bNavVerified)
		UE_LOG(LogBr, Warning, TEXT("Zone target nav validation pending: Stage=%d PlayableGroundFallback=%d Hold=%d; retrying when navigation is ready (10s budget)."),
			Index + 1, bPlayable, bNavigationHold);
	ForceNetUpdate();
	if (!bNavigationHold) LogStage();
}

void ABrSafeZone::LogStage() const
{
	UE_LOG(LogBr, Display, TEXT("Zone stage: %d/%d Radius=%.1f Target=%.1f WaitEnd=%.2f ShrinkEnd=%.2f DPS=%.1f"),
		State.StageIndex + 1, Stages.Num(), State.Current.Radius, State.Target.Radius, State.WaitEnd, State.ShrinkEnd, State.DamagePerSecond);
}

bool ABrSafeZone::SelectPlayableTarget(const FBrZoneCircle& Current, float Radius, FBrZoneCircle& Target, bool& bNavVerified)
{
	bNavVerified = false;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const bool bNavReady = Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) &&
		!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld());
	FNavLocation Anchor;
	if (bNavReady && Nav->ProjectPointToNavigation(Current.Center, Anchor, FVector(10000, 10000, 2000)) &&
		Current.IsInside(Anchor.Location))
	{
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			FNavLocation Reachable;
			// Failure is not a validated random center. Only consume successful nav results.
			if (!Nav->GetRandomReachablePointInRadius(Anchor.Location, Current.Radius, Reachable)) continue;
			const FBrZoneCircle Candidate {Reachable.Location, Radius};
			if (!FBrZoneRules::IsPlayableTarget(Current, Candidate, PlayableBounds)) continue;
			Target = Candidate;
			bNavVerified = true;
			return true;
		}
		// A projected anchor is known navigable/reachable from itself. If necessary
		// reduce the fallback radius to fit, rather than inventing an unreachable center.
		const double Clearance = FMath::Min(Current.Radius - FVector::Dist2D(Current.Center, Anchor.Location),
			FMath::Min(FMath::Min(Anchor.Location.X - PlayableBounds.Min.X, PlayableBounds.Max.X - Anchor.Location.X),
				FMath::Min(Anchor.Location.Y - PlayableBounds.Min.Y, PlayableBounds.Max.Y - Anchor.Location.Y)));
		const FBrZoneCircle Fallback {Anchor.Location, static_cast<float>(FMath::Min<double>(Radius, Clearance))};
		if (Clearance >= 0 && FBrZoneRules::IsPlayableTarget(Current, Fallback, PlayableBounds))
		{
			Target = Fallback;
			bNavVerified = true;
			UE_LOG(LogBr, Display, TEXT("Zone target: using verified nav anchor fallback Radius=%.1f"), Target.Radius);
			return true;
		}
	}
	// No usable nav result: only use a concentric fallback after verifying walkable
	// ground and static capsule clearance. Retry nav later, never bless a random point.
	FVector Floor;
	const UCapsuleComponent* Capsule = GetDefault<ABrCharacter>()->GetCapsuleComponent();
	FBrGroundQuery GroundQuery;
	GroundQuery.Radius = Capsule->GetScaledCapsuleRadius();
	GroundQuery.HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	if (FBrPlayableGround::Find(GetWorld(), Current.Center, Floor, GroundQuery))
	{
		const FBrZoneCircle Fallback {Floor, Radius};
		if (FBrZoneRules::IsPlayableTarget(Current, Fallback, PlayableBounds)) { Target = Fallback; return true; }
	}
	return false;
}

void ABrSafeZone::RetryTargetSelection(double Now)
{
	if (!bTargetSelectionPending) return;
	// No circle expansion when a delayed nav result arrives during shrink.
	const FBrZoneCircle Current = Circle();
	FBrZoneCircle Target;
	bool bNavVerified = false;
	const bool bPlayable = SelectPlayableTarget(Current, FMath::Min(Current.Radius, PendingTargetRadius), Target, bNavVerified);
	if (bPlayable && (bNavVerified || bNavigationHold))
	{
		const bool bWasHeld = bNavigationHold;
		State.Resnapshot(Now, Now, 1);
		State.Target = Target;
		if (bNavigationHold) { State.bPaused = false; bNavigationHold = false; }
		bTargetSelectionPending = !bNavVerified;
		ForceNetUpdate();
		if (bWasHeld) LogStage();
		if (bNavVerified) UE_LOG(LogBr, Display, TEXT("Zone target nav retry verified: Stage=%d Radius=%.1f"), State.StageIndex + 1, Target.Radius);
	}
	if (Now < TargetSearchDeadline) return;
	bTargetSelectionPending = false;
	if (!bNavigationHold)
	{
		UE_LOG(LogBr, Warning, TEXT("Zone nav retry budget expired; retaining verified playable ground target."));
		return;
	}
	// A broken map must not hold the match forever. No usable target was found:
	// explicitly enter a terminal collapse, rather than claim an unreachable safe area.
	UE_LOG(LogBr, Warning, TEXT("Zone has no verified playable target after 10s; forcing terminal collapse."));
	State.Current = Current;
	State.Target = {Current.Center, 0};
	State.WaitEnd = Now;
	State.ShrinkEnd = Now + 1;
	State.DamagePerSecond = FMath::Max(1.0f, Stages.Last().DamagePerSecond);
	State.StageIndex = Stages.Num() - 1;
	State.bFinal = State.bDisableRespawns = true;
	State.bPaused = bNavigationHold = false;
	ForceNetUpdate();
}

void ABrSafeZone::Service()
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!GS || GS->bResults) { Stop(); return; }
	if (Stages.IsEmpty()) return;
	const double Now = ServerTime();
	RetryTargetSelection(Now);
	if (State.bPaused) return;
	while (FBrZoneRules::CanAdvance(State.StageIndex, Stages.Num(), State, Now))
		StartStage(State.StageIndex + 1, State.Target, State.ShrinkEnd);
	if (IsSuddenDeath() && !bSuddenDeathLogged)
	{
		bSuddenDeathLogged = true;
		UE_LOG(LogBr, Display, TEXT("Zone sudden death: respawns disabled"));
	}
	for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->GetLife()->IsAlive() || IsInside(It->GetActorLocation())) continue;
		const float Applied = UGameplayStatics::ApplyDamage(*It,
			FBrZoneRules::DamagePerTick(State.DamagePerSecond, 0.5f), nullptr, this, UBrZoneDamageType::StaticClass());
		if (Applied > 0) UE_LOG(LogBr, Verbose, TEXT("Zone damage: %s Applied=%.2f"), *It->GetName(), Applied);
	}
}

void ABrSafeZone::Stop()
{
	GetWorldTimerManager().ClearTimer(ServiceTimer);
	SetActorTickEnabled(false);
	CurrentRing->SetVisibility(false);
	TargetRing->SetVisibility(false);
}

void ABrSafeZone::NextStage()
{
	if (!HasAuthority() || Stages.IsEmpty() || State.bPaused) return;
	if (State.StageIndex + 1 < Stages.Num()) StartStage(State.StageIndex + 1, Circle(), ServerTime());
	else
	{
		State.Current = Circle();
		State.WaitEnd = ServerTime();
		State.ShrinkEnd = State.WaitEnd + Stages.Last().ShrinkTime / TimeScale;
		ForceNetUpdate();
	}
}

void ABrSafeZone::SetTimeScale(float Value)
{
	if (!HasAuthority() || !FMath::IsFinite(Value) || Value < 0.01f || Value > 1000) return;
	const double Now = ServerTime();
	State.Resnapshot(Now, State.EffectiveTime(Now), TimeScale / Value);
	TimeScale = Value;
	ForceNetUpdate();
}

void ABrSafeZone::TogglePause()
{
	if (!HasAuthority()) return;
	if (bNavigationHold) { UE_LOG(LogBr, Warning, TEXT("Zone pause is held until target validation completes.")); return; }
	const double Now = ServerTime();
	if (State.bPaused)
	{
		State.Resnapshot(State.PauseTime, Now, 1);
		State.bPaused = false;
	}
	else { State.Resnapshot(Now, Now, 1); State.PauseTime = Now; State.bPaused = true; }
	ForceNetUpdate();
}

void ABrSafeZone::UpdateVisuals()
{
	auto Update = [](UStaticMeshComponent* Ring, const FBrZoneCircle& C, float Z)
	{
		Ring->SetVisibility(C.Radius > 0);
		Ring->SetWorldLocation(C.Center + FVector(0, 0, Z));
		Ring->SetWorldScale3D(FVector(FMath::Max(0.001f, C.Radius / 48), FMath::Max(0.001f, C.Radius / 48), 1));
	};
	Update(CurrentRing, Circle(), 8);
	Update(TargetRing, State.Target, 10);
}
void ABrSafeZone::Tick(float DeltaSeconds) { Super::Tick(DeltaSeconds); UpdateVisuals(); }
void ABrSafeZone::OnRep_State()
{
	UpdateVisuals();
	UE_LOG(LogBr, Display, TEXT("Zone replicated: Stage=%d Final=%d Radius=%.1f"), State.StageIndex + 1, State.bFinal, Circle().Radius);
}
void ABrSafeZone::EndPlay(const EEndPlayReason::Type Reason) { Stop(); Super::EndPlay(Reason); }
void ABrSafeZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrSafeZone, State);
}
