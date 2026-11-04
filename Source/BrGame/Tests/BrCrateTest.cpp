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
	TestEqual(TEXT("One periodic drop after each interval, independent of initial wave"), Time, 1373.5);
	TestEqual(TEXT("Network layout requires protocol 13"), FBrSessionRules::Protocol, 13);
	return true;
}

/** Real nav, timers, collision and round ownership, not a second model of the manager. */
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
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!GS || !Manager || !Nav) return false;
		if (!bNavRequested)
		{
			Manager->ClearRoundPickups();
			Nav->RemoveNavigationBuildLock(ENavigationBuildLock::NoUpdateInPIE, UNavigationSystemV1::ELockRemovalRebuildAction::Rebuild);
			Nav->Build(); bNavRequested = true;
			return false;
		}
		if (UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World)) return false;
		const float Now = World->GetTimeSeconds();
		const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
		if (Phase == 0)
		{
			SavedInterval = GS->MatchSettings.CrateDropInterval;
			GS->MatchSettings.CrateDropInterval = 1;
			Manager->RespawnAllPickups();
			Phase = 1; PhaseTime = Now;
			return false;
		}
		if (Phase == 1)
		{
			int32 Landed = 0, Falling = 0, Items = 0;
			for (TActorIterator<ABrSupplyCrate> It(World); It; ++It)
			{
				if (It->IsActorBeingDestroyed()) continue;
				if (It->IsLanded()) ++Landed; else ++Falling;
			}
			for (TActorIterator<ABrPickup> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++Items;
			if (!Landed || !Items || Now - PhaseTime < S->CrateFallDuration + 1.5f) return false;
			Test->TestTrue(TEXT("Periodic crates keep falling after initial wave lands"), Falling > 0);
			Test->TestTrue(TEXT("Landing scatters real pickup actors"), Items >= 10);
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
				ExpiringItem = *It;
				It->SetLifeSpan(0.2f);
				break;
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
		if (Phase == 3 && Now - PhaseTime > S->CrateFallDuration + 1.5f)
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
	bool bNavRequested = false;
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
