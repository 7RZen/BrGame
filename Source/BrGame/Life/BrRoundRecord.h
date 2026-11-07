#pragma once

#include "CoreMinimal.h"
#include "BrInventoryComponent.h"
#include "BrLifeRules.h"
#include "BrZoneRules.h"
#include "BrQuestRules.h"
#include "BrPenaltyDefinition.h"
#include "BrRoundRecord.generated.h"

USTRUCT()
struct FBrLifeBookkeeping
{
	GENERATED_BODY()
	UPROPERTY()
	int32 Deaths = 0;
	UPROPERTY()
	int32 VoluntaryRespawns = 0;
	UPROPERTY()
	bool bEliminated = false;
};

/** Server-only, round-local ledger. Strong asset references keep disconnected gear definitions alive. */
USTRUCT()
struct FBrRoundRecord
{
	GENERATED_BODY()
	UPROPERTY()
	FBrLifeBookkeeping Life;
	UPROPERTY()
	FBrInventoryState Gear;
	UPROPERTY()
	TArray<float> ArmorPlates;
	UPROPERTY()
	FBrQuestJournal Journal;
	UPROPERTY()
	FBrPenaltyState Penalty;
	FBrScoreEpisode ScoreEpisode;
	UPROPERTY()
	TWeakObjectPtr<AController> ActiveController;
	int32 Team = INDEX_NONE;
	int32 PlayerId = 0;
	int32 DownsInflicted = 0;
	FString PlayerName;
	FLinearColor TeamColor = FLinearColor::White;
	bool bHasPawnSnapshot = false;
	bool bMatchStartContestant = false;
	FBrSpawnRetryBudget SpawnRetries;
	EBrLifeState State = EBrLifeState::Alive;
	float Health = 100;
	float MaxHealth = 100;
	float DownedEndTime = 0;
	float ProtectionEndTime = 0;
	bool bAwaitingInitialization = false;
	bool bForfeited = false;
	bool bOfflineCorpseLeft = false;
	float ReconnectDeadline = -1;
	FVector GroundLocation = FVector::ZeroVector;
	float Yaw = 0;
	float UnstuckCooldownEnd = 0;
};

struct FBrReconnectRules
{
	static bool GraceExpired(bool bUnavailable, bool bForfeited, float Deadline, float Now)
	{
		return bUnavailable && !bForfeited && Deadline >= 0 && FMath::IsFinite(Deadline) &&
			FMath::IsFinite(Now) && Now >= Deadline;
	}
	static bool Forfeit(FBrRoundRecord& Record)
	{
		if (Record.bForfeited) return false;
		Record.bForfeited = true;
		Record.Life.bEliminated = true;
		Record.State = EBrLifeState::Eliminated;
		Record.Health = 0;
		Record.ArmorPlates.Reset();
		Record.Penalty = FBrPenaltyState();
		Record.DownedEndTime = Record.ProtectionEndTime = 0;
		return true;
	}
	// UE reactivates the inactive PS, then OverrideWith supplies the *fresh* login PS.
	// Merge monotonically so those default zeroes cannot erase the retained round history.
	static FBrLifeBookkeeping Merge(const FBrLifeBookkeeping& Retained, const FBrLifeBookkeeping& Incoming)
	{
		FBrLifeBookkeeping Result;
		Result.Deaths = FMath::Max(Retained.Deaths, Incoming.Deaths);
		Result.VoluntaryRespawns = FMath::Max(Retained.VoluntaryRespawns, Incoming.VoluntaryRespawns);
		Result.bEliminated = Retained.bEliminated || Incoming.bEliminated;
		return Result;
	}
	/** Resolve at most one expired offline dying episode. The absolute deadline never gets renewed. */
	static bool ResolveOfflineTimeout(FBrLifeBookkeeping& Life, EBrLifeState& State,
		float Deadline, float Now, int32 MaxDeaths, bool bCountDeath)
	{
		if (Life.bEliminated || State != EBrLifeState::Downed || Now < Deadline) return false;
		if (FBrLifeRules::Outcome(false, Life.Deaths, MaxDeaths) == EBrDownedOutcome::Eliminated)
		{
			Life.bEliminated = true;
			State = EBrLifeState::Eliminated;
		}
		else
		{
			if (bCountDeath) Life.Deaths = static_cast<int32>(FMath::Min<int64>(MAX_int32, static_cast<int64>(Life.Deaths) + 1));
			State = EBrLifeState::Alive;
		}
		return true;
	}
};
