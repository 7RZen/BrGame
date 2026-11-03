#include "BrCombatMath.h"
#include "BrInventoryComponent.h"
#include "BrDevCheatPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrDevCheatAuthorizationTest, "BrGame.Combat.DevCheatAuthorization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrDevCheatAuthorizationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Listen host can test locally"), FBrDevCheatPolicy::CanExecute(true, true, NM_ListenServer));
	TestTrue(TEXT("Standalone remains usable"), FBrDevCheatPolicy::CanExecute(true, true, NM_Standalone));
	TestFalse(TEXT("Remote owning client cannot cheat on listen server"), FBrDevCheatPolicy::CanExecute(true, false, NM_ListenServer));
	TestFalse(TEXT("Client-local controller cannot authorize itself"), FBrDevCheatPolicy::CanExecute(false, true, NM_Client));
	TestFalse(TEXT("Dedicated servers never grant this host-only permission"), FBrDevCheatPolicy::CanExecute(true, true, NM_DedicatedServer));
	TestFalse(TEXT("Authority is required even for local controller"), FBrDevCheatPolicy::CanExecute(false, true, NM_ListenServer));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrHitChanceTest, "BrGame.Combat.HitChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrHitChanceTest::RunTest(const FString& Parameters)
{
	FBrHitChanceRow Rows[3];
	const EBrPosture Postures[] = {EBrPosture::Standing, EBrPosture::Crouching, EBrPosture::Prone};
	for (int32 Row = 0; Row < 3; ++Row)
	{
		Rows[Row].Standing = Row * 20 + 7;
		Rows[Row].Crouching = Row * 20 + 11;
		Rows[Row].Prone = Row * 20 + 17;
	}
	const float Columns[] = {7, 11, 17};
	for (int32 Shooter = 0; Shooter < 3; ++Shooter)
	{
		for (int32 Target = 0; Target < 3; ++Target)
		{
			TestEqual(TEXT("Non-symmetric shooter-row/target-column lookup"),
				FBrCombatMath::HitChance(Postures[Shooter], Postures[Target], Rows[0], Rows[1], Rows[2]),
				Shooter * 20 + Columns[Target]);
		}
	}
	// Movement's gait only changes speed; the same posture must be supplied to combat.
	for (bool bSprinting : {false, true})
	{
		const EBrPosture Posture = EBrPosture::Standing;
		TestTrue(TEXT("Gait changes speed"), FBrMovementSpeed::Calculate(Posture, bSprinting, 0.9f, 600, 1.3f, 0.2f, 0.2f) > 0);
		TestEqual(TEXT("Sprint/run use standing row"), FBrCombatMath::HitChance(Posture, EBrPosture::Crouching,
			Rows[0], Rows[1], Rows[2]), 11.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrDamageTest, "BrGame.Combat.Damage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrDamageTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Bullet uses custom target crouch multiplier"),
		FBrCombatMath::Damage(37, EBrPosture::Crouching, false, false, 0.9f, 0.4f, 0.2f), 37 * 0.4f);
	TestEqual(TEXT("Grenade enabled uses custom prone multiplier"),
		FBrCombatMath::Damage(81, EBrPosture::Prone, true, true, 0.9f, 0.4f, 0.2f), 81 * 0.2f);
	TestEqual(TEXT("Grenade disabled ignores posture"),
		FBrCombatMath::Damage(81, EBrPosture::Prone, true, false, 0.9f, 0.4f, 0.2f), 81.0f);
	TestEqual(TEXT("Standing damage is developer configurable"),
		FBrCombatMath::Damage(13, EBrPosture::Standing, false, true, 0.9f, 0.4f, 0.2f), 13 * 0.9f);
	TestEqual(TEXT("Negative damage cannot heal"),
		FBrCombatMath::Damage(-7, EBrPosture::Standing, false, true, 1, 1, 1), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAmmoTest, "BrGame.Combat.AmmoReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrAmmoTest::RunTest(const FString& Parameters)
{
	UBrAmmoDefinition* Standard = NewObject<UBrAmmoDefinition>();
	UBrAmmoDefinition* Other = NewObject<UBrAmmoDefinition>();
	TArray<FBrAmmoPool> Pools;
	const int32 MagSize = 7;
	const int32 Magazines = 3;
	FBrAmmoAccounting::Add(Pools, Standard, FBrCombatMath::GrantReserve(MagSize, Magazines));
	TestEqual(TEXT("Loaded plus reserve equals full grant"), MagSize + FBrAmmoAccounting::Reserve(Pools, Standard), MagSize * Magazines);
	FBrAmmoAccounting::Add(Pools, Standard, FBrCombatMath::GrantReserve(11, 2));
	TestEqual(TEXT("Different weapon with same ammo adds to shared pool"), FBrAmmoAccounting::Reserve(Pools, Standard), 25);
	TestEqual(TEXT("One shared pool, no duplicates"), Pools.Num(), 1);
	FBrAmmoAccounting::Add(Pools, Other, 9);
	TestEqual(TEXT("Future ammo type stays separate"), FBrAmmoAccounting::Reserve(Pools, Other), 9);
	TestEqual(TEXT("Standard pool unaffected"), FBrAmmoAccounting::Reserve(Pools, Standard), 25);
	TestEqual(TEXT("Partial reload"), FBrCombatMath::ReloadRounds(4, 11, 3), 3);
	TestEqual(TEXT("Full reload consumes only missing rounds"), FBrCombatMath::ReloadRounds(4, 11, 25), 7);
	TestEqual(TEXT("Full mag refuses reload"), FBrCombatMath::ReloadRounds(11, 11, 25), 0);
	TestEqual(TEXT("Empty pool refuses reload"), FBrCombatMath::ReloadRounds(4, 11, 0), 0);
	TestEqual(TEXT("One starting magazine has no spare"), FBrCombatMath::GrantReserve(19, 1), 0);
	FBrAmmoAccounting::Add(Pools, Standard, MAX_int32);
	TestEqual(TEXT("Grants saturate without overflow"), FBrAmmoAccounting::Reserve(Pools, Standard), MAX_int32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFireRateTest, "BrGame.Combat.FireRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFireRateTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Initial shot allowed"), FBrCombatMath::CanFire(0, 0));
	const double Next = FBrCombatMath::NextFireTime(10, 0, 8);
	TestFalse(TEXT("Too early rejected"), FBrCombatMath::CanFire(10.1, Next));
	TestTrue(TEXT("Small jitter tolerated"), FBrCombatMath::CanFire(Next - 0.003, Next));
	TestTrue(TEXT("Deadline accepted"), FBrCombatMath::CanFire(Next, Next));
	TestEqual(TEXT("Early tolerance never accumulates faster sustained cadence"),
		FBrCombatMath::NextFireTime(Next - 0.003, Next, 8), Next + 0.125);
	TestEqual(TEXT("Late shot doesn't bank a burst"), FBrCombatMath::NextFireTime(20, Next, 8), 20.125);
	return true;
}
#endif
