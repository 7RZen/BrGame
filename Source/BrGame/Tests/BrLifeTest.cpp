#include "BrLifeRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrLifeExitOrderTest, "BrGame.Life.DownedExitDeadlineOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrLifeExitOrderTest::RunTest(const FString&)
{
	const auto Late = FBrLifeRules::SelectDownedExit(25, true, 20, true, 10);
	TestTrue(TEXT("Late service crosses all exit deadlines"), Late.IsDue(30));
	TestEqual(TEXT("Earliest voluntary wins over later completed revive/timeout"), Late.Kind, EBrDownedExit::VoluntaryRespawn);
	TestEqual(TEXT("Chosen cutoff is voluntary deadline"), Late.Deadline, 10.f);
	TestEqual(TEXT("Revive wins three-way tie"), FBrLifeRules::SelectDownedExit(10, true, 10, true, 10).Kind, EBrDownedExit::Revive);
	TestEqual(TEXT("Timeout wins tie with voluntary"), FBrLifeRules::SelectDownedExit(10, false, 0, true, 10).Kind, EBrDownedExit::Timeout);
	TestEqual(TEXT("Invalid voluntary ignored"), FBrLifeRules::SelectDownedExit(25, true, 20, false, 10).Kind, EBrDownedExit::Revive);
	TestEqual(TEXT("Invalid revive ignored"), FBrLifeRules::SelectDownedExit(25, false, 5, true, 10).Kind, EBrDownedExit::VoluntaryRespawn);
	TestFalse(TEXT("No early transition"), Late.IsDue(9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrLifePenaltyTest, "BrGame.Life.MaxHealthPenalty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrLifePenaltyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Initial health uses configurable base"), FBrLifeRules::MaxHealth(80, 0, 0.15f, 0.2f), 80.0f);
	TestEqual(TEXT("Penalty is cumulative, not multiplicative"), FBrLifeRules::MaxHealth(80, 2, 0.15f, 0.2f), 56.0f);
	TestEqual(TEXT("Many respawns clamp to floor"), FBrLifeRules::MaxHealth(80, 100, 0.15f, 0.2f), 16.0f);
	const float ExpectedByDeath[] = {100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 10};
	for (int32 Deaths = 0; Deaths < UE_ARRAY_COUNT(ExpectedByDeath); ++Deaths)
	{
		TestEqual(FString::Printf(TEXT("Default 10%% penalty at voluntary death %d"), Deaths),
			FBrLifeRules::MaxHealth(100, Deaths, 0.10f, 0.10f), ExpectedByDeath[Deaths]);
	}
	TestTrue(TEXT("At the tenth-death limit voluntary choice is refused"),
		FBrLifeRules::Outcome(true, 10, 10) == EBrDownedOutcome::Refused);
	TestTrue(TEXT("At the tenth-death limit timeout eliminates"),
		FBrLifeRules::Outcome(false, 10, 10) == EBrDownedOutcome::Eliminated);
	TestEqual(TEXT("Negative counts never grant bonus HP"), FBrLifeRules::MaxHealth(80, -1, 0.15f, 0.2f), 80.0f);
	TestEqual(TEXT("Zero penalty preserves base"), FBrLifeRules::MaxHealth(80, 5, 0, 0.2f), 80.0f);
	TestEqual(TEXT("Base capped at 100"), FBrLifeRules::MaxHealth(200, 0, 0.1f, 0.1f), 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrLifeOutcomeTest, "BrGame.Life.DownedOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrLifeOutcomeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Voluntary respawn has penalty"), FBrLifeRules::Outcome(true, 2, 3) == EBrDownedOutcome::VoluntaryRespawn);
	TestTrue(TEXT("Timeout respawn has no penalty"), FBrLifeRules::Outcome(false, 2, 3) == EBrDownedOutcome::TimeoutRespawn);
	TestTrue(TEXT("Voluntary at limit is refused, not eliminated"), FBrLifeRules::Outcome(true, 3, 3) == EBrDownedOutcome::Refused);
	TestTrue(TEXT("Timeout at limit eliminates"), FBrLifeRules::Outcome(false, 3, 3) == EBrDownedOutcome::Eliminated);
	TestTrue(TEXT("Over limit is also eliminated"), FBrLifeRules::Outcome(false, 9, 3) == EBrDownedOutcome::Eliminated);
	TestTrue(TEXT("No respawns configured"), FBrLifeRules::Outcome(true, 0, 0) == EBrDownedOutcome::Refused);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrLifeSpawnTest, "BrGame.Life.SpawnScoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrLifeSpawnTest::RunTest(const FString& Parameters)
{
	TArray<FVector> Candidates = {FVector(10, 0, 0), FVector(100, 0, 0), FVector(300, 0, 0)};
	TArray<FBrSpawnThreat> Threats = {{FVector::ZeroVector, false, true, true}};
	TestEqual(TEXT("Farthest eligible point preferred"), FBrLifeRules::PickSpawn(Candidates, Threats, 200), 2);
	TestEqual(TEXT("If all too close choose best rather than fail"), FBrLifeRules::PickSpawn(Candidates, Threats, 500), 2);
	TestEqual(TEXT("Minimum distance measures nearest of multiple enemies"), FBrLifeRules::SpawnDistanceSquared(
		FVector(100, 0, 0), {{FVector(0, 0, 0), false, true, true}, {FVector(120, 0, 0), false, true, true}}), 400.0);
	Threats.Add({FVector(300, 0, 0), true, true, true});
	Threats.Add({FVector(300, 0, 0), false, false, true});
	Threats.Add({FVector(300, 0, 0), false, true, false});
	TestEqual(TEXT("Ignore self, non-enemies and eliminated"), FBrLifeRules::PickSpawn(Candidates, Threats, 200), 2);
	TestEqual(TEXT("No candidates fails explicitly"), FBrLifeRules::PickSpawn({}, Threats, 200), INDEX_NONE);
	TestEqual(TEXT("No enemies selects a valid point"), FBrLifeRules::PickSpawn(Candidates, {}, 200), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrLifeReviveTest, "BrGame.Life.ReviveValidity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrLifeReviveTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Anyone in range"), FBrLifeRules::CanRevive(EBrLifeState::Alive, EBrLifeState::Downed, 100, 11, EBrReviveRule::Anyone, false, false));
	TestFalse(TEXT("Out of range"), FBrLifeRules::CanRevive(EBrLifeState::Alive, EBrLifeState::Downed, 144, 11, EBrReviveRule::Anyone, true, false));
	TestFalse(TEXT("Cannot self revive"), FBrLifeRules::CanRevive(EBrLifeState::Alive, EBrLifeState::Downed, 0, 11, EBrReviveRule::Anyone, true, true));
	TestFalse(TEXT("Downed reviver refused"), FBrLifeRules::CanRevive(EBrLifeState::Downed, EBrLifeState::Downed, 0, 11, EBrReviveRule::Anyone, true, false));
	TestFalse(TEXT("Dead target refused"), FBrLifeRules::CanRevive(EBrLifeState::Alive, EBrLifeState::Eliminated, 0, 11, EBrReviveRule::Anyone, true, false));
	TestFalse(TEXT("Alive target refused"), FBrLifeRules::CanRevive(EBrLifeState::Alive, EBrLifeState::Alive, 0, 11, EBrReviveRule::Anyone, true, false));
	TestFalse(TEXT("Team rule rejects enemy"), FBrLifeRules::CanRevive(EBrLifeState::Alive, EBrLifeState::Downed, 0, 11, EBrReviveRule::TeammatesOnly, false, false));
	TestTrue(TEXT("Team rule accepts teammate at boundary"), FBrLifeRules::CanRevive(EBrLifeState::Alive, EBrLifeState::Downed, 121, 11, EBrReviveRule::TeammatesOnly, true, false));
	return true;
}
#endif
