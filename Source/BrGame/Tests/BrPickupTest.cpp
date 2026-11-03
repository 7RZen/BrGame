#include "BrPickupRules.h"
#include "BrMatchSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupWeightedTest, "BrGame.Pickups.WeightedSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupWeightedTest::RunTest(const FString& Parameters)
{
	FRandomStream A(49271), B(49271);
	const TArray<float> Weights = {0, 2, 6, 0};
	int32 Counts[4] = {};
	for (int32 Index = 0; Index < 10000; ++Index)
	{
		const int32 Chosen = FBrPickupRules::WeightedIndex(Weights, A);
		if (Chosen < 0 || Chosen >= 4) { AddError(TEXT("Invalid weighted index")); return false; }
		++Counts[Chosen];
		if (Chosen != FBrPickupRules::WeightedIndex(Weights, B)) { AddError(TEXT("Seeded stream not deterministic")); return false; }
	}
	TestEqual(TEXT("Zero weight at beginning never chosen"), Counts[0], 0);
	TestEqual(TEXT("Zero weight at end never chosen"), Counts[3], 0);
	TestTrue(TEXT("Relative weights produce sane 1:3 distribution"), Counts[1] > 2300 && Counts[1] < 2700);
	TestEqual(TEXT("No positive weights fails explicitly"), FBrPickupRules::WeightedIndex({0, -2, 0}, A), INDEX_NONE);
	TestEqual(TEXT("Single positive entry always selected"), FBrPickupRules::WeightedIndex({0, 0, 7}, A), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupApplicationTest, "BrGame.Pickups.ApplicationRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupApplicationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Health fraction respects penalized maximum"), FBrPickupRules::HealthGrant(10, 60, 0.5f), 30.0f);
	TestEqual(TEXT("Health clamps near maximum"), FBrPickupRules::HealthGrant(55, 60, 0.5f), 5.0f);
	TestEqual(TEXT("Full health refuses"), FBrPickupRules::HealthGrant(60, 60, 1), 0.0f);
	TestEqual(TEXT("Grenade below cap grants one"), FBrPickupRules::GrenadeGrant(2, 3), 1);
	TestEqual(TEXT("Grenade at cap refuses"), FBrPickupRules::GrenadeGrant(3, 3), 0);
	TestEqual(TEXT("Ammo uses current magazine size"), FBrPickupRules::AmmoGrant(17, 32, 0), 17);
	TestEqual(TEXT("Ammo uses a different current weapon's magazine size"), FBrPickupRules::AmmoGrant(43, 32, 0), 43);
	TestEqual(TEXT("Reserve cap allows partial magazine"), FBrPickupRules::AmmoGrant(17, 32, 40), 8);
	TestEqual(TEXT("At cap refuses"), FBrPickupRules::AmmoGrant(17, 40, 40), 0);
	TestEqual(TEXT("Above cap also refuses"), FBrPickupRules::AmmoGrant(17, 50, 40), 0);
	TestEqual(TEXT("Unlimited arithmetic doesn't overflow"), FBrPickupRules::AmmoGrant(17, MAX_int32 - 2, 0), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupWeaponTest, "BrGame.Pickups.WeaponPayloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupWeaponTest::RunTest(const FString& Parameters)
{
	const FBrWeaponPickupPlan Fresh = FBrPickupRules::WeaponPlan(true, false, 5, 17, 3, false, 0);
	TestTrue(TEXT("Different weapon drops previous instance"), Fresh.bDropOldWeapon);
	TestEqual(TEXT("Drop contains only loaded rounds"), Fresh.DropLoaded, 5);
	TestEqual(TEXT("Fresh weapon loads one magazine"), Fresh.LoadedAfter, 17);
	TestEqual(TEXT("Loaded plus spare equals complete starting grant"), Fresh.LoadedAfter + Fresh.ReserveGrant, 51);
	const FBrWeaponPickupPlan Dropped = FBrPickupRules::WeaponPlan(true, false, 11, 17, 3, true, 5);
	TestEqual(TEXT("Dropped weapon restores partial magazine"), Dropped.LoadedAfter, 5);
	TestEqual(TEXT("Dropped weapon grants no new spare"), Dropped.ReserveGrant, 0);
	TestEqual(TEXT("Swapping dropped instances conserves represented loaded rounds"),
		Dropped.LoadedAfter + Dropped.DropLoaded + Dropped.ReserveGrant, 16);
	const FBrWeaponPickupPlan SameFresh = FBrPickupRules::WeaponPlan(true, true, 4, 17, 3, false, 0);
	TestFalse(TEXT("Same type creates no drop"), SameFresh.bDropOldWeapon);
	TestEqual(TEXT("Same type never refills current loaded rounds"), SameFresh.LoadedAfter, 4);
	TestEqual(TEXT("Fresh same type contributes all represented ammo once"), SameFresh.ReserveGrant, 51);
	const FBrWeaponPickupPlan SameDrop = FBrPickupRules::WeaponPlan(true, true, 4, 17, 3, true, 5);
	TestEqual(TEXT("Same dropped type contributes loaded payload only"), SameDrop.ReserveGrant, 5);
	TestEqual(TEXT("Empty dropped same type is not an ammo source"),
		FBrPickupRules::WeaponPlan(true, true, 4, 17, 3, true, 0).ReserveGrant, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupCountTest, "BrGame.Pickups.CountAndHostBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupCountTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No players retains base"), FBrPickupRules::Count(7, 3, 0), 7);
	TestEqual(TEXT("Count scales without imposing player cap"), FBrPickupRules::Count(7, 3, 1000), 3007);
	TestEqual(TEXT("Disabled generation"), FBrPickupRules::Count(0, 0, 1000), 0);
	TestEqual(TEXT("Bad negative config doesn't subtract"), FBrPickupRules::Count(-5, 3, 2), 6);
	TestEqual(TEXT("Large values saturate safely"), FBrPickupRules::Count(1, MAX_int32, 2), MAX_int32);
	TestEqual(TEXT("Host cannot select immediate respawn"), FBrMatchSettings::ClampPickupRespawnTime(-9), 1.0f);
	TestEqual(TEXT("Host upper duration bound"), FBrMatchSettings::ClampPickupRespawnTime(10000), 3600.0f);
	TestEqual(TEXT("Valid host value preserved"), FBrMatchSettings::ClampPickupRespawnTime(12.5f), 12.5f);
	return true;
}
#endif
