#pragma once

#include "CoreMinimal.h"
#include "BrZoneRules.generated.h"

USTRUCT()
struct FBrZoneStage
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, meta=(ClampMin="0", Units="s"))
	float WaitTime = 90;
	UPROPERTY(EditAnywhere, meta=(ClampMin="0.1", Units="s"))
	float ShrinkTime = 60;
	UPROPERTY(EditAnywhere, meta=(ClampMin="0", ClampMax="1"))
	float TargetRadiusFraction = 0.6f;
	UPROPERTY(EditAnywhere, meta=(ClampMin="0"))
	float DamagePerSecond = 2;
};

USTRUCT()
struct FBrZoneCircle
{
	GENERATED_BODY()
	UPROPERTY()
	FVector Center = FVector::ZeroVector;
	UPROPERTY()
	float Radius = 0;
	bool IsInside(const FVector& Location) const
	{
		// A collapsed circle has no safe area, even at its exact center.
		return Radius > 0 && FVector::DistSquared2D(Center, Location) <= FMath::Square(Radius);
	}
};

/** One atomic snapshot, including everything a late joiner needs. No frame-by-frame replication. */
USTRUCT()
struct FBrZoneState
{
	GENERATED_BODY()
	UPROPERTY()
	int32 StageIndex = INDEX_NONE;
	UPROPERTY()
	FBrZoneCircle Current;
	UPROPERTY()
	FBrZoneCircle Target;
	UPROPERTY()
	double WaitEnd = 0;
	UPROPERTY()
	double ShrinkEnd = 0;
	UPROPERTY()
	float DamagePerSecond = 0;
	UPROPERTY()
	bool bFinal = false;
	UPROPERTY()
	bool bDisableRespawns = true;
	UPROPERTY()
	bool bPaused = false;
	UPROPERTY()
	double PauseTime = 0;
	double EffectiveTime(double Time) const { return bPaused ? PauseTime : Time; }
	FBrZoneCircle At(double Time) const
	{
		const double Now = EffectiveTime(Time);
		// Completed and instantaneous intervals must reach the target, however short the stage.
		if (Now >= ShrinkEnd) return Target;
		if (Now <= WaitEnd) return Current;
		const double Alpha = (Now - WaitEnd) / (ShrinkEnd - WaitEnd);
		return { FMath::Lerp(Current.Center, Target.Center, Alpha),
			FMath::Lerp(Current.Radius, Target.Radius, static_cast<float>(Alpha)) };
	}
	/** Preserve the visible circle; only the remaining wait/shrink time changes. */
	void Resnapshot(double Time, double NewStartTime, double RemainingTimeScale)
	{
		const double Now = EffectiveTime(Time);
		const FBrZoneCircle Snapshot = At(Time);
		const double WaitRemaining = FMath::Max(0.0, WaitEnd - Now);
		const double ShrinkRemaining = FMath::Max(0.0, ShrinkEnd - FMath::Max(Now, WaitEnd));
		Current = Snapshot;
		WaitEnd = NewStartTime + WaitRemaining * RemainingTimeScale;
		ShrinkEnd = WaitEnd + ShrinkRemaining * RemainingTimeScale;
		if (bPaused) PauseTime = NewStartTime;
	}
	bool IsSuddenDeath(double Time) const { return bDisableRespawns && bFinal && EffectiveTime(Time) >= WaitEnd; }
	bool IsFinished(double Time) const { return EffectiveTime(Time) >= ShrinkEnd; }
};

struct FBrZoneRules
{
	/** Repairs invalid developer data rather than leaving a match running without a terminal zone. */
	static bool RepairStages(TArray<FBrZoneStage>& Stages)
	{
		bool bChanged = false;
		if (Stages.IsEmpty()) { Stages.AddDefaulted(); bChanged = true; }
		const FBrZoneStage Defaults;
		for (FBrZoneStage& Stage : Stages)
		{
			auto Repair = [&](float& Value, float Default, float Minimum, float Maximum)
			{
				const float Valid = FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Default;
				if (Value != Valid) { Value = Valid; bChanged = true; }
			};
			Repair(Stage.WaitTime, Defaults.WaitTime, 0, MAX_flt);
			Repair(Stage.ShrinkTime, Defaults.ShrinkTime, 0.1f, MAX_flt);
			Repair(Stage.TargetRadiusFraction, Defaults.TargetRadiusFraction, 0, 1);
			Repair(Stage.DamagePerSecond, Defaults.DamagePerSecond, 0, MAX_flt);
		}
		if (Stages.Last().TargetRadiusFraction != 0) { Stages.Last().TargetRadiusFraction = 0; bChanged = true; }
		if (Stages.Last().DamagePerSecond < 1) { Stages.Last().DamagePerSecond = 1; bChanged = true; }
		return bChanged;
	}
	static float DamagePerTick(float DPS, float Interval) { return FMath::Max(0.0f, DPS) * FMath::Max(0.0f, Interval); }
	static bool Contains(const FBrZoneCircle& Outer, const FBrZoneCircle& Inner)
	{
		return FVector::Dist2D(Outer.Center, Inner.Center) + Inner.Radius <= Outer.Radius + 0.01;
	}
	static FBrZoneCircle NextCircle(const FBrZoneCircle& Current, float Fraction, FRandomStream& Stream)
	{
		FBrZoneCircle Next;
		Next.Radius = Current.Radius * FMath::Clamp(Fraction, 0.0f, 1.0f);
		const float Angle = Stream.FRandRange(0, 2 * PI);
		const float Distance = FMath::Sqrt(Stream.FRand()) * (Current.Radius - Next.Radius);
		Next.Center = Current.Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Distance;
		return Next;
	}
	static bool IsPlayableTarget(const FBrZoneCircle& Current, const FBrZoneCircle& Target, const FBox& Bounds)
	{
		return Contains(Current, Target) && Target.Center.X - Target.Radius >= Bounds.Min.X &&
			Target.Center.X + Target.Radius <= Bounds.Max.X && Target.Center.Y - Target.Radius >= Bounds.Min.Y &&
			Target.Center.Y + Target.Radius <= Bounds.Max.Y;
	}
	/** 0: preferred target, 1: current safe circle, 2: ineligible. */
	static int32 SpawnRank(const FVector& Point, const FBrZoneCircle& Current, const FBrZoneCircle& Target)
	{
		return !Current.IsInside(Point) ? 2 : Target.IsInside(Point) ? 0 : 1;
	}
	static bool CanAdvance(int32 Index, int32 Count, const FBrZoneState& State, double Now)
	{
		return Index >= 0 && Index + 1 < Count && State.IsFinished(Now);
	}
};

/** Round-local retry budget, shared by pawn-less joins and failed downed respawns. */
struct FBrSpawnRetryBudget
{
	static constexpr int32 MaxAttempts = 4;
	static constexpr double RetryInterval = 1;
	int32 Failures = 0;
	double NextAttemptTime = 0;
	bool CanAttempt(double Now) const { return Failures < MaxAttempts && Now >= NextAttemptTime; }
	bool Failed(double Now) { ++Failures; NextAttemptTime = Now + RetryInterval; return Failures >= MaxAttempts; }
	void Reset() { Failures = 0; NextAttemptTime = 0; }
};
