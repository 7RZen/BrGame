#include "BrRoundRecord.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrReconnectHistoryTest, "BrGame.Life.ReconnectBookkeeping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrReconnectHistoryTest::RunTest(const FString& Parameters)
{
	FBrLifeBookkeeping Retained;
	Retained.Deaths = 7;
	Retained.VoluntaryRespawns = 3;
	Retained.bEliminated = true;
	const FBrLifeBookkeeping Reactivated = FBrReconnectRules::Merge(Retained, {});
	TestEqual(TEXT("Fresh login cannot erase deaths on inactive PS"), Reactivated.Deaths, 7);
	TestEqual(TEXT("Fresh login cannot erase penalties"), Reactivated.VoluntaryRespawns, 3);
	TestTrue(TEXT("Elimination survives fresh defaults"), Reactivated.bEliminated);
	const FBrLifeBookkeeping Reverse = FBrReconnectRules::Merge({}, Retained);
	TestEqual(TEXT("New PS can also inherit old history"), Reverse.Deaths, 7);
	TestEqual(TEXT("Inherited counts preserve HP penalty"),
		FBrLifeRules::MaxHealth(90, Reverse.VoluntaryRespawns, 0.2f, 0.1f),
		FBrLifeRules::MaxHealth(90, Retained.VoluntaryRespawns, 0.2f, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrReconnectTimeoutTest, "BrGame.Life.ReconnectDownedTimeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrReconnectTimeoutTest::RunTest(const FString& Parameters)
{
	FBrLifeBookkeeping Life;
	Life.Deaths = 2;
	Life.VoluntaryRespawns = 1;
	EBrLifeState State = EBrLifeState::Downed;
	TestFalse(TEXT("Rejoining before deadline must stay downed"),
		FBrReconnectRules::ResolveOfflineTimeout(Life, State, 30, 29, 3, true));
	TestTrue(TEXT("Original deadline triggers timeout"), FBrReconnectRules::ResolveOfflineTimeout(Life, State, 30, 30, 3, true));
	TestTrue(TEXT("Respawns remaining returns Alive"), State == EBrLifeState::Alive);
	TestEqual(TEXT("Counts timeout death"), Life.Deaths, 3);
	TestEqual(TEXT("Timeout adds no voluntary penalty"), Life.VoluntaryRespawns, 1);
	TestFalse(TEXT("Repeated login cannot charge the timeout twice"),
		FBrReconnectRules::ResolveOfflineTimeout(Life, State, 30, 100, 3, true));
	State = EBrLifeState::Downed;
	TestTrue(TEXT("Expired dying at death limit is resolved"), FBrReconnectRules::ResolveOfflineTimeout(Life, State, 100, 101, 3, true));
	TestTrue(TEXT("No further lives from reconnect"), Life.bEliminated && State == EBrLifeState::Eliminated);
	FBrLifeBookkeeping OptionalCount;
	State = EBrLifeState::Downed;
	FBrReconnectRules::ResolveOfflineTimeout(OptionalCount, State, 0, 1, 3, false);
	TestEqual(TEXT("Developer timeout counting switch honored"), OptionalCount.Deaths, 0);
	return true;
}
#endif
