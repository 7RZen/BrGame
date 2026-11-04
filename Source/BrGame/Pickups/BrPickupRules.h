#pragma once

#include "CoreMinimal.h"
#include "BrCombatMath.h"

struct FBrWeaponPickupPlan
{
	bool bSameWeapon = false;
	bool bDropOldWeapon = false;
	int32 DropLoaded = 0;
	int32 LoadedAfter = 0;
	int32 ReserveGrant = 0;
};

struct FBrPickupRules
{
	static float HealthGrant(float Health, float MaxHealth, float Fraction)
	{
		return FMath::Max(0.0f, FMath::Min(MaxHealth - Health, MaxHealth * FMath::Clamp(Fraction, 0.0f, 1.0f)));
	}
	static int32 AmmoGrant(int32 Rounds, int32 Reserve, int32 Cap)
	{
		const int32 Limit = Cap > 0 ? Cap : MAX_int32;
		return FMath::Min(FMath::Max(0, Rounds), FMath::Max(0, Limit - FMath::Max(0, Reserve)));
	}
	static int32 GrenadeGrant(int32 Count, int32 Cap) { return Count < FMath::Max(0, Cap) ? 1 : 0; }
	static FBrWeaponPickupPlan WeaponPlan(bool bHasOld, bool bSame, int32 OldLoaded,
		int32 PickupSize, int32 StartingMagazines, bool bDropped, int32 PickupLoaded)
	{
		FBrWeaponPickupPlan Plan;
		Plan.bSameWeapon = bSame;
		Plan.bDropOldWeapon = bHasOld && !bSame;
		Plan.DropLoaded = Plan.bDropOldWeapon ? FMath::Max(0, OldLoaded) : 0;
		const int32 Loaded = bDropped ? FMath::Clamp(PickupLoaded, 0, FMath::Max(0, PickupSize)) : FMath::Max(0, PickupSize);
		const int32 Spare = bDropped ? 0 : FBrCombatMath::GrantReserve(PickupSize, StartingMagazines);
		Plan.LoadedAfter = bSame ? OldLoaded : Loaded;
		Plan.ReserveGrant = bSame ? static_cast<int32>(FMath::Min<int64>(MAX_int32, static_cast<int64>(Loaded) + Spare)) : Spare;
		return Plan;
	}
	static int32 WeightedIndex(const TArray<float>& Weights, FRandomStream& Stream)
	{
		double Total = 0;
		for (float Weight : Weights) if (FMath::IsFinite(Weight) && Weight > 0) Total += Weight;
		if (Total <= 0) return INDEX_NONE;
		double Roll = Stream.FRand() * Total;
		int32 LastPositive = INDEX_NONE;
		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			if (!FMath::IsFinite(Weights[Index]) || Weights[Index] <= 0) continue;
			LastPositive = Index;
			Roll -= Weights[Index];
			if (Roll < 0) return Index;
		}
		return LastPositive;
	}
};
