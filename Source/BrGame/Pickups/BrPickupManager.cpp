#include "BrPickupManager.h"
#include "BrPickup.h"
#include "BrPickupDefinition.h"
#include "BrCrateRules.h"
#include "BrSupplyCrate.h"
#include "BrDeveloperSettings.h"
#include "BrGameState.h"
#include "BrSafeZone.h"
#include "BrZoneBounds.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

ABrPickupManager::ABrPickupManager()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
}

void ABrPickupManager::Initialize(int32 PlayersAtStart)
{
	if (!HasAuthority() || bInitialized) return;
	bInitialized = true;
	Stream.Initialize(FMath::Rand());
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	if (S->CrateDefinitions.Num() != FBrCrateRules::TypeCount ||
		S->CrateHealthWeights.Num() != 3 ||
		FBrPickupRules::WeightedIndex(S->CrateHealthWeights, Stream) == INDEX_NONE)
	{
		UE_LOG(LogBr, Error, TEXT("Crates disabled: require eight definitions and three positive-total health weights"));
		return;
	}
	for (const auto& Ref : S->CrateDefinitions)
	{
		UBrPickupDefinition* Definition = Ref.LoadSynchronous();
		if (!Definition)
		{
			UE_LOG(LogBr, Error, TEXT("Crates disabled: missing loot definition %s"), *Ref.ToString());
			Definitions.Reset();
			return;
		}
		Definitions.Add(Definition);
	}
	GetWorld()->GetTimerManager().SetTimer(DropTimer, this, &ThisClass::InitialWave, 0.25f, false);
}

void ABrPickupManager::InitialWave()
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Probe;
	if (!Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()) || !Nav->GetRandomPoint(Probe))
	{
		if (++NavigationWaits < 40)
		{
			GetWorld()->GetTimerManager().SetTimer(DropTimer, this, &ThisClass::InitialWave, 0.25f, false);
			return;
		}
		UE_LOG(LogBr, Warning, TEXT("Crate initial wave: navigation unavailable after 10 seconds; bounded searches may skip drops"));
	}
	const int32 N = FBrCrateRules::InitialWave(UBrDeveloperSettings::Get()->InitialCrates);
	UE_LOG(LogBr, Display, TEXT("Crate initial wave: Requested=%d"), N);
	for (int32 I = 0; I < N; ++I) DropOne();
	ScheduleNext();
}

bool ABrPickupManager::InsideZone(const FVector& Point) const
{
	for (TActorIterator<ABrSafeZone> It(GetWorld()); It; ++It)
	{
		const FBrZoneCircle Circle = It->Circle();
		return FBrCrateRules::Contains(Point, Circle.Center, Circle.Radius, ABrZoneBounds::FindPlayableBounds(GetWorld()));
	}
	return false;
}

bool ABrPickupManager::ClearGround(const FVector& Candidate, FVector& Floor, bool bCrate, const AActor* Ignore) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (!Nav || !Nav->ProjectPointToNavigation(Candidate, Projected, FVector(100, 100, 500))) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BrCrateGround), false, Ignore);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Projected.Location + FVector(0, 0, 100),
		Projected.Location - FVector(0, 0, 200), ECC_WorldStatic, Query) || Hit.ImpactNormal.Z < 0.7) return false;
	Floor = Hit.ImpactPoint;
	// Tall capsule also rejects overhangs/walls beside a 120cm-wide cover crate.
	if (GetWorld()->OverlapBlockingTestByProfile(Floor + FVector(0, 0, 100), FQuat::Identity, TEXT("Pawn"),
		FCollisionShape::MakeCapsule(bCrate ? 85.f : 42.f, 96), Query)) return false;
	if (bCrate)
	{
		const float Clearance = FMath::Max(300.f, UBrDeveloperSettings::Get()->CratePawnClearance);
		for (TActorIterator<APawn> It(GetWorld()); It; ++It)
			if (FVector::DistSquared2D(It->GetActorLocation(), Floor) < FMath::Square(Clearance)) return false;
	}
	return true;
}

void ABrPickupManager::DropOne()
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!GS || GS->bResults || Definitions.Num() != FBrCrateRules::TypeCount) return;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	for (TActorIterator<ABrSafeZone> It(GetWorld()); It; ++It)
	{
		const FBrZoneCircle Circle = It->Circle();
		for (int32 I = 0; I < FMath::Clamp(S->CratePlacementAttempts, 1, 256); ++I)
		{
			FVector Floor;
			const FVector Candidate = FBrCrateRules::LandingCandidate(Stream, Circle.Center, Circle.Radius);
			if (!ClearGround(Candidate, Floor, true) || !InsideZone(Floor)) continue;
			const FTransform Transform(Floor + FVector(0, 0, 60));
			ABrSupplyCrate* Crate = GetWorld()->SpawnActorDeferred<ABrSupplyCrate>(ABrSupplyCrate::StaticClass(),
				Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Crate) { UE_LOG(LogBr, Error, TEXT("Crate actor allocation failed")); return; }
			Crate->Initialize(this);
			Crates.Add(Crate);
			Crate->OnDestroyed.AddUniqueDynamic(this, &ThisClass::CrateDestroyed);
			UGameplayStatics::FinishSpawningActor(Crate, Transform);
			UE_LOG(LogBr, Display, TEXT("Crate falling: Landing=%s ZoneCenter=%s ZoneRadius=%.0f Inside=1"),
				*Floor.ToString(), *Circle.Center.ToString(), Circle.Radius);
			return;
		}
		break;
	}
	UE_LOG(LogBr, Warning, TEXT("Crate drop skipped: no clear walkable candidate inside current zone/playable bounds away from pawns"));
}

bool ABrPickupManager::LandCrate(ABrSupplyCrate* Crate)
{
	if (!HasAuthority() || !bInitialized || !IsValid(Crate) || !Crates.Contains(Crate)) return false;
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	FVector Landing;
	if (!GS || GS->bResults || !InsideZone(Crate->GetActorLocation()) ||
		!ClearGround(Crate->GetActorLocation(), Landing, true, Crate) ||
		FVector::DistSquared2D(Landing, Crate->GetActorLocation()) > FMath::Square(10.f)) return false;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const TArray<int32> Loot = FBrCrateRules::Roll(Stream, FMath::Clamp(S->CrateARMin, 1, 2),
		FMath::Clamp(S->CrateARMax, 1, 2), S->CrateMGChance, S->CrateHealthMin, S->CrateHealthMax,
		S->CrateHealthWeights, S->CrateAmmoMin, S->CrateAmmoMax, S->CrateGrenadeMin, S->CrateGrenadeMax);
	int32 Spawned = 0, Closer = 0;
	TArray<FVector> Used;
	for (const int32 Type : Loot)
	{
		FVector Floor;
		bool bFound = false, bCloser = false;
		for (int32 I = 0; I < 64; ++I)
		{
			bCloser = I >= 48;
			const FVector Offset = FBrCrateRules::Scatter(Stream, bCloser ? 150.f : S->CrateScatterMin,
				bCloser ? FMath::Max(150.f, S->CrateScatterMin) : S->CrateScatterMax);
			if (!ClearGround(Landing + Offset, Floor, false, Crate)) continue;
			const double Radius = FVector::Dist2D(Floor, Landing);
			if (Radius < (bCloser ? 140.f : FMath::Max(0.f, S->CrateScatterMin)) ||
				Radius > (bCloser ? FMath::Max(150.f, S->CrateScatterMin) : FMath::Max(S->CrateScatterMin, S->CrateScatterMax))) continue;
			if (Used.ContainsByPredicate([&Floor](const FVector& Other) { return FVector::DistSquared2D(Other, Floor) < 10000; })) continue;
			bFound = true;
			break;
		}
		if (!bFound) { UE_LOG(LogBr, Warning, TEXT("Crate loot item skipped: no capsule-clear scatter/fallback position")); continue; }
		if (ABrPickup* Pickup = ABrPickup::Spawn(GetWorld(), FTransform(Floor + FVector(0, 0, 35)),
			Definitions[Type], FBrPickupPayload()))
		{
			RegisterRoundDrop(Pickup);
			Pickup->SetLifeSpan(FMath::Max(1.f, S->CrateItemLifetime));
			Used.Add(Floor);
			++Spawned;
			if (bCloser) ++Closer;
			UE_LOG(LogBr, Display, TEXT("Crate item: Type=%d Radius=%.0f Lifetime=%.1f Fallback=%d"),
				Type, FVector::Dist2D(Floor, Landing), S->CrateItemLifetime, bCloser);
		}
	}
	UE_LOG(LogBr, Display, TEXT("Crate landed: Location=%s Inside=1 Items=%d/%d Closer=%d"),
		*Landing.ToString(), Spawned, Loot.Num(), Closer);
	return true;
}

void ABrPickupManager::ScheduleNext()
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!GS || GS->bResults) return;
	const double Now = GetWorld()->GetTimeSeconds();
	GetWorld()->GetTimerManager().SetTimer(DropTimer, this, &ThisClass::ServiceDrop,
		static_cast<float>(FBrCrateRules::NextDrop(Now, GS->MatchSettings.CrateDropInterval) - Now), false);
}

void ABrPickupManager::ServiceDrop()
{
	DropOne();
	ScheduleNext(); // No final-stage gate: current circle remains the only zone policy.
}

void ABrPickupManager::RespawnAllPickups()
{
	if (!HasAuthority()) return;
	ClearRoundPickups();
	Initialize(0);
}

void ABrPickupManager::RegisterRoundDrop(ABrPickup* Pickup)
{
	if (!HasAuthority() || !IsValid(Pickup)) return;
	RoundDrops.Add(Pickup);
	Pickup->OnDestroyed.AddUniqueDynamic(this, &ThisClass::RoundDropDestroyed);
}

void ABrPickupManager::RoundDropDestroyed(AActor* DestroyedActor)
{
	RoundDrops.Remove(Cast<ABrPickup>(DestroyedActor));
}

void ABrPickupManager::CrateDestroyed(AActor* DestroyedActor)
{
	Crates.Remove(Cast<ABrSupplyCrate>(DestroyedActor));
}

void ABrPickupManager::ClearRoundPickups()
{
	if (!HasAuthority()) return;
	GetWorld()->GetTimerManager().ClearTimer(DropTimer);
	const auto Drops = MoveTemp(RoundDrops);
	for (const auto& Entry : Drops)
		if (ABrPickup* Pickup = Entry.Get())
		{
			Pickup->OnDestroyed.RemoveDynamic(this, &ThisClass::RoundDropDestroyed);
			Pickup->Destroy();
		}
	const auto OldCrates = MoveTemp(Crates);
	for (const auto& Entry : OldCrates)
		if (ABrSupplyCrate* Crate = Entry.Get())
		{
			Crate->OnDestroyed.RemoveDynamic(this, &ThisClass::CrateDestroyed);
			Crate->Destroy();
		}
	Definitions.Reset();
	bInitialized = false;
	NavigationWaits = 0;
}

void ABrPickupManager::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearRoundPickups();
	Super::EndPlay(Reason);
}
