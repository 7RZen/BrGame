#include "BrVisibilitySubsystem.h"
#include "BrZoneBounds.h"
#include "BrPickupSpawnPoint.h"
#include "BrDeveloperSettings.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TargetPoint.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/PlatformTime.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace BrMapTestDetail
{
	// Respawn/start capsule (BrGameMode.cpp: r=42, hh=96).
	constexpr float PawnRadius = 42.0f;
	constexpr float PawnHalfHeight = 96.0f;
	// 120 cm clear doorway: a standing capsule of radius 60 must sweep through every entrance.
	constexpr float ClearanceRadius = 60.0f;
	// Sweep runs from 150 cm outside the wall line to 150 cm on the other side.
	constexpr float EntranceHalfSpan = 150.0f;
	constexpr float EntranceZ = 100.0f;      // capsule centre (half-height 96 clears ground at Z=0)
	// Sphere used to keep pickups out of blocking geometry (pickup mesh is small).
	constexpr float PickupRadius = 30.0f;
	constexpr float PickupLift = 40.0f;
	// Probe distance beyond the surface on each side for the vision probes.
	constexpr float ProbeOffset = 300.0f;
	constexpr float ProbeZ = 50.0f;
	// Runtime navmesh must become usable within this wall-clock budget.
	constexpr double WorldTimeoutSec = 120.0;
	constexpr double NavTimeoutSec = 120.0;
	// Navigation must stay ready for this long before the checks run (guards against an early poll).
	constexpr double NavSettleSec = 1.0;

	// The PIE world for L_Town. Never the editor world (which has no PIE navigation lock).
	UWorld* FindPieTownWorld()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && Context.WorldType == EWorldType::PIE && World->GetMapName().Contains(TEXT("L_Town")))
			{
				return World;
			}
		}
		return nullptr;
	}

	// Actors whose vision-channel response is Block and that the line hits (one entry per actor).
	TArray<const AActor*> VisionBlockersAlong(UWorld* World, const FVector& A, const FVector& B, const FCollisionQueryParams& Params)
	{
		TArray<FHitResult> Hits;
		World->LineTraceMultiByChannel(Hits, A, B, UBrVisibilitySubsystem::VisionChannel, Params);
		TArray<const AActor*> Result;
		for (const FHitResult& Hit : Hits)
		{
			const UPrimitiveComponent* Comp = Hit.GetComponent();
			if (Comp && Hit.GetActor() && Comp->GetCollisionResponseToChannel(UBrVisibilitySubsystem::VisionChannel) == ECR_Block)
			{
				Result.AddUnique(Hit.GetActor());
			}
		}
		return Result;
	}

#if WITH_EDITOR
	// Probe line across the thin axis of an actor, at ProbeZ, starting and ending ProbeOffset outside it.
	void MakeCrossingProbe(const AActor* Actor, FVector& OutA, FVector& OutB)
	{
		FVector Center, Extent;
		Actor->GetActorBounds(false, Center, Extent);
		const bool bAlongX = Extent.X <= Extent.Y;
		const FVector Axis = bAlongX ? FVector::XAxisVector : FVector::YAxisVector;
		const double Half = bAlongX ? Extent.X : Extent.Y;
		const FVector Start = Center - Axis * (Half + ProbeOffset);
		const FVector End = Center + Axis * (Half + ProbeOffset);
		OutA = FVector(Start.X, Start.Y, ProbeZ);
		OutB = FVector(End.X, End.Y, ProbeZ);
	}

	// Finds the first StaticMeshActor with the label prefix whose crossing is isolated.
	// Blocker categories: the component's BrVision response is Block, and the only vision blocker on the line is that actor.
	// Low cover: the BrVision response is Ignore, the line has no vision blockers, and a WorldStatic trace hits the actor
	// (proves the line really crosses the cover, so "no vision block" is meaningful).
	AStaticMeshActor* FindIsolatedProbe(UWorld* World, const TCHAR* Prefix, bool bExpectBlock, const FCollisionQueryParams& Params)
	{
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (!It->GetActorLabel().StartsWith(Prefix)) continue;
			UStaticMeshComponent* Comp = It->GetStaticMeshComponent();
			if (!Comp) continue;
			FVector A, B;
			MakeCrossingProbe(*It, A, B);
			const ECollisionResponse Response = Comp->GetCollisionResponseToChannel(UBrVisibilitySubsystem::VisionChannel);
			const TArray<const AActor*> Blockers = VisionBlockersAlong(World, A, B, Params);
			if (bExpectBlock)
			{
				if (Response == ECR_Block && Blockers.Num() == 1 && Blockers[0] == static_cast<const AActor*>(*It))
				{
					return *It;
				}
			}
			else if (Response == ECR_Ignore && Blockers.Num() == 0)
			{
				TArray<FHitResult> Geometry;
				World->LineTraceMultiByChannel(Geometry, A, B, ECC_WorldStatic, Params);
				for (const FHitResult& Hit : Geometry)
				{
					if (Hit.GetActor() == static_cast<const AActor*>(*It)) return *It;
				}
			}
		}
		return nullptr;
	}
#endif

	// Pawn-channel capsule sweep through a doorway centre. Returns true if anything is hit (including start penetration).
	bool SweepBlocked(UWorld* World, const FVector& Centre, bool bAlongX, float Radius, const FCollisionQueryParams& Params)
	{
		const FVector Dir = bAlongX ? FVector::XAxisVector : FVector::YAxisVector;
		const FVector Start = Centre - Dir * EntranceHalfSpan;
		const FVector End = Centre + Dir * EntranceHalfSpan;
		FHitResult Hit;
		return World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, PawnHalfHeight), Params);
	}
}

// Waits for the L_Town PIE world, releases the PIE navigation lock, builds navigation at runtime and waits until
// it is usable (elapsed-time timeout), then runs the layout, entrance, navigation and vision checks.
class FBrTownLayoutCheck : public IAutomationLatentCommand
{
public:
	explicit FBrTownLayoutCheck(FAutomationTestBase* InTest) : Test(InTest), StartSec(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		switch (Phase)
		{
		case EPhase::WaitWorld:
		{
			World = BrMapTestDetail::FindPieTownWorld();
			if (!World)
			{
				if (FPlatformTime::Seconds() - StartSec > BrMapTestDetail::WorldTimeoutSec)
				{
					Test->AddError(TEXT("No PIE world named L_Town was created within the timeout"));
					return true;
				}
				return false;
			}
			Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav)
			{
				Test->AddError(TEXT("PIE world L_Town has no navigation system"));
				return true;
			}
			// PIE sets ENavigationBuildLock::NoUpdateInPIE (flag 0x4), which makes Build() a no-op.
			// Release it explicitly, then request the build.
			Nav->RemoveNavigationBuildLock(ENavigationBuildLock::NoUpdateInPIE,
				UNavigationSystemV1::ELockRemovalRebuildAction::Rebuild);
			Nav->Build();
			BuildStartSec = FPlatformTime::Seconds();
			Phase = EPhase::WaitNav;
			return false;
		}
		case EPhase::WaitNav:
		{
			const double Now = FPlatformTime::Seconds();
			const bool bReady = !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World)
				&& !Nav->IsNavigationBuildInProgress()
				&& Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr;
			if (bReady)
			{
				if (ReadySinceSec == 0.0) ReadySinceSec = Now;
				if (Now - ReadySinceSec >= BrMapTestDetail::NavSettleSec)
				{
					NavBuildSec = Now - BuildStartSec;
					Phase = EPhase::Check;
				}
				return false;
			}
			ReadySinceSec = 0.0;
			if (Now - BuildStartSec > BrMapTestDetail::NavTimeoutSec)
			{
				Test->AddError(FString::Printf(TEXT("Runtime navigation on PIE L_Town not usable after %.0f s (locked=%d building=%d)"),
					BrMapTestDetail::NavTimeoutSec,
					UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World) ? 1 : 0,
					Nav->IsNavigationBuildInProgress() ? 1 : 0));
				return true;
			}
			return false;
		}
		case EPhase::Check:
			Check();
			return true;
		}
		return true;
	}

private:
	enum class EPhase : uint8 { WaitWorld, WaitNav, Check };

	void Check()
	{
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(BrMapTest), false);
		ANavigationData* NavData = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		Test->TestNotNull(TEXT("Runtime navmesh data exists on PIE L_Town"), NavData);
		Test->AddInfo(FString::Printf(TEXT("Navigation checked: world=%s (PIE) navdata=%s build=%.2fs"),
			*World->GetName(), NavData ? *NavData->GetName() : TEXT("none"), NavBuildSec));

		auto NavProjects = [&](const FVector& Point) -> bool
		{
			FNavLocation Anchor;
			return Nav->ProjectPointToNavigation(Point, Anchor, FVector(100, 100, 300));
		};

		// Playable bounds from the zone actor (2D containment; zone circles are 2D).
		const FBox Playable = ABrZoneBounds::FindPlayableBounds(World);
		Test->TestTrue(TEXT("Zone playable bounds are valid"), Playable.IsValid != 0);
		auto InsidePlayable2D = [&Playable](const FVector& Loc)
		{
			return Loc.X >= Playable.Min.X && Loc.X <= Playable.Max.X && Loc.Y >= Playable.Min.Y && Loc.Y <= Playable.Max.Y;
		};

		// PlayerStarts: capsule must not overlap blocking geometry.
		int32 StartCount = 0, StartBlocked = 0, StartOutside = 0;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			++StartCount;
			const FVector Loc = It->GetActorLocation();
			if (World->OverlapBlockingTestByChannel(Loc, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(BrMapTestDetail::PawnRadius, BrMapTestDetail::PawnHalfHeight), Params))
			{
				++StartBlocked;
				Test->AddError(FString::Printf(TEXT("PlayerStart %s overlaps blocking geometry at %s"), *It->GetName(), *Loc.ToString()));
			}
			if (!InsidePlayable2D(Loc)) ++StartOutside;
		}
		Test->TestTrue(TEXT("L_Town has at least 40 PlayerStarts"), StartCount >= 40);
		Test->TestEqual(TEXT("PlayerStarts overlapping blocking geometry"), StartBlocked, 0);
		Test->TestEqual(TEXT("PlayerStarts outside playable bounds"), StartOutside, 0);

		// Pickup spawn points: inside zone bounds, not in blocking geometry, on ground and navmesh.
		int32 PickupCount = 0, PickupBlocked = 0, PickupOutside = 0, PickupNoGround = 0, PickupNoNav = 0;
		const ABrPickupSpawnPoint* PickupMinX = nullptr;
		const ABrPickupSpawnPoint* PickupMaxX = nullptr;
		for (TActorIterator<ABrPickupSpawnPoint> It(World); It; ++It)
		{
			++PickupCount;
			const FVector Loc = It->GetActorLocation();
			if (!PickupMinX || Loc.X < PickupMinX->GetActorLocation().X) PickupMinX = *It;
			if (!PickupMaxX || Loc.X > PickupMaxX->GetActorLocation().X) PickupMaxX = *It;
			if (!InsidePlayable2D(Loc)) ++PickupOutside;
			if (World->OverlapBlockingTestByChannel(Loc + FVector(0, 0, BrMapTestDetail::PickupLift), FQuat::Identity,
				ECC_WorldStatic, FCollisionShape::MakeSphere(BrMapTestDetail::PickupRadius), Params)) ++PickupBlocked;
			if (!World->LineTraceTestByChannel(Loc + FVector(0, 0, 500), Loc - FVector(0, 0, 500), ECC_WorldStatic, Params)) ++PickupNoGround;
			if (!NavProjects(Loc)) ++PickupNoNav;
		}
		Test->TestTrue(TEXT("L_Town has at least 144 pickup spawn points"), PickupCount >= 144);
		Test->TestEqual(TEXT("Pickup spawn points outside zone bounds"), PickupOutside, 0);
		Test->TestEqual(TEXT("Pickup spawn points inside blocking geometry"), PickupBlocked, 0);
		Test->TestEqual(TEXT("Pickup spawn points without ground"), PickupNoGround, 0);
		Test->TestEqual(TEXT("Pickup spawn points without navmesh projection"), PickupNoNav, 0);
		Test->AddInfo(FString::Printf(TEXT("Town layout: PlayerStarts=%d Pickups=%d NavProjectedPickups=%d"),
			StartCount, PickupCount, PickupCount - PickupNoNav));

		// Navigation proof: a path must exist between the two pickup spawns at the extremes of X.
		if (NavData && PickupMinX && PickupMaxX)
		{
			const FPathFindingQuery Query(World, *NavData, PickupMinX->GetActorLocation(), PickupMaxX->GetActorLocation());
			const FPathFindingResult PathResult = Nav->FindPathSync(Query);
			Test->TestTrue(TEXT("Runtime navmesh path exists across the map (pickup min-X to max-X)"), PathResult.IsSuccessful());
			if (PathResult.IsSuccessful() && PathResult.Path.IsValid())
			{
				Test->AddInfo(FString::Printf(TEXT("Nav path points=%d"), PathResult.Path->GetPathPoints().Num()));
			}
		}

		// Doorways: every BrEntrance marker (written by create_br_map.py) must be clear for a 120 cm capsule.
		int32 Entrances = 0, EntranceBlocked = 0, EntranceNoAxis = 0;
		for (TActorIterator<ATargetPoint> It(World); It; ++It)
		{
			if (!It->Tags.Contains(FName(TEXT("BrEntrance")))) continue;
			++Entrances;
			bool bAlongX = false, bHaveAxis = false;
			FString Name;
			for (const FName& Tag : It->Tags)
			{
				const FString S = Tag.ToString();
				if (S == TEXT("BrEntranceAxis:X")) { bAlongX = true; bHaveAxis = true; }
				else if (S == TEXT("BrEntranceAxis:Y")) { bHaveAxis = true; }
				else if (S.StartsWith(TEXT("BrEntranceName:"))) { Name = S.RightChop(15); }
			}
			if (!bHaveAxis)
			{
				++EntranceNoAxis;
				continue;
			}
			const FVector Centre(It->GetActorLocation().X, It->GetActorLocation().Y, BrMapTestDetail::EntranceZ);
			if (BrMapTestDetail::SweepBlocked(World, Centre, bAlongX, BrMapTestDetail::ClearanceRadius, Params))
			{
				++EntranceBlocked;
				Test->AddError(FString::Printf(TEXT("Entrance %s is not clear for a 120 cm capsule at %s"), *Name, *Centre.ToString()));
			}
		}
		Test->TestTrue(TEXT("L_Town has at least 19 BrEntrance markers (one per intended doorway)"), Entrances >= 19);
		Test->TestEqual(TEXT("BrEntrance markers without an axis tag"), EntranceNoAxis, 0);
		Test->TestEqual(TEXT("Entrances blocked for a 120 cm capsule"), EntranceBlocked, 0);

#if WITH_EDITOR
		// Vision: one isolated probe per blocker category, plus a pawn-channel positive control.
		FCollisionQueryParams VisionParams(SCENE_QUERY_STAT(BrMapVision), false);
		struct FCategory { const TCHAR* Label; const TCHAR* Prefix; bool bBlocks; };
		const FCategory Categories[] = {
			{ TEXT("building wall"), TEXT("BR_Wall_"), true },
			{ TEXT("container"), TEXT("BR_Container_"), true },
			{ TEXT("trunk"), TEXT("BR_Trunk_"), true },
			{ TEXT("map boundary"), TEXT("BR_Boundary_"), true },
			{ TEXT("low cover crate"), TEXT("BR_Crate_"), false },
		};
		for (const FCategory& Category : Categories)
		{
			AStaticMeshActor* Probe = BrMapTestDetail::FindIsolatedProbe(World, Category.Prefix, Category.bBlocks, VisionParams);
			Test->TestNotNull(*FString::Printf(TEXT("Isolated vision probe found: %s"), Category.Label), Probe);
			if (!Probe) continue;
			FVector A, B;
			BrMapTestDetail::MakeCrossingProbe(Probe, A, B);
			const ECollisionResponse Response = Probe->GetStaticMeshComponent()->GetCollisionResponseToChannel(UBrVisibilitySubsystem::VisionChannel);
			Test->AddInfo(FString::Printf(TEXT("Vision probe %s: actor=%s vision=%s expect=%s"), Category.Label, *Probe->GetActorLabel(),
				Response == ECR_Block ? TEXT("Block") : (Response == ECR_Ignore ? TEXT("Ignore") : TEXT("Overlap")),
				Category.bBlocks ? TEXT("blocks") : TEXT("does not block")));
			if (Category.bBlocks)
			{
				Test->TestEqual(*FString::Printf(TEXT("BrVision response is Block: %s"), Category.Label), Response, ECR_Block);
				const TArray<const AActor*> Blockers = BrMapTestDetail::VisionBlockersAlong(World, A, B, VisionParams);
				Test->TestEqual(*FString::Printf(TEXT("Only %s blocks its vision crossing"), Category.Label), Blockers.Num(), 1);
			}
			else
			{
				Test->TestEqual(*FString::Printf(TEXT("BrVision response is Ignore: %s"), Category.Label), Response, ECR_Ignore);
				const TArray<const AActor*> Blockers = BrMapTestDetail::VisionBlockersAlong(World, A, B, VisionParams);
				Test->TestEqual(*FString::Printf(TEXT("No vision blocker on %s crossing"), Category.Label), Blockers.Num(), 0);
			}
		}

		// Pawn-channel control: the wall probe line at pawn height must be blocked for a capsule (movement and bullets).
		if (AStaticMeshActor* Wall = BrMapTestDetail::FindIsolatedProbe(World, TEXT("BR_Wall_"), true, VisionParams))
		{
			FVector A, B;
			BrMapTestDetail::MakeCrossingProbe(Wall, A, B);
			const FVector Centre((A.X + B.X) * 0.5, (A.Y + B.Y) * 0.5, BrMapTestDetail::EntranceZ);
			const bool bAlongX = FMath::Abs(B.X - A.X) > FMath::Abs(B.Y - A.Y);
			Test->TestTrue(TEXT("Pawn capsule sweep is blocked by a building wall"),
				BrMapTestDetail::SweepBlocked(World, Centre, bAlongX, BrMapTestDetail::PawnRadius, VisionParams));
		}
#else
		Test->AddWarning(TEXT("Vision probes skipped: actor labels need an editor build"));
#endif
	}

	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	UNavigationSystemV1* Nav = nullptr;
	EPhase Phase = EPhase::WaitWorld;
	double StartSec = 0.0;
	double BuildStartSec = 0.0;
	double ReadySinceSec = 0.0;
	double NavBuildSec = 0.0;
};


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrTownLayoutTest, "BrGame.Map.TownLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrTownLayoutTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TEXT("/Game/BrGame/Maps/L_Town")))
	{
		AddError(TEXT("Could not open /Game/BrGame/Maps/L_Town"));
		return true;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FBrTownLayoutCheck(this));
	return true;
}

#endif
