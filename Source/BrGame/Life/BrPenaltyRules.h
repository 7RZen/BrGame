#pragma once

#include "BrLifeRules.h"

/** Pure policy: no actors, assets or timers. Absolute times use server world time. */
struct FBrPenaltyRules
{
	static bool CanPlant(EBrLifeState Planter, EBrLifeState Target, double DistanceSquared,
		float Radius, bool bSelf, bool bPlantedThisDown)
	{
		return FMath::IsFinite(DistanceSquared) && DistanceSquared >= 0 && FMath::IsFinite(Radius) &&
			!bPlantedThisDown && FBrLifeRules::CanRevive(Planter, Target, DistanceSquared,
			Radius, EBrReviveRule::Anyone, false, bSelf);
	}
	static float Progress(float Now, float Start, float End)
	{
		return FMath::Clamp((Now - Start) / FMath::Max(0.1f, End - Start), 0.f, 1.f);
	}
	static bool Active(bool bPresent, float Start, float End, float Now)
	{
		return bPresent && Start > 0 && Now >= Start && Now < End;
	}
	static float StartDormant(float Start, bool bAlive, float Now)
	{
		return Start <= 0 && bAlive ? FMath::Max(UE_SMALL_NUMBER, Now) : Start;
	}
	static bool Wins(float PlantEnd, float Now, const FBrDownedExit& Exit)
	{
		// Any tied life exit wins. No independent revive/respawn ordering here.
		return PlantEnd > 0 && Now >= PlantEnd && PlantEnd < Exit.Deadline;
	}
	static float Speed(float WeaponMultiplier, float PenaltyMultiplier)
	{
		return FMath::Max(0.f, WeaponMultiplier) * FMath::Clamp(PenaltyMultiplier, 0.1f, 4.f);
	}
};
