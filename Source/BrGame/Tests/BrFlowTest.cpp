#include "BrFlowRules.h"
#include "BrRoundRecord.h"
#include "BrDamageSource.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFlowEvenTest, "BrGame.Flow.AutoEven",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFlowEvenTest::RunTest(const FString& Parameters)
{
	const TArray<int32> Balanced = {0, 0, 1};
	TestTrue(TEXT("Balanced teams survive reversed seamless-arrival order"),
	         FBrFlowRules::Rebalance(Balanced, 2) == Balanced);
	const TArray<int32> Uneven = {0, 0, 0, 0, 0, 1, 1};
	const TArray<int32> Rebalanced = FBrFlowRules::Rebalance(Uneven, 3);
	int32 Moves = 0;
	for (int32 I = 0; I < Uneven.Num(); ++I)
	{
		if (Uneven[I] != Rebalanced[I])
		{
			++Moves;
		}
	}
	TestEqual(TEXT("Rebalancing moves only necessary members"), Moves, 2);
	for (const FIntPoint Scenario : {FIntPoint(12, 4), FIntPoint(7, 3), FIntPoint(2, 5), FIntPoint(0, 3)})
	{
		TArray<int32> Sizes;
		Sizes.Init(0, Scenario.Y);
		for (int32 Team : FBrFlowRules::AssignEven(Scenario.X, Scenario.Y))
		{
			++Sizes[Team];
		}
		int32 Min = MAX_int32, Max = 0, Total = 0;
		for (int32 Size : Sizes)
		{
			Min = FMath::Min(Min, Size);
			Max = FMath::Max(Max, Size);
			Total += Size;
		}
		TestEqual(TEXT("All players assigned"), Total, Scenario.X);
		TestTrue(TEXT("Rebalanced groups differ by at most one"), Max - Min <= 1);
		TestEqual(TEXT("Late join picks minimum size"), Sizes[FBrFlowRules::Smallest(Sizes)], Min);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFlowCapTest, "BrGame.Flow.AdmissionAndChoiceCap",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFlowCapTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Unlimited admits large count"), FBrFlowRules::Admit(100000, 0));
	TestTrue(TEXT("Last slot admitted"), FBrFlowRules::Admit(6, 7));
	TestFalse(TEXT("At maximum denied"), FBrFlowRules::Admit(7, 7));
	TestTrue(TEXT("Unlimited choice"), FBrFlowRules::HasRoom(10000, 0, 3));
	TestTrue(TEXT("Ceiling cap permits third member"), FBrFlowRules::HasRoom(2, 7, 3));
	TestFalse(TEXT("Ceiling cap refuses fourth member"), FBrFlowRules::HasRoom(3, 7, 3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFlowColorTest, "BrGame.Flow.Colors",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFlowColorTest::RunTest(const FString& Parameters)
{
	TArray<int32> Used = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
	TestTrue(TEXT("Admitted first controller may choose"), FBrFlowRules::CanChooseColor(true, true));
	TestFalse(
		TEXT("A different controller, even with the same identity, cannot choose"),
		FBrFlowRules::CanChooseColor(false, true));
	TestFalse(TEXT("Leaving first entrant loses permission"), FBrFlowRules::CanChooseColor(true, false));
	Used[0] = FBrFlowRules::AllocateColor(Used, 4);
	TestEqual(TEXT("First entrant choice"), Used[0], 4);
	Used[1] = FBrFlowRules::AllocateColor(Used, 4);
	TestTrue(TEXT("Duplicate request falls back to free color"), Used[1] != Used[0]);
	Used[2] = FBrFlowRules::AllocateColor(Used);
	TestTrue(TEXT("Fallback unique"), Used[2] != Used[0] && Used[2] != Used[1]);
	const int32 Kept = Used[0];
	FBrFlowRules::ReleaseEmpty(Used, {1, 0, 2});
	TestEqual(TEXT("Nonempty keeps first entrant color"), Used[0], Kept);
	TestEqual(TEXT("Empty group frees color"), Used[1], INDEX_NONE);
	TestEqual(TEXT("Freed first palette color reusable"), FBrFlowRules::AllocateColor(Used), 0);
	for (int32 I = 0; I < 1024; ++I)
	{
		TestFalse(TEXT("Military green excluded"), FBrFlowRules::Palette(I).Equals(FBrFlowRules::MilitaryGreen()));
		for (int32 J = 0; J < I; ++J)
		{
			TestFalse(TEXT("Palette colors distinct"), FBrFlowRules::Palette(I).Equals(FBrFlowRules::Palette(J)));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFlowWinTest, "BrGame.Flow.WinEvaluation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFlowWinTest::RunTest(const FString& Parameters)
{
	int32 Winner;
	TestFalse(TEXT("Two FFA contestants remain"), FBrFlowRules::Evaluate({10, 11}, 2, false, Winner));
	TestTrue(TEXT("Last FFA player wins"), FBrFlowRules::Evaluate({11}, 2, false, Winner));
	TestEqual(TEXT("Correct winner"), Winner, 11);
	TestFalse(TEXT("Downed member keeps second group in"), FBrFlowRules::Evaluate({0, 0, 1}, 2, false, Winner));
	TestTrue(TEXT("Multiple members of last group win"), FBrFlowRules::Evaluate({1, 1}, 2, false, Winner));
	TestEqual(TEXT("Group identity"), Winner, 1);
	TestTrue(TEXT("Simultaneous elimination draws"), FBrFlowRules::Evaluate({}, 2, false, Winner));
	TestEqual(TEXT("Draw has no winner"), Winner, INDEX_NONE);
	TestFalse(TEXT("Solo practice remains running"), FBrFlowRules::Evaluate({0}, 1, false, Winner));
	TestTrue(TEXT("Developer can allow solo finish"), FBrFlowRules::Evaluate({0}, 1, true, Winner));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFlowAnchorTest, "BrGame.Flow.GroupSpawnScoring",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFlowAnchorTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> Candidates = {FVector(200, 0, 0), FVector(3000, 0, 0), FVector(6000, 0, 0)};
	TArray<FBrSpawnThreat> Threats = {{FVector(0, 0, 0), false, true, true}};
	const int32 Anchor = FBrLifeRules::PickSpawn(Candidates, Threats, 2000);
	TestTrue(TEXT("Second group anchor has requested enemy separation"),
	         FBrLifeRules::SpawnDistanceSquared(Candidates[Anchor], Threats) >= FMath::Square(2000.0));
	Threats.Add({Candidates[Anchor], false, false, true});
	TestEqual(TEXT("Own group cluster doesn't repel anchor"), FBrLifeRules::PickSpawn(Candidates, Threats, 2000),
	          Anchor);
	Threats.Add({Candidates[Anchor], false, true, true});
	const int32 Next = FBrLifeRules::PickSpawn(Candidates, Threats, 2000);
	TestTrue(TEXT("Next enemy group anchor separates from both groups"),
	         FBrLifeRules::SpawnDistanceSquared(Candidates[Next], Threats) >= FMath::Square(2000.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFlowNameTest, "BrGame.Flow.NameSanitization",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFlowNameTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Whitespace removed"), FBrFlowRules::SanitizeName(TEXT("  Jane Doe \t")), FString(TEXT("Jane Doe")));
	TestEqual(TEXT("URL, markup and control characters cannot escape into names"),
	          FBrFlowRules::SanitizeName(TEXT("A?B\n<C>|D")), FString(TEXT("A_B__C__D")));
	TestEqual(TEXT("Empty name gets usable fallback"), FBrFlowRules::SanitizeName(TEXT(" \r\n ")),
	          FString(TEXT("Player")));
	const FString Long = TEXT("abcdefghijklmnopqrstuvwxyz0123456789");
	const FString Name = FBrFlowRules::SanitizeName(Long);
	TestTrue(TEXT("Long name is bounded and retains its prefix"), Name.Len() < Long.Len() && Long.StartsWith(Name));
	TestEqual(TEXT("Already sanitized is idempotent"), FBrFlowRules::SanitizeName(Name), Name);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFlowGraceTest, "BrGame.Flow.ReconnectGraceForfeit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFlowGraceTest::RunTest(const FString& Parameters)
{
	TestFalse(
		TEXT("Connected initialized contestant cannot forfeit"), FBrReconnectRules::GraceExpired(false, false, 10, 20));
	TestFalse(TEXT("Disconnected still inside grace"), FBrReconnectRules::GraceExpired(true, false, 10, 9.9f));
	TestTrue(
		TEXT("Boundary expiry, including travelling uninitialized contestant"),
		FBrReconnectRules::GraceExpired(true, false, 10, 10));
	TestFalse(TEXT("No deadline is not expiry"), FBrReconnectRules::GraceExpired(true, false, -1, 100));
	FBrRoundRecord Record;
	Record.State = EBrLifeState::Downed;
	Record.DownedEndTime = 3;
	Record.Life.Deaths = 1;
	TestTrue(TEXT("Offline dying episode resolves on server timeline"), FBrReconnectRules::ResolveOfflineTimeout(
		         Record.Life, Record.State, Record.DownedEndTime, 3, 5, true));
	TestEqual(TEXT("Offline respawn outcome charged once"), Record.Life.Deaths, 2);
	TestTrue(TEXT("Grace marks permanent forfeit"), FBrReconnectRules::Forfeit(Record));
	TestTrue(TEXT("Forfeit is eliminated"), Record.Life.bEliminated && Record.State == EBrLifeState::Eliminated);
	TestFalse(TEXT("Forfeit cannot be repeated"), FBrReconnectRules::Forfeit(Record));
	TestFalse(TEXT("No repeated grace decision"), FBrReconnectRules::GraceExpired(true, Record.bForfeited, 10, 30));
	TestTrue(TEXT("Fresh reconnect bookkeeping cannot undo forfeit"),
	         FBrReconnectRules::Merge(Record.Life, FBrLifeBookkeeping()).bEliminated);
	TestFalse(TEXT("Offline dying outcome cannot resurrect forfeited record"), FBrReconnectRules::ResolveOfflineTimeout(
		          Record.Life, Record.State, 0, 20, 5, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFlowDamageSourceTest, "BrGame.Flow.PersistentDamageSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFlowDamageSourceTest::RunTest(const FString& Parameters)
{
	FBrDamageSource Source;
	Source.RoundKey = TEXT("AdmittedRoundRecord");
	Source.PlayerId = 42;
	Source.Team = 2;
	TestFalse(TEXT("Pawn/PlayerState may no longer exist"), Source.PlayerState.IsValid());
	TestTrue(TEXT("Captured source still identifies respawned self"), Source.IsSelf(42));
	TestTrue(TEXT("Captured source still identifies teammate"), Source.IsFriendly(2));
	TestFalse(TEXT("Other group is not teammate"), Source.IsFriendly(3));
	Source.Team = INDEX_NONE;
	TestFalse(TEXT("FFA not a friendly group"), Source.IsFriendly(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFlowDestinationTest, "BrGame.Flow.DestinationAdmission",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrFlowDestinationTest::RunTest(const FString& Parameters)
{
	TestFalse(
		TEXT("Carried controller has not yet been admitted in destination"), FBrFlowRules::CanRetrySpawn(false, true));
	TestFalse(TEXT("Even an admitted controller must finish loading"), FBrFlowRules::CanRetrySpawn(true, false));
	TestFalse(TEXT("Still travelling controller cannot be retried"), FBrFlowRules::CanRetrySpawn(false, false));
	TestTrue(TEXT("Only initialized loaded destination controller can retry"), FBrFlowRules::CanRetrySpawn(true, true));
	return true;
}
#endif
