#include "BrCrateRules.h"
#include "BrMatchSettings.h"
#include "BrSessionRules.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "BrDeveloperSettings.h"
#include "BrPickup.h"
#include "BrPickupDefinition.h"
#include "BrPickupManager.h"
#include "BrSupplyCrate.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrSafeZone.h"
#include "BrZoneBounds.h"
#include "BrPlayableGround.h"
#include "Components/BoxComponent.h"
#include "NavigationSystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCrateLootTest, "BrGame.Crate.SeededLoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrCrateLootTest::RunTest(const FString&)
{
	FRandomStream A(16427), B(16427);
	int32 MG = 0, Small = 0, Medium = 0, Large = 0, AR[3] = {};
	for (int32 I = 0; I < 10000; ++I)
	{
		const auto Roll = [](FRandomStream& S, float Chance)
		{ return FBrCrateRules::Roll(S, 1, 2, Chance, 3, 8, {60,30,10}, 3, 8, 2, 4); };
		const TArray<int32> Loot = Roll(A, 0.5f);
		if (Loot != Roll(B, 0.5f)) { AddError(TEXT("Same seed changed loot")); return false; }
		int32 Counts[FBrCrateRules::TypeCount] = {};
		for (int32 Type : Loot) ++Counts[Type];
		if (Counts[0] != 1 || Counts[1] < 1 || Counts[1] > 2 || Counts[2] > 1 ||
			Counts[3] < 3 || Counts[3] > 8 || Counts[4] < 2 || Counts[4] > 4 ||
			Counts[5] + Counts[6] + Counts[7] < 3 || Counts[5] + Counts[6] + Counts[7] > 8)
		{ AddError(TEXT("Loot outside configured bounds")); return false; }
		MG += Counts[2]; Small += Counts[5]; Medium += Counts[6]; Large += Counts[7]; ++AR[Counts[1]];
		if (Roll(A, 0).Contains(FBrCrateRules::MachineGun) || !Roll(A, 1).Contains(FBrCrateRules::MachineGun))
		{ AddError(TEXT("MG probability endpoints not respected")); return false; }
		// Keep deterministic streams aligned after the endpoint probes.
		Roll(B, 0); Roll(B, 1);
	}
	TestTrue(TEXT("50 percent MG distribution"), MG > 4700 && MG < 5300);
	TestTrue(TEXT("Both AR counts exercised"), AR[1] > 4700 && AR[2] > 4700);
	const float Health = static_cast<float>(Small + Medium + Large);
	TestTrue(TEXT("Health weights approximately 60/30/10"),
		FMath::Abs(Small / Health - 0.6f) < 0.02f && FMath::Abs(Medium / Health - 0.3f) < 0.02f &&
		FMath::Abs(Large / Health - 0.1f) < 0.02f);
	FRandomStream S(21);
	const auto Custom = FBrCrateRules::Roll(S, 2, 2, 0, 7, 7, {0,0,1}, 4, 4, 3, 3);
	TestEqual(TEXT("Custom count totals"), Custom.Num(), 17);
	TestEqual(TEXT("Only weighted health selected"), Custom.FilterByPredicate([](int32 T) { return T == FBrCrateRules::Health100; }).Num(), 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCrateGeometryTest, "BrGame.Crate.ScatterAndLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrCrateGeometryTest::RunTest(const FString&)
{
	FRandomStream S(913);
	const FVector Center(1800, -900, 0);
	const FBox Bounds(FVector(-2000,-2000,-100), FVector(3000,3000,100));
	int32 Accepted = 0, Rejected = 0;
	double RadiusTotal = 0;
	FVector Sum = FVector::ZeroVector;
	for (int32 I = 0; I < 10000; ++I)
	{
		const FVector Offset = FBrCrateRules::Scatter(S, 500, 1000);
		const double Radius = Offset.Size2D();
		if (Radius < 499.99 || Radius > 1000.01 || Offset.Z != 0)
		{ AddError(TEXT("Scatter escaped annulus")); return false; }
		RadiusTotal += Radius; Sum += Offset;
		const FVector Candidate = FBrCrateRules::LandingCandidate(S, Center, 2000);
		if (FVector::Dist2D(Candidate, Center) > 2000.01) { AddError(TEXT("Candidate escaped zone")); return false; }
		if (FBrCrateRules::Contains(Candidate, Center, 2000, Bounds)) ++Accepted;
		else ++Rejected;
	}
	TestTrue(TEXT("Scatter covers full annulus without directional bias"), RadiusTotal / 10000 > 740 &&
		RadiusTotal / 10000 < 760 && (Sum / 10000).Size2D() < 20);
	TestTrue(TEXT("Intersecting playable bounds reject and accept candidates"), Accepted > 1000 && Rejected > 1000);
	TestFalse(TEXT("Collapsed final circle has no landing area"), FBrCrateRules::Contains(Center, Center, 0, Bounds));
	TestFalse(TEXT("Outside zone rejected even inside bounds"), FBrCrateRules::Contains(FVector::ZeroVector, Center, 100, Bounds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCrateScheduleTest, "BrGame.Crate.ScheduleAndHostClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrCrateScheduleTest::RunTest(const FString&)
{
	FBrMatchSettings Settings;
	Settings.CrateDropInterval = -10; Settings.Sanitize();
	TestEqual(TEXT("Invalid host interval minimum"), Settings.CrateDropInterval, 1.f);
	Settings.CrateDropInterval = 9999; Settings.Sanitize();
	TestEqual(TEXT("Invalid host interval maximum"), Settings.CrateDropInterval, 3600.f);
	TestEqual(TEXT("Nonfinite defaults"), FBrMatchSettings::ClampCrateDropInterval(std::numeric_limits<float>::infinity()), 30.f);
	TestEqual(TEXT("Initial wave"), FBrCrateRules::InitialWave(4), 4);
	TestEqual(TEXT("Disabled initial wave"), FBrCrateRules::InitialWave(-1), 0);
	double Time = 123.5;
	for (int32 I = 0; I < 100; ++I) Time = FBrCrateRules::NextDrop(Time, 12.5f);
	TestEqual(TEXT("Periodic wave deadline is independent of initial wave count"), Time, 1373.5);
	TestEqual(TEXT("Quest network layout requires protocol 14"), FBrSessionRules::Protocol, 14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCrateWaveTest, "BrGame.Crate.MapCountsAndWaveSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrCrateWaveTest::RunTest(const FString&)
{
	const FBrMapEntry Defaults;
	TestEqual(TEXT("Entry default initial crates"), Defaults.InitialCrates, 4);
	TestEqual(TEXT("Entry default interval crates"), Defaults.CratesPerDrop, 2);
	TestEqual(TEXT("Negative initial count disables initial wave"), FBrCrateRules::InitialWave(-2), 0);
	TestEqual(TEXT("Interval cannot be disabled by invalid config"), FBrCrateRules::IntervalWave(-2), 1);
	TestEqual(TEXT("Initial work bounded"), FBrCrateRules::InitialWave(MAX_int32), 100);
	TestEqual(TEXT("Interval work bounded"), FBrCrateRules::IntervalWave(MAX_int32), 100);
	struct FExpected { const TCHAR* Id; int32 Initial; int32 PerDrop; };
	for (const FExpected Expected : {FExpected{TEXT("Town"),6,3}, FExpected{TEXT("Battlefield"),2,1}, FExpected{TEXT("TestArena"),2,1}})
	{
		const FBrMapEntry* Entry = UBrDeveloperSettings::Get()->FindMap(Expected.Id);
		if (!TestNotNull(Expected.Id, Entry)) return false;
		TestEqual(*(Entry->Id.ToString() + TEXT(" initial")), Entry->InitialCrates, Expected.Initial);
		TestEqual(*(Entry->Id.ToString() + TEXT(" per interval")), Entry->CratesPerDrop, Expected.PerDrop);
	}
	const float Spacing = FBrCrateRules::WaveSpacing(500, 1000);
	TestEqual(TEXT("Spacing separates scatter discs"), Spacing, 2000.f);
	TestEqual(TEXT("Reversed scatter tuning still separates discs"), FBrCrateRules::WaveSpacing(1000, 500), Spacing);
	TestTrue(TEXT("Zero scatter still separates physical crates"), FBrCrateRules::WaveSpacing(0, 0) >= 170);
	TestFalse(TEXT("Vertical separation cannot bypass XY spacing"),
		FBrCrateRules::IsWaveSpaced(FVector(100,0,5000), {FVector::ZeroVector}, Spacing));
	TestTrue(TEXT("Exact spacing boundary allowed"),
		FBrCrateRules::IsWaveSpaced(FVector(2000,0,0), {FVector::ZeroVector}, Spacing));
	FRandomStream Random(1602);
	TArray<FVector> Accepted;
	int32 Rejected = 0;
	for (int32 I = 0; I < 1000; ++I)
	{
		const FVector Candidate = FBrCrateRules::LandingCandidate(Random, FVector::ZeroVector, 10000);
		if (FBrCrateRules::IsWaveSpaced(Candidate, Accepted, Spacing)) Accepted.Add(Candidate);
		else ++Rejected;
	}
	TestTrue(TEXT("Bounded candidate search accepts a wave and rejects crowding"), Accepted.Num() > 6 && Rejected > 100);
	for (int32 I = 0; I < Accepted.Num(); ++I)
		for (int32 J = I + 1; J < Accepted.Num(); ++J)
			if (FVector::Dist2D(Accepted[I], Accepted[J]) < Spacing)
			{ AddError(TEXT("Accepted wave contains overlapping scatter discs")); return false; }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCrateGroundRulesTest, "BrGame.Crate.GroundSurfacePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrCrateGroundRulesTest::RunTest(const FString&)
{
	for (int32 Degrees = 0; Degrees <= 90; ++Degrees)
	{
		const float NormalZ = FMath::Cos(FMath::DegreesToRadians(static_cast<float>(Degrees)));
		TestEqual(TEXT("Only walkable slopes on tagged static ground"),
			FBrPlayableGroundRules::AcceptSurface(NormalZ, true, true, true), NormalZ >= 0.7f);
		TestFalse(TEXT("Untagged roof/cover is never a floor"),
			FBrPlayableGroundRules::AcceptSurface(NormalZ, true, false, true));
		TestFalse(TEXT("Dynamic objects cannot supply ground, even if tagged"),
			FBrPlayableGroundRules::AcceptSurface(NormalZ, false, true, true));
	}
	TestFalse(TEXT("Invalid normals rejected"), FBrPlayableGroundRules::AcceptSurface(
		std::numeric_limits<float>::quiet_NaN(), true, true, true));
	TestTrue(TEXT("Legacy zone fallback keeps accepting untagged static floor"),
		FBrPlayableGroundRules::AcceptSurface(1, true, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCrateGroundTest, "BrGame.Crate.GroundWithoutNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrCrateGroundTest::RunTest(const FString&)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	if (!TestNotNull(TEXT("Collision-only world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	auto Box = [World](const FVector& Center, const FVector& Extent, bool bGround)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Component = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Component);
		Actor->AddInstanceComponent(Component);
		Component->SetBoxExtent(Extent);
		Component->SetCollisionProfileName(TEXT("BlockAll"));
		if (bGround) Component->ComponentTags.Add(TEXT("BrFogGround"));
		Component->RegisterComponent();
		Actor->SetActorLocation(Center);
		return Actor;
	};
	AActor* GroundActor = Box(FVector(0,0,-10), FVector(2000,2000,10), true);
	// Actual TestArena Floor is at rest but authored Movable/BlockAllDynamic.
	UBoxComponent* GroundComponent = CastChecked<UBoxComponent>(GroundActor->GetRootComponent());
	GroundComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Box(FVector(0,0,100), FVector(100,100,100), false); // cover
	Box(FVector(500,0,150), FVector(30,300,150), false); // wall
	Box(FVector(1000,0,400), FVector(200,200,20), false); // high roof
	// Match TestArena's fog-tagged SM_Cylinder: radius 350, cap at Z=210.
	Box(FVector(-500,1000,205), FVector(350,350,5), true);
	FBrGroundQuery Query;
	Query.Radius = 85;
	Query.bRequireTaggedStaticFloor = Query.bUsePawnProfile = true;
	Query.MinFloorHalfWidth = FBrCrateRules::FloorHalfWidth(500, 1000);
	FVector Floor;
	TestNull(TEXT("No nav required"), FNavigationSystem::GetCurrent<UNavigationSystemV1>(World));
	TestTrue(TEXT("Clear floor succeeds without navigation"),
		FBrPlayableGround::Find(World, FVector(-1000,0,0), Floor, Query));
	TestTrue(TEXT("Ground height from physics, not candidate Z"), FMath::Abs(Floor.Z) < 0.1);
	TestFalse(TEXT("Cover top rejected rather than tracing through to floor"),
		FBrPlayableGround::Find(World, FVector::ZeroVector, Floor, Query));
	TestFalse(TEXT("Roof rejected even with capsule space above it"),
		FBrPlayableGround::Find(World, FVector(1000,0,0), Floor, Query));
	TestFalse(TEXT("Capsule beside wall rejected"),
		FBrPlayableGround::Find(World, FVector(450,0,0), Floor, Query));
	TestFalse(TEXT("Missing ground rejected"),
		FBrPlayableGround::Find(World, FVector(3000,0,0), Floor, Query));
	TestFalse(TEXT("TestArena-sized tagged raised cap cannot supply crate/scatter ground"),
		FBrPlayableGround::Find(World, FVector(-500,1000,0), Floor, Query));
	FBrGroundQuery ZoneQuery;
	TestTrue(TEXT("Legacy zone fallback behavior remains unchanged on a walkable raised cap"),
		FBrPlayableGround::Find(World, FVector(-500,1000,0), Floor, ZoneQuery));
	TestTrue(TEXT("Legacy query really hit the raised cap"), FMath::Abs(Floor.Z - 210) < 0.1);
	AActor* ThinTaggedCap = Box(FVector(-500,-1000,205), FVector(2000,20,5), true);
	ThinTaggedCap->SetActorRotation(FRotator(0,45,0));
	TestFalse(TEXT("Rotated thin tagged prop cannot inflate world AABB into a broad floor"),
		FBrPlayableGround::Find(World, FVector(-500,-1000,0), Floor, Query));
	AActor* SideAuthoredFloor = Box(FVector(6000,0,-10), FVector(10,2000,2000), true);
	SideAuthoredFloor->SetActorRotation(FRotator(90,0,0));
	TestTrue(TEXT("Broad floor authored on its side remains eligible"),
		FBrPlayableGround::Find(World, FVector(6000,0,0), Floor, Query));
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCrateFootprintTest, "BrGame.Crate.RaisedTaggedFloorFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrCrateFootprintTest::RunTest(const FString&)
{
	const float Width = FBrCrateRules::FloorHalfWidth(500, 1000);
	TestEqual(TEXT("Scatter plus clearance radius determines floor width"), Width, 1085.f);
	TestEqual(TEXT("Reversed tuning still uses the maximum"), FBrCrateRules::FloorHalfWidth(1000, 500), Width);
	TestFalse(TEXT("Actual TestArena cylinder horizontal half extent is insufficient"),
		FBrPlayableGroundRules::SupportsFootprint(FVector2D(350,350), Width));
	for (float HalfWidth : {2000.f, 5000.f, 10000.f})
		TestTrue(TEXT("TestArena/Battlefield/Town broad floors remain eligible"),
			FBrPlayableGroundRules::SupportsFootprint(FVector2D(HalfWidth,HalfWidth), Width));
	TestFalse(TEXT("Large area but narrow width cannot support footprint"),
		FBrPlayableGroundRules::SupportsFootprint(FVector2D(10000, 50), Width));
	TestTrue(TEXT("Exact footprint boundary accepted"),
		FBrPlayableGroundRules::SupportsFootprint(FVector2D(Width, Width), Width));
	TestFalse(TEXT("One deficient axis rejected"),
		FBrPlayableGroundRules::SupportsFootprint(FVector2D(Width, Width - 1), Width));
	TestFalse(TEXT("Nonfinite dimensions cannot qualify"), FBrPlayableGroundRules::SupportsFootprint(
		FVector2D(std::numeric_limits<double>::quiet_NaN(), 2000), Width));
	TestFalse(TEXT("Nonfinite required size cannot qualify"), FBrPlayableGroundRules::SupportsFootprint(
		FVector2D(2000, 2000), std::numeric_limits<float>::infinity()));
	TestEqual(TEXT("Zero scatter still requires the crate clearance footprint"), FBrCrateRules::FloorHalfWidth(0,0), 85.f);
	TestTrue(TEXT("Local X can be the floor normal"),
		FBrPlayableGroundRules::SupportsSurfaceFootprint(FVector(2000,2000,10), FVector(10,2000,2000),
			FVector::ForwardVector, Width));
	TestFalse(TEXT("A tall thin prop cannot use its height as support width"),
		FBrPlayableGroundRules::SupportsSurfaceFootprint(FVector(1500,1500,2000), FVector(2000,20,2000),
			FVector::UpVector, Width));
	TestTrue(TEXT("At-rest template BlockAllDynamic floor can support ground"),
		FBrPlayableGroundRules::StableSupport(true, false, 0));
	TestFalse(TEXT("Moving support cannot supply crate ground"), FBrPlayableGroundRules::StableSupport(true, false, 100));
	TestFalse(TEXT("Physics objects cannot supply ground even at rest"), FBrPlayableGroundRules::StableSupport(true, true, 0));
	TestFalse(TEXT("Pawn collision type cannot supply ground"), FBrPlayableGroundRules::StableSupport(false, false, 0));
	return true;
}

/** Real collision, timers and round ownership, without a test-only nav rebuild. */
class FBrCrateLifecycle : public IAutomationLatentCommand
{
public:
	explicit FBrCrateLifecycle(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual bool Update() override
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.WorldType == EWorldType::PIE && Context.World() &&
				Context.World()->GetMapName().Contains(TEXT("L_Town"))) World = Context.World();
		if (FPlatformTime::Seconds() - Started > 90) { Test->AddError(TEXT("Crate lifecycle timed out")); return true; }
		if (!World) return false;
		ABrGameMode* Mode = World->GetAuthGameMode<ABrGameMode>();
		ABrPickupManager* Manager = Mode ? Mode->GetPickupManager() : nullptr;
		ABrGameState* GS = World->GetGameState<ABrGameState>();
		if (!GS || !Manager) return false;
		if (!bReset)
		{
			Manager->ClearRoundPickups();
			bReset = true;
			return false;
		}
		const float Now = World->GetTimeSeconds();
		const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
		if (Phase == 0)
		{
			SavedInterval = GS->MatchSettings.CrateDropInterval;
			GS->MatchSettings.CrateDropInterval = 6;
			Manager->RespawnAllPickups();
			Phase = 1; PhaseTime = Now;
			return false;
		}
		if (Phase == 1)
		{
			const FBrMapEntry* Town = S->FindMap(TEXT("Town"));
			if (!Town) { Test->AddError(TEXT("Town entry missing")); return true; }
			if (!bCheckedInitial && Now - PhaseTime >= 0.5f)
			{
				TArray<FVector> Landings;
				for (TActorIterator<ABrSupplyCrate> It(World); It; ++It)
					if (!It->IsActorBeingDestroyed())
					{
						Test->TestTrue(TEXT("Initial wave has spaced real landing positions"),
							FBrCrateRules::IsWaveSpaced(It->GetActorLocation(), Landings,
								FBrCrateRules::WaveSpacing(S->CrateScatterMin, S->CrateScatterMax)));
						Landings.Add(It->GetActorLocation());
					}
				Test->TestEqual(TEXT("Town initial wave uses per-map count"), Landings.Num(), Town->InitialCrates);
				bCheckedInitial = true;
			}
			int32 Landed = 0, Falling = 0, Items = 0;
			TArray<FVector> FallingLandings;
			for (TActorIterator<ABrSupplyCrate> It(World); It; ++It)
			{
				if (It->IsActorBeingDestroyed()) continue;
				if (It->IsLanded()) ++Landed;
				else { ++Falling; FallingLandings.Add(It->GetActorLocation()); }
			}
			for (TActorIterator<ABrPickup> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++Items;
			if (!Landed || !Items || Now - PhaseTime < 6.75f) return false;
			Test->TestEqual(TEXT("Town interval wave uses per-map count"), Falling, Town->CratesPerDrop);
			for (int32 I = 0; I < FallingLandings.Num(); ++I)
				for (int32 J = I + 1; J < FallingLandings.Num(); ++J)
					Test->TestTrue(TEXT("Real periodic wave has spaced landing positions"),
						FVector::Dist2D(FallingLandings[I], FallingLandings[J]) >=
							FBrCrateRules::WaveSpacing(S->CrateScatterMin, S->CrateScatterMax));
			Test->TestTrue(TEXT("Landing scatters real pickup actors"), Items >= 10);
			TSet<FGuid> OfferIds;
			for (TActorIterator<ABrSupplyCrate> Crate(World); Crate; ++Crate)
			{
				if (!Crate->IsLanded() || Crate->IsActorBeingDestroyed()) continue;
				int32 Offers = 0;
				for (TActorIterator<ABrPickup> Pickup(World); Pickup; ++Pickup)
				{
					if (Pickup->IsActorBeingDestroyed()) continue;
					const FBrQuestOffer* Offer = Pickup->GetQuestOffer();
					if (!Offer || FVector::DistSquared2D(Pickup->GetActorLocation(), Crate->GetActorLocation()) >
						FMath::Square(FMath::Max(S->CrateScatterMin, S->CrateScatterMax))) continue;
					++Offers;
					Test->TestTrue(TEXT("Landed offer payload valid"), FBrQuestRules::Valid(*Offer));
					Test->TestFalse(TEXT("Every token has a distinct identity"), OfferIds.Contains(Offer->Id));
					OfferIds.Add(Offer->Id);
					Test->TestTrue(TEXT("Quest tokens share crate loot expiry"), Pickup->GetLifeSpan() > S->CrateItemLifetime - 10 &&
						Pickup->GetLifeSpan() <= S->CrateItemLifetime);
				}
				Test->TestTrue(TEXT("Every successfully landed crate has 1 or 2 separate tokens"), Offers >= 1 && Offers <= 2);
			}
			for (TActorIterator<ABrSupplyCrate> It(World); It; ++It)
			{
				if (!It->IsLanded() || It->IsActorBeingDestroyed()) continue;
				Test->TestTrue(TEXT("Landed crate cover has configured remaining lifetime"), It->GetLifeSpan() > 0 && It->GetLifeSpan() <= S->CrateCoverLifetime);
				Test->TestTrue(TEXT("Crate public replication"), It->GetIsReplicated() && It->bAlwaysRelevant);
				for (TActorIterator<ABrSafeZone> Zone(World); Zone; ++Zone)
					Test->TestTrue(TEXT("Actual crate landed inside safe zone and bounds"),
						FBrCrateRules::Contains(It->GetActorLocation(), Zone->Circle().Center, Zone->Circle().Radius,
							ABrZoneBounds::FindPlayableBounds(World)));
				ExpiringCrate = *It;
				It->SetLifeSpan(0.2f);
				break;
			}
			for (TActorIterator<ABrPickup> It(World); It; ++It)
			{
				if (It->IsActorBeingDestroyed()) continue;
				Test->TestTrue(TEXT("Contents have configured item lifetime"), It->GetLifeSpan() > S->CrateItemLifetime - 10 &&
					It->GetLifeSpan() <= S->CrateItemLifetime);
				// Explicitly exercise a quest token's timer, not just ordinary loot.
				if (It->GetQuestOffer())
				{
					ExpiringItem = *It;
					It->SetLifeSpan(0.2f);
					break;
				}
			}
			Phase = 2; PhaseTime = Now;
			return false;
		}
		if (Phase == 2 && Now - PhaseTime > 0.5f)
		{
			Test->TestFalse(TEXT("Uncollected content expires via server lifetime timer"), ExpiringItem.IsValid());
			Test->TestFalse(TEXT("Landed cover expires via server lifetime timer"), ExpiringCrate.IsValid());
			Manager->ClearRoundPickups();
			Phase = 3; PhaseTime = Now;
			return false;
		}
		if (Phase == 3 && Now - PhaseTime > FMath::Max(6.f, S->CrateFallDuration) + 1.5f)
		{
			int32 Remaining = 0;
			for (TActorIterator<ABrSupplyCrate> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++Remaining;
			for (TActorIterator<ABrPickup> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++Remaining;
			Test->TestEqual(TEXT("Reset clears crates, contents and pending landing/periodic timers"), Remaining, 0);
			GS->MatchSettings.CrateDropInterval = SavedInterval;
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	double Started = FPlatformTime::Seconds();
	float PhaseTime = 0, SavedInterval = 30;
	int32 Phase = 0;
	bool bReset = false;
	bool bCheckedInitial = false;
	TWeakObjectPtr<ABrPickup> ExpiringItem;
	TWeakObjectPtr<ABrSupplyCrate> ExpiringCrate;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCrateLifecycleTest, "BrGame.Crate.RuntimeLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrCrateLifecycleTest::RunTest(const FString&)
{
	if (!AutomationOpenMap(TEXT("/Game/BrGame/Maps/L_Town"))) return false;
	ADD_LATENT_AUTOMATION_COMMAND(FBrCrateLifecycle(this));
	return true;
}
#endif
