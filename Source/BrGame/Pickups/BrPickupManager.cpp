#include "BrPickupManager.h"
#include "BrPickup.h"
#include "BrPickupDefinition.h"
#include "BrInventoryComponent.h"
#include "BrCrateRules.h"
#include "BrSupplyCrate.h"
#include "BrDeveloperSettings.h"
#include "BrGameState.h"
#include "BrSafeZone.h"
#include "BrZoneBounds.h"
#include "BrPlayableGround.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UObject/Package.h"

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
	if (!ResolveMapCounts()) return;
	PlayableBounds = ABrZoneBounds::FindPlayableBounds(GetWorld());
	if (FParse::Param(FCommandLine::Get(), TEXT("BrCrateNavProbe")))
	{
		NavigationProbeStarted = FPlatformTime::Seconds();
		ProbeNavigation();
		GetWorldTimerManager().SetTimer(NavigationProbeTimer, this, &ThisClass::ProbeNavigation, 1.f, true);
	}
	if (S->CrateDefinitions.Num() != FBrCrateRules::TypeCount ||
		S->CrateHealthWeights.Num() != 3 ||
		FBrPickupRules::WeightedIndex(S->CrateHealthWeights, Stream) == INDEX_NONE)
	{
		UE_LOG(LogBr, Error, TEXT("Crates disabled: require nine definitions and three positive-total health weights"));
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
	if (!Cast<UBrArmorPickupDefinition>(Definitions[FBrCrateRules::Armor]))
	{
		UE_LOG(LogBr, Error, TEXT("Crates disabled: Armor must be a BrArmorPickupDefinition."));
		Definitions.Reset();
		return;
	}
	QuestDefinition = Cast<UBrQuestPickupDefinition>(S->QuestPickup.LoadSynchronous());
	if (!QuestDefinition)
	{
		UE_LOG(LogBr, Error, TEXT("Crates disabled: missing/invalid interactive QuestPickup definition %s"), *S->QuestPickup.ToString());
		Definitions.Reset();
		return;
	}
	TArray<FName> WeaponKeys;
	S->WeaponRegistry.GetKeys(WeaponKeys);
	WeaponKeys.Sort(FNameLexicalLess());
	for (const FName Key : WeaponKeys)
	{
		UBrWeaponDefinition* Weapon = S->WeaponRegistry.FindChecked(Key).LoadSynchronous();
		if (UBrInventoryComponent::IsValidWeaponDefinition(Weapon)) QuestWeapons.AddUnique(Weapon);
		else UE_LOG(LogBr, Error, TEXT("Quest weapon registry entry rejected: %s"), *Key.ToString());
	}
	if (QuestWeapons.IsEmpty())
	{
		UE_LOG(LogBr, Error, TEXT("Crates disabled: no valid quest weapon targets in WeaponRegistry"));
		Definitions.Reset();
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(DropTimer, this, &ThisClass::InitialWave, 0.25f, false);
}

void ABrPickupManager::InitialWave()
{
	// Collision-verified ground is available without waiting for navigation generation.
	UE_LOG(LogBr, Display, TEXT("Crate initial wave: Map=%s Count=%d PerDrop=%d"),
		*MapId.ToString(), InitialCrateCount, IntervalCrateCount);
	DropWave(true);
	ScheduleNext();
}

bool ABrPickupManager::ResolveMapCounts()
{
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	const FString Package = UWorld::RemovePIEPrefix(GetWorld()->GetPackage()->GetName());
	const FBrMapEntry* Entry = GS ? S->FindMap(GS->MatchSettings.ResolvedMapId) : nullptr;
	if (!Entry || Entry->Map.ToSoftObjectPath().GetLongPackageName() != Package)
	{
		if (GS && !GS->MatchSettings.ResolvedMapId.IsNone())
			UE_LOG(LogBr, Warning, TEXT("Crate map outcome '%s' does not match server world '%s'; resolving from current world"),
				*GS->MatchSettings.ResolvedMapId.ToString(), *Package);
		Entry = nullptr;
		for (const FBrMapEntry& Candidate : S->ValidMaps())
			if (Candidate.Map.ToSoftObjectPath().GetLongPackageName() == Package)
			{
				Entry = S->FindMap(Candidate.Id);
				break;
			}
	}
	if (!Entry)
	{
		UE_LOG(LogBr, Error, TEXT("Crates disabled: server world '%s' has no allowlisted map entry"), *Package);
		return false;
	}
	MapId = Entry->Id;
	InitialCrateCount = FBrCrateRules::InitialWave(Entry->InitialCrates);
	IntervalCrateCount = FBrCrateRules::IntervalWave(Entry->CratesPerDrop);
	if (InitialCrateCount != Entry->InitialCrates || IntervalCrateCount != Entry->CratesPerDrop)
		UE_LOG(LogBr, Warning, TEXT("Clamped invalid crate counts for map %s: Initial=%d PerDrop=%d"),
			*MapId.ToString(), InitialCrateCount, IntervalCrateCount);
	return true;
}

void ABrPickupManager::DropWave(bool bInitial)
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!HasAuthority() || !bInitialized || !GS || GS->bResults) return;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const float Spacing = FBrCrateRules::WaveSpacing(S->CrateWaveSpacing, S->CrateScatterMin, S->CrateScatterMax);
	const int32 Count = bInitial ? InitialCrateCount : IntervalCrateCount;
	TArray<FVector> Landings;
	for (int32 I = 0; I < Count; ++I) DropOne(Landings, Spacing);
	UE_LOG(LogBr, Display, TEXT("Crate %s wave placed: Map=%s Count=%d Spawned=%d Spacing=%.0f"),
		bInitial ? TEXT("initial") : TEXT("interval"), *MapId.ToString(), Count, Landings.Num(), Spacing);
}

bool ABrPickupManager::InsideZone(const FVector& Point) const
{
	for (TActorIterator<ABrSafeZone> It(GetWorld()); It; ++It)
	{
		const FBrZoneCircle Circle = It->Circle();
		return FBrCrateRules::Contains(Point, Circle.Center, Circle.Radius, PlayableBounds);
	}
	return false;
}

bool ABrPickupManager::ClearGround(const FVector& Candidate, FVector& Floor, bool bCrate, const AActor* Ignore,
	bool* bUsedNavigation) const
{
	if (bUsedNavigation) *bUsedNavigation = false;
	FBrGroundQuery GroundQuery;
	GroundQuery.Radius = bCrate ? 85.f : 42.f;
	GroundQuery.bRequireTaggedStaticFloor = true;
	GroundQuery.bUsePawnProfile = true;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	GroundQuery.MinFloorHalfWidth = FBrCrateRules::FloorHalfWidth(S->CrateScatterMin, S->CrateScatterMax);
	// Full-height trace rejects roof/cover tops even if nav projects onto them.
	GroundQuery.TraceUp = FMath::Max(1000.f, static_cast<float>(PlayableBounds.Max.Z - Candidate.Z + 100));
	GroundQuery.TraceDown = FMath::Max(2000.f, static_cast<float>(Candidate.Z - PlayableBounds.Min.Z + 100));
	const auto Verify = [&](const FVector& Point)
	{
		if (!FBrPlayableGround::Find(GetWorld(), Point, Floor, GroundQuery, Ignore)) return false;
		if (bCrate)
		{
			const float Clearance = FMath::Max(300.f, UBrDeveloperSettings::Get()->CratePawnClearance);
			for (TActorIterator<APawn> It(GetWorld()); It; ++It)
				if (FVector::DistSquared2D(It->GetActorLocation(), Floor) < FMath::Square(Clearance)) return false;
		}
		return true;
	};
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	const bool bProjected = Nav && Nav->ProjectPointToNavigation(Candidate, Projected, FVector(100, 100, 500)) &&
		Projected.HasNodeRef() && Verify(Projected.Location);
	if (bProjected)
	{
		if (bUsedNavigation) *bUsedNavigation = true;
	}
	else if (!Verify(Candidate)) return false;
	return true;
}

bool ABrPickupManager::DropOne(TArray<FVector>& WaveLandings, float Spacing)
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!GS || GS->bResults || Definitions.Num() != FBrCrateRules::TypeCount) return false;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	FVector SearchCenter = FVector::ZeroVector;
	for (TActorIterator<ABrSafeZone> It(GetWorld()); It; ++It)
	{
		const FBrZoneCircle Circle = It->Circle();
		SearchCenter = Circle.Center;
		for (int32 I = 0; I < FMath::Clamp(S->CratePlacementAttempts, 1, 256); ++I)
		{
			FVector Floor;
			bool bUsedNav = false;
			const FVector Candidate = FBrCrateRules::LandingCandidate(Stream, Circle.Center, Circle.Radius);
			if (!FBrCrateRules::Contains(Candidate, Circle.Center, Circle.Radius, PlayableBounds) ||
				!ClearGround(Candidate, Floor, true, nullptr, &bUsedNav) || !InsideZone(Floor) ||
				!FBrCrateRules::IsWaveSpaced(Floor, WaveLandings, Spacing)) continue;
			const FTransform Transform(Floor + FVector(0, 0, 60));
			ABrSupplyCrate* Crate = GetWorld()->SpawnActorDeferred<ABrSupplyCrate>(ABrSupplyCrate::StaticClass(),
				Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Crate) { UE_LOG(LogBr, Error, TEXT("Crate actor allocation failed")); return false; }
			Crate->Initialize(this);
			Crates.Add(Crate);
			Crate->OnDestroyed.AddUniqueDynamic(this, &ThisClass::CrateDestroyed);
			UGameplayStatics::FinishSpawningActor(Crate, Transform);
			WaveLandings.Add(Floor);
			UE_LOG(LogBr, Display, TEXT("Crate falling: Landing=%s ZoneCenter=%s ZoneRadius=%.0f Inside=1 Attempts=%d Ground=%s TaggedStatic=1 CapsuleClear=1 PawnClear=1 WaveClear=1"),
				*Floor.ToString(), *Circle.Center.ToString(), Circle.Radius, I + 1, bUsedNav ? TEXT("Navigation") : TEXT("Trace"));
			return true;
		}
		break;
	}
	FNavLocation Probe;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const bool bNavProbe = Nav && Nav->GetRandomPoint(Probe) && Probe.HasNodeRef();
	UE_LOG(LogBr, Warning, TEXT("Crate drop skipped: Map=%s no clear walkable candidate inside current zone/playable bounds away from pawns and wave mates (Attempts=%d Spacing=%.0f Center=%s NavProbe=%s)"),
		*MapId.ToString(), FMath::Clamp(S->CratePlacementAttempts, 1, 256), Spacing, *SearchCenter.ToString(), bNavProbe ? *Probe.Location.ToString() : TEXT("Unavailable"));
	return false;
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
	if (!QuestDefinition || QuestWeapons.IsEmpty())
	{
		UE_LOG(LogBr, Error, TEXT("Crate landing cancelled: quest definition/weapon registry unavailable"));
		return false;
	}
	// Reserve every mandatory token position before spawning any loot. The normal
	// ground policy is bounded and independent of navigation readiness.
	const int32 QuestCount = FBrQuestRules::OfferCount(Stream);
	TArray<FVector> Used;
	for (int32 Q = 0; Q < QuestCount; ++Q)
	{
		bool bFound = false;
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const bool bCloser = Attempt >= 48;
			const float Min = bCloser ? 150.f : FMath::Max(150.f, S->CrateScatterMin);
			const float Max = bCloser ? FMath::Max(150.f, S->CrateScatterMin) : FMath::Max(Min, S->CrateScatterMax);
			FVector Floor;
			if (!ClearGround(Landing + FBrCrateRules::Scatter(Stream, Min, Max), Floor, false, Crate) ||
				!InsideZone(Floor)) continue;
			const double Radius = FVector::Dist2D(Floor, Landing);
			if (Radius < Min || Radius > Max ||
				Used.ContainsByPredicate([&](const FVector& Other) { return FVector::DistSquared2D(Other, Floor) < 10000; })) continue;
			Used.Add(Floor);
			bFound = true;
			break;
		}
		if (!bFound)
		{
			UE_LOG(LogBr, Warning, TEXT("Crate landing cancelled: cannot reserve %d required quest tokens (64 attempts/token)"), QuestCount);
			return false;
		}
	}
	TArray<ABrPickup*> Tokens;
	for (const FVector& Floor : Used)
	{
		FBrPickupPayload Payload;
		Payload.QuestOffer.Id = FGuid::NewGuid();
		Payload.QuestOffer.Objective = FBrQuestRules::SelectObjective(Stream);
		if (Payload.QuestOffer.Objective == EBrQuestObjective::CollectWeapon)
			Payload.QuestOffer.Weapon = QuestWeapons[FBrQuestRules::SelectWeapon(Stream, QuestWeapons.Num())];
		ABrPickup* Token = ABrPickup::Spawn(GetWorld(), FTransform(Floor + FVector(0, 0, 35)), QuestDefinition, Payload);
		if (!Token)
		{
			for (ABrPickup* SpawnedToken : Tokens) SpawnedToken->Destroy();
			UE_LOG(LogBr, Error, TEXT("Crate landing cancelled: mandatory quest token allocation failed"));
			return false;
		}
		Tokens.Add(Token);
	}
	for (ABrPickup* Token : Tokens)
	{
		RegisterRoundDrop(Token);
		Token->SetLifeSpan(FMath::Max(1.f, S->CrateItemLifetime));
	}
	const TArray<int32> Loot = FBrCrateRules::Roll(Stream, FMath::Clamp(S->CrateARMin, 1, 2),
		FMath::Clamp(S->CrateARMax, 1, 2), S->CrateMGChance, S->CrateHealthMin, S->CrateHealthMax,
		S->CrateHealthWeights, S->CrateAmmoMin, S->CrateAmmoMax, S->CrateGrenadeMin, S->CrateGrenadeMax,
		S->CrateArmorMin, S->CrateArmorMax);
	int32 Spawned = 0, Closer = 0;
	for (const int32 Type : Loot)
	{
		FVector Floor;
		bool bFound = false, bCloser = false;
		int32 Attempts = 0;
		bool bUsedNav = false;
		for (int32 I = 0; I < 64; ++I)
		{
			Attempts = I + 1;
			bCloser = I >= 48;
			const FVector Offset = FBrCrateRules::Scatter(Stream, bCloser ? 150.f : S->CrateScatterMin,
				bCloser ? FMath::Max(150.f, S->CrateScatterMin) : S->CrateScatterMax);
			if (!ClearGround(Landing + Offset, Floor, false, Crate, &bUsedNav)) continue;
			const double Radius = FVector::Dist2D(Floor, Landing);
			if (Radius < (bCloser ? 140.f : FMath::Max(0.f, S->CrateScatterMin)) ||
				Radius > (bCloser ? FMath::Max(150.f, S->CrateScatterMin) : FMath::Max(S->CrateScatterMin, S->CrateScatterMax))) continue;
			if (Used.ContainsByPredicate([&Floor](const FVector& Other) { return FVector::DistSquared2D(Other, Floor) < 10000; })) continue;
			bFound = true;
			break;
		}
		if (!bFound) { UE_LOG(LogBr, Warning, TEXT("Crate loot item skipped: no capsule-clear scatter/fallback position Attempts=%d"), Attempts); continue; }
		if (ABrPickup* Pickup = ABrPickup::Spawn(GetWorld(), FTransform(Floor + FVector(0, 0, 35)),
			Definitions[Type], FBrPickupPayload()))
		{
			RegisterRoundDrop(Pickup);
			Pickup->SetLifeSpan(FMath::Max(1.f, S->CrateItemLifetime));
			Used.Add(Floor);
			++Spawned;
			if (bCloser) ++Closer;
			UE_LOG(LogBr, Display, TEXT("Crate item: Type=%d Radius=%.0f Lifetime=%.1f Fallback=%d Attempts=%d Ground=%s"),
				Type, FVector::Dist2D(Floor, Landing), S->CrateItemLifetime, bCloser, Attempts, bUsedNav ? TEXT("Navigation") : TEXT("Trace"));
		}
	}
	UE_LOG(LogBr, Display, TEXT("Crate landed: Location=%s Inside=1 Items=%d/%d Closer=%d Quests=%d"),
		*Landing.ToString(), Spawned, Loot.Num(), Closer, Tokens.Num());
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
	DropWave(false);
	ScheduleNext(); // No final-stage gate: current circle remains the only zone policy.
}

void ABrPickupManager::ProbeNavigation()
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ARecastNavMesh* Mesh = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
	FNavLocation Point;
	const bool bReady = Nav && Nav->GetRandomPoint(Point) && Point.HasNodeRef();
	const double Elapsed = FPlatformTime::Seconds() - NavigationProbeStarted;
	// Opt-in diagnostics only; never rebuild, unlock or otherwise affect nav/zone/respawn.
	UE_LOG(LogBr, Display, TEXT("Crate nav probe: Map=%s RealSeconds=%.2f WorldSeconds=%.2f Ready=%d Generation=%d ActiveTiles=%d Locked=%d Building=%d NeedsRebuild=%d InitialRebuild=%d"),
		*MapId.ToString(), Elapsed, GetWorld()->GetTimeSeconds(), bReady,
		Mesh ? static_cast<int32>(Mesh->GetRuntimeGenerationMode()) : -1,
		Mesh ? Mesh->GetNumActiveTiles() : 0, Nav && Nav->IsNavigationBuildingLocked(),
		Nav && Nav->IsNavigationBuildInProgress(), Mesh && Mesh->NeedsRebuildOnLoad(),
		Mesh && Mesh->RequiresInitialRebuild());
	if (++NavigationProbes >= 120 || bReady) GetWorldTimerManager().ClearTimer(NavigationProbeTimer);
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
	GetWorldTimerManager().ClearTimer(NavigationProbeTimer);
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
	QuestDefinition = nullptr;
	QuestWeapons.Reset();
	bInitialized = false;
	NavigationProbes = 0;
	PlayableBounds = FBox(ForceInit);
	MapId = NAME_None;
	InitialCrateCount = 0;
	IntervalCrateCount = 1;
}

void ABrPickupManager::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearRoundPickups();
	Super::EndPlay(Reason);
}
