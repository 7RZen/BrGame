#include "BrZoneRules.h"
#include "BrLifeRules.h"
#include "BrRoundRecord.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrZoneInterpolationTest, "BrGame.Zone.Interpolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrZoneInterpolationTest::RunTest(const FString& Parameters)
{
	FBrZoneState State;
	State.Current = {FVector(10, 20, 0), 200};
	State.Target = {FVector(60, 40, 0), 80};
	State.WaitEnd = 15;
	State.ShrinkEnd = 35;
	TestEqual(TEXT("Wait radius"), State.At(10).Radius, State.Current.Radius);
	TestEqual(TEXT("Wait center"), State.At(15).Center, State.Current.Center);
	TestEqual(TEXT("Mid shrink radius"), State.At(25).Radius, (State.Current.Radius + State.Target.Radius) / 2);
	TestEqual(TEXT("Mid shrink center"), State.At(25).Center, (State.Current.Center + State.Target.Center) / 2);
	TestEqual(TEXT("After shrink"), State.At(100).Radius, State.Target.Radius);
	State.bPaused = true;
	State.PauseTime = 25;
	TestEqual(TEXT("Paused radius does not move"), State.At(500).Radius, State.At(25).Radius);
	State.bPaused = false;
	TestTrue(TEXT("Inside ignores height"), State.At(10).IsInside(FVector(10, 20, 100000)));
	TestTrue(TEXT("Boundary inclusive"), State.At(10).IsInside(
		         State.Current.Center + FVector(State.Current.Radius, 0, 0)));
	TestFalse(TEXT("Outside"), State.At(10).IsInside(State.Current.Center + FVector(State.Current.Radius + 1, 0, 0)));
	TestFalse(TEXT("Collapsed circle never safe"), FBrZoneCircle().IsInside(FVector::ZeroVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrZoneContainmentTest, "BrGame.Zone.Containment",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrZoneContainmentTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream(1731);
	for (int32 I = 0; I < 2000; ++I)
	{
		const FBrZoneCircle Initial{
			FVector(Stream.FRandRange(-5000, 5000), Stream.FRandRange(-5000, 5000), 17),
			Stream.FRandRange(10, 10000)
		};
		const float Fraction = Stream.FRand();
		const FBrZoneCircle Next = FBrZoneRules::NextCircle(Initial, Fraction, Stream);
		if (!TestTrue(TEXT("Entire next circle is contained"), FBrZoneRules::Contains(Initial, Next)))
		{
			return false;
		}
		TestEqual(TEXT("Fraction controls target radius"), Next.Radius, Initial.Radius * Fraction);
		TestEqual(TEXT("Center stays on same plane"), Next.Center.Z, Initial.Center.Z);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrZoneProgressionTest, "BrGame.Zone.ProgressionAndDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrZoneProgressionTest::RunTest(const FString& Parameters)
{
	FBrZoneState State;
	State.StageIndex = 1;
	State.WaitEnd = 18;
	State.ShrinkEnd = 27;
	State.DamagePerSecond = 13;
	TestEqual(TEXT("Damage is DPS times interval"), FBrZoneRules::DamagePerTick(State.DamagePerSecond, 0.3f),
	          State.DamagePerSecond * 0.3f);
	TestEqual(TEXT("Negative interval safe"), FBrZoneRules::DamagePerTick(State.DamagePerSecond, -1), 0.0f);
	TestFalse(TEXT("No advance while waiting"), FBrZoneRules::CanAdvance(1, 3, State, 17));
	TestFalse(TEXT("No advance while shrinking"), FBrZoneRules::CanAdvance(1, 3, State, 26));
	TestTrue(TEXT("Advance at deadline"), FBrZoneRules::CanAdvance(1, 3, State, 27));
	TestFalse(TEXT("Final never advances beyond array"), FBrZoneRules::CanAdvance(2, 3, State, 100));
	State.bFinal = true;
	TestFalse(TEXT("Final wait still permits respawn"), State.IsSuddenDeath(17));
	TestTrue(TEXT("Final shrink disables respawn"), State.IsSuddenDeath(18));
	TestTrue(TEXT("Final collapse still sudden death"), State.IsSuddenDeath(100));
	TestTrue(
		TEXT("Timeout eliminates without respawns"),
		FBrLifeRules::Outcome(false, 0, State.IsSuddenDeath(18) ? 0 : 10) == EBrDownedOutcome::Eliminated);
	TestTrue(
		TEXT("Voluntary denied without eliminating"), FBrLifeRules::Outcome(true, 0, 0) == EBrDownedOutcome::Refused);
	FBrLifeBookkeeping Life;
	EBrLifeState LifeState = EBrLifeState::Downed;
	TestTrue(
		TEXT("Offline downed episode resolves"),
		FBrReconnectRules::ResolveOfflineTimeout(Life, LifeState, 20, 21, 0, true));
	TestTrue(TEXT("Offline timeout cannot bypass sudden death"), Life.bEliminated);
	State.bDisableRespawns = false;
	TestFalse(TEXT("Developer opt out"), State.IsSuddenDeath(100));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrZoneSpawnTest, "BrGame.Zone.SpawnFiltering",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrZoneSpawnTest::RunTest(const FString& Parameters)
{
	const FBrZoneCircle Current{FVector::ZeroVector, 100};
	const FBrZoneCircle Target{FVector(25, 0, 0), 30};
	TestEqual(TEXT("Next circle preferred"), FBrZoneRules::SpawnRank(FVector(25, 0, 0), Current, Target), 0);
	TestEqual(TEXT("Current circle eligible"), FBrZoneRules::SpawnRank(FVector(-80, 0, 0), Current, Target), 1);
	TestEqual(TEXT("Unsafe candidates filtered"), FBrZoneRules::SpawnRank(FVector(101, 0, 0), Current, Target), 2);
	TestEqual(TEXT("Height does not filter"), FBrZoneRules::SpawnRank(FVector(25, 0, 9999), Current, Target), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrZoneResnapshotTest, "BrGame.Zone.TimeScaleMonotonic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrZoneResnapshotTest::RunTest(const FString& Parameters)
{
	for (const double Duration : {20.0, 0.0001})
	{
		FBrZoneState State;
		State.Current = {FVector::ZeroVector, 1000};
		State.Target = {FVector(200, 0, 0), 0};
		State.WaitEnd = 10;
		State.ShrinkEnd = State.WaitEnd + Duration;
		double Now = State.WaitEnd + Duration * 0.75;
		for (const double Ratio : {0.001, 1000.0, 0.01, 100.0})
		{
			const FBrZoneCircle Before = State.At(Now);
			State.Resnapshot(Now, Now, Ratio);
			TestEqual(TEXT("Scale change is radius-continuous"), State.At(Now).Radius, Before.Radius);
			TestEqual(TEXT("Scale change is center-continuous"), State.At(Now).Center, Before.Center);
			const double End = State.ShrinkEnd;
			float PreviousRadius = Before.Radius;
			for (int32 I = 1; I <= 16; ++I)
			{
				const float Radius = State.At(Now + (End - Now) * I / 16).Radius;
				TestTrue(TEXT("Radius never grows after scaling"), Radius <= PreviousRadius);
				PreviousRadius = Radius;
			}
			Now += (End - Now) * 0.25;
		}
		const FBrZoneCircle BeforePause = State.At(Now);
		State.Resnapshot(Now, Now, 1);
		State.bPaused = true;
		State.PauseTime = Now;
		TestEqual(TEXT("Paused circle frozen"), State.At(Now + 100).Radius, BeforePause.Radius);
		State.Resnapshot(State.PauseTime, Now + 100, 1);
		State.bPaused = false;
		TestEqual(TEXT("Resume does not expand"), State.At(Now + 100).Radius, BeforePause.Radius);
		const double End = State.ShrinkEnd;
		State.Resnapshot(End + 1, End + 1, 1000);
		TestEqual(TEXT("Completed stage stays completed"), State.At(End + 1).Radius, State.Target.Radius);
	}
	FBrZoneState Instant;
	Instant.Current.Radius = 100;
	Instant.WaitEnd = Instant.ShrinkEnd = 4;
	TestEqual(TEXT("Instant interval waits before deadline"), Instant.At(3).Radius, Instant.Current.Radius);
	TestEqual(TEXT("Instant interval completes at deadline"), Instant.At(4).Radius, Instant.Target.Radius);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrZoneConfigRepairTest, "BrGame.Zone.UnusableConfigRepair",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrZoneConfigRepairTest::RunTest(const FString& Parameters)
{
	TArray<FBrZoneStage> Stages;
	TestTrue(TEXT("Empty stages repaired"), FBrZoneRules::RepairStages(Stages));
	TestTrue(
		TEXT("Repair creates a terminating stage"),
		Stages.Num() > 0 && Stages.Last().DamagePerSecond > 0 && Stages.Last().TargetRadiusFraction == 0);
	Stages[0].WaitTime = std::numeric_limits<float>::quiet_NaN();
	Stages[0].ShrinkTime = -1;
	Stages.Last().DamagePerSecond = 0;
	Stages.Last().TargetRadiusFraction = 1;
	TestTrue(TEXT("Unusable stage repaired"), FBrZoneRules::RepairStages(Stages));
	TestTrue(TEXT("Finite wait"), FMath::IsFinite(Stages[0].WaitTime) && Stages[0].WaitTime >= 0);
	TestTrue(TEXT("Positive duration"), Stages[0].ShrinkTime > 0);
	TestTrue(TEXT("Positive terminal damage"), FBrZoneRules::DamagePerTick(Stages.Last().DamagePerSecond, 0.5f) > 0);
	TestEqual(TEXT("Final collapse required"), Stages.Last().TargetRadiusFraction, 0.0f);
	TestFalse(TEXT("Repair is idempotent"), FBrZoneRules::RepairStages(Stages));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrZoneTargetValidationTest, "BrGame.Zone.PlayableTargetValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrZoneTargetValidationTest::RunTest(const FString& Parameters)
{
	const FBrZoneCircle Current{FVector::ZeroVector, 100};
	const FBox Bounds(FVector(-80, -80, -100), FVector(80, 80, 100));
	TestTrue(TEXT("Concentric playable fallback"), FBrZoneRules::IsPlayableTarget(
		         Current, {FVector::ZeroVector, 70}, Bounds));
	TestFalse(
		TEXT("Outside playable rectangle rejected"),
		FBrZoneRules::IsPlayableTarget(Current, {FVector(75, 0, 0), 10}, Bounds));
	TestFalse(TEXT("Outside current circle rejected"), FBrZoneRules::IsPlayableTarget(
		          Current, {FVector(70, 70, 0), 10}, Bounds));
	// A nav projection is not permission to bypass the spawn zone check.
	TestEqual(
		TEXT("Unsafe projected center is ineligible"), FBrZoneRules::SpawnRank(FVector(120, 0, 0), Current, Current),
		2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrZoneSpawnRetryTest, "BrGame.Zone.BoundedSpawnRetries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrZoneSpawnRetryTest::RunTest(const FString& Parameters)
{
	FBrSpawnRetryBudget Budget;
	double Now = 10;
	for (int32 I = 0; I < FBrSpawnRetryBudget::MaxAttempts; ++I)
	{
		TestTrue(TEXT("Attempt allowed after timer"), Budget.CanAttempt(Now));
		TestEqual(TEXT("Only last failure forfeits"), Budget.Failed(Now), I + 1 == FBrSpawnRetryBudget::MaxAttempts);
		TestFalse(TEXT("No immediate retry/RPC spam"), Budget.CanAttempt(Now));
		Now = Budget.NextAttemptTime;
	}
	TestFalse(TEXT("Exhausted contestant never retries forever"), Budget.CanAttempt(Now + 10000));
	Budget.Reset();
	TestTrue(TEXT("Successful spawn resets budget"), Budget.CanAttempt(Now));
	return true;
}
#endif
