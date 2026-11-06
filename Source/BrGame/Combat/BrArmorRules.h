#pragma once
#include "CoreMinimal.h"

/** UObject-free armor policy. Zero-value plates are destroyed, not refillable slots. */
struct FBrArmorRules
{
	struct FAbsorption
	{
		float Remainder = 0;
		float Absorbed = 0;
		int32 Broken = 0;
	};
	static int32 Capacity(int32 MaxPlates) { return FMath::Clamp(MaxPlates, 0, 100); }
	static int32 Occupied(const TArray<float>& Plates)
	{
		int32 Count = 0;
		for (float Value : Plates) if (FMath::IsFinite(Value) && Value > 0) ++Count;
		return Count;
	}
	static bool CanAddPlate(const TArray<float>& Plates, int32 MaxPlates, float PlateValue)
	{
		return FMath::IsFinite(PlateValue) && PlateValue > 0 && Occupied(Plates) < Capacity(MaxPlates);
	}
	static bool AddPlate(TArray<float>& Plates, int32 MaxPlates, float PlateValue)
	{
		if (!CanAddPlate(Plates, MaxPlates, PlateValue)) return false;
		Plates.RemoveAll([](float Value) { return !FMath::IsFinite(Value) || Value <= 0; });
		Plates.Add(PlateValue);
		return true;
	}
	static FAbsorption Absorb(TArray<float>& Plates, float Damage, bool bBypass = false)
	{
		FAbsorption Result;
		if (!FMath::IsFinite(Damage) || Damage <= 0) return Result;
		Result.Remainder = Damage;
		if (bBypass) return Result;
		Plates.RemoveAll([](float Value) { return !FMath::IsFinite(Value) || Value <= 0; });
		while (Result.Remainder > 0 && !Plates.IsEmpty())
		{
			int32 Lowest = 0;
			for (int32 I = 1; I < Plates.Num(); ++I)
				if (Plates[I] < Plates[Lowest]) Lowest = I;
			const float Taken = FMath::Min(Plates[Lowest], Result.Remainder);
			Plates[Lowest] -= Taken;
			Result.Remainder -= Taken;
			Result.Absorbed += Taken;
			if (Plates[Lowest] <= 0)
			{
				Plates.RemoveAt(Lowest);
				++Result.Broken;
			}
		}
		return Result;
	}
};
