#pragma once
#include "CoreMinimal.h"
#include "BrPickupRules.h"

/** Pure policy; indices match the developer crate definition list. */
struct FBrCrateRules
{
	enum ELoot { Pistol, AssaultRifle, MachineGun, Ammo, Grenade, Health20, Health50, Health100, TypeCount };
	static int32 Count(FRandomStream& Stream, int32 Min, int32 Max)
	{
		Min = FMath::Clamp(Min, 0, 100);
		return Stream.RandRange(Min, FMath::Clamp(Max, Min, 100));
	}
	static TArray<int32> Roll(FRandomStream& Stream, int32 ARMin, int32 ARMax, float MGChance,
		int32 HealthMin, int32 HealthMax, const TArray<float>& HealthWeights,
		int32 AmmoMin, int32 AmmoMax, int32 GrenadeMin, int32 GrenadeMax)
	{
		TArray<int32> Result = { Pistol };
		auto Add = [&](int32 Type, int32 N) { for (int32 I = 0; I < N; ++I) Result.Add(Type); };
		Add(AssaultRifle, Count(Stream, ARMin, ARMax));
		if (Stream.FRand() < FMath::Clamp(MGChance, 0.f, 1.f)) Result.Add(MachineGun);
		Add(Ammo, Count(Stream, AmmoMin, AmmoMax));
		Add(Grenade, Count(Stream, GrenadeMin, GrenadeMax));
		const int32 N = Count(Stream, HealthMin, HealthMax);
		for (int32 I = 0; I < N; ++I)
		{
			const int32 Size = FBrPickupRules::WeightedIndex(HealthWeights, Stream);
			if (Size >= 0 && Size < 3) Result.Add(Health20 + Size);
		}
		return Result;
	}
	static FVector Scatter(FRandomStream& Stream, float Min, float Max)
	{
		Min = FMath::Max(0.f, Min);
		const float Radius = Stream.FRandRange(Min, FMath::Max(Min, Max));
		const float Angle = Stream.FRandRange(0, 2 * PI);
		return FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius;
	}
	static FVector LandingCandidate(FRandomStream& Stream, const FVector& Center, float Radius)
	{
		return Center + Scatter(Stream, 0, FMath::Max(0.f, Radius));
	}
	static bool Contains(const FVector& Point, const FVector& Center, float Radius, const FBox& Bounds)
	{
		return Radius > 0 && FVector::DistSquared2D(Point, Center) <= FMath::Square(Radius) &&
			Bounds.IsValid && Point.X >= Bounds.Min.X && Point.X <= Bounds.Max.X &&
			Point.Y >= Bounds.Min.Y && Point.Y <= Bounds.Max.Y;
	}
	static int32 InitialWave(int32 Base) { return FMath::Clamp(Base, 0, 100); }
	static int32 IntervalWave(int32 Count) { return FMath::Clamp(Count, 1, 100); }
	/** Supporting floor must span the scatter disc plus the crate clearance radius. */
	static float FloorHalfWidth(float ScatterMin, float ScatterMax)
	{
		return 85.f + FMath::Max(0.f, FMath::Max(
			FMath::IsFinite(ScatterMin) ? ScatterMin : 0.f,
			FMath::IsFinite(ScatterMax) ? ScatterMax : 0.f));
	}
	/** Tuned wave spacing, never below scatter-disc separation or the 170 cm crate-body minimum. */
	static float WaveSpacing(float MinSpacing, float ScatterMin, float ScatterMax)
	{
		return FMath::Max(FMath::IsFinite(MinSpacing) ? MinSpacing : 0.f, FMath::Max(170.f, 2 * FMath::Max(
			FMath::IsFinite(ScatterMin) ? ScatterMin : 0.f,
			FMath::IsFinite(ScatterMax) ? ScatterMax : 0.f)));
	}
	static bool IsWaveSpaced(const FVector& Candidate, const TArray<FVector>& Previous, float Spacing)
	{
		for (const FVector& Point : Previous)
			if (FVector::DistSquared2D(Candidate, Point) < FMath::Square(Spacing)) return false;
		return true;
	}
	static double NextDrop(double Now, float Interval)
	{
		return Now + (FMath::IsFinite(Interval) ? FMath::Clamp(Interval, 1.f, 3600.f) : 30.f);
	}
};
