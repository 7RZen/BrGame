#pragma once

#include "CoreMinimal.h"
#include "BrLifeRules.generated.h"

UENUM(BlueprintType)
enum class EBrLifeState : uint8 { Alive, Downed, Eliminated };

UENUM(BlueprintType)
enum class EBrReviveRule : uint8 { Anyone, TeammatesOnly };

enum class EBrDownedOutcome : uint8 { Refused, VoluntaryRespawn, TimeoutRespawn, Eliminated };

struct FBrSpawnThreat
{
	FVector Location = FVector::ZeroVector;
	bool bSelf = false;
	bool bEnemy = true;
	bool bAliveOrDowned = true;
};

struct FBrLifeRules
{
	static float MaxHealth(float Base, int32 VoluntaryCount, float Penalty, float MinimumFraction)
	{
		return FMath::Clamp(Base, 1.0f, 100.0f) * FMath::Clamp(
			1.0f - FMath::Max(0, VoluntaryCount) * FMath::Clamp(Penalty, 0.0f, 1.0f),
			FMath::Clamp(MinimumFraction, 0.01f, 1.0f), 1.0f);
	}
	static EBrDownedOutcome Outcome(bool bVoluntary, int32 Deaths, int32 MaxDeaths)
	{
		if (Deaths >= FMath::Max(0, MaxDeaths))
			return bVoluntary ? EBrDownedOutcome::Refused : EBrDownedOutcome::Eliminated;
		return bVoluntary ? EBrDownedOutcome::VoluntaryRespawn : EBrDownedOutcome::TimeoutRespawn;
	}
	static bool CanRevive(EBrLifeState Reviver, EBrLifeState Target, float DistanceSquared,
		float Radius, EBrReviveRule Rule, bool bTeammates, bool bSelf)
	{
		return !bSelf && Reviver == EBrLifeState::Alive && Target == EBrLifeState::Downed &&
			DistanceSquared <= FMath::Square(FMath::Max(0.0f, Radius)) &&
			(Rule == EBrReviveRule::Anyone || bTeammates);
	}
	static double SpawnDistanceSquared(const FVector& Candidate, const TArray<FBrSpawnThreat>& Threats)
	{
		double Minimum = TNumericLimits<double>::Max();
		for (const FBrSpawnThreat& Threat : Threats)
		{
			if (!Threat.bSelf && Threat.bEnemy && Threat.bAliveOrDowned)
				Minimum = FMath::Min(Minimum, FVector::DistSquared(Candidate, Threat.Location));
		}
		return Minimum;
	}
	/** Valid/clear/safe candidates only. Prefer safe-distance candidates, then maximize minimum distance. */
	static int32 PickSpawn(const TArray<FVector>& Candidates, const TArray<FBrSpawnThreat>& Threats, float MinDistance)
	{
		int32 Best = INDEX_NONE;
		double BestDistance = -1;
		bool bBestPreferred = false;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const double Distance = SpawnDistanceSquared(Candidates[Index], Threats);
			const bool bPreferred = Distance >= FMath::Square(FMath::Max(0.0f, MinDistance));
			if (Best == INDEX_NONE || (bPreferred && !bBestPreferred) ||
				(bPreferred == bBestPreferred && Distance > BestDistance))
			{
				Best = Index;
				BestDistance = Distance;
				bBestPreferred = bPreferred;
			}
		}
		return Best;
	}
};
