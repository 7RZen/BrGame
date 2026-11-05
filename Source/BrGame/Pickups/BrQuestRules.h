#pragma once

#include "CoreMinimal.h"
#include "BrQuestRules.generated.h"

class UBrWeaponDefinition;

UENUM()
enum class EBrQuestObjective : uint8
{
	DownOpponent,
	CollectGrenade,
	CollectWeapon
};

/** Immutable after offer creation; identity distinguishes duplicate objectives. */
USTRUCT()
struct FBrQuestOffer
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	EBrQuestObjective Objective = EBrQuestObjective::DownOpponent;
	UPROPERTY()
	TObjectPtr<UBrWeaponDefinition> Weapon;
};

USTRUCT()
struct FBrQuestProgress
{
	GENERATED_BODY()
	UPROPERTY()
	FBrQuestOffer Offer;
	UPROPERTY()
	bool bComplete = false;
};

USTRUCT()
struct FBrQuestJournal
{
	GENERATED_BODY()
	UPROPERTY()
	int32 Score = 0;
	UPROPERTY()
	TArray<FBrQuestProgress> Quests;
};

/** Server ledger episode, independent of pawn lifetime and DeathCount tuning. */
struct FBrScoreEpisode
{
	bool bDownOpen = false;
	bool bFinalCharged = false;
	FString DownerKey;
};

/** Header-only policy: no world, inventory mutation, or UObject calls. */
struct FBrQuestRules
{
	static int32 OfferCount(FRandomStream& Stream) { return Stream.RandRange(1, 2); }
	static EBrQuestObjective SelectObjective(FRandomStream& Stream)
	{
		return static_cast<EBrQuestObjective>(Stream.RandRange(0, 2));
	}
	static int32 SelectWeapon(FRandomStream& Stream, int32 Count)
	{
		return Count > 0 ? Stream.RandRange(0, Count - 1) : INDEX_NONE;
	}
	static bool Valid(const FBrQuestOffer& Offer)
	{
		return Offer.Id.IsValid() && (Offer.Objective == EBrQuestObjective::DownOpponent ||
			Offer.Objective == EBrQuestObjective::CollectGrenade ||
			(Offer.Objective == EBrQuestObjective::CollectWeapon && Offer.Weapon));
	}
	static bool CanAccept(const FBrQuestJournal& Journal, const FBrQuestOffer& Offer)
	{
		return Valid(Offer) && !Journal.Quests.ContainsByPredicate(
			[&](const FBrQuestProgress& Quest) { return Quest.Offer.Id == Offer.Id; });
	}
	static bool Accept(FBrQuestJournal& Journal, const FBrQuestOffer& Offer)
	{
		if (!CanAccept(Journal, Offer)) return false;
		FBrQuestProgress Progress;
		Progress.Offer = Offer;
		Journal.Quests.Add(Progress);
		return true;
	}
	static void AddScore(FBrQuestJournal& Journal, int32 Delta)
	{
		Journal.Score = static_cast<int32>(FMath::Clamp<int64>(
			static_cast<int64>(Journal.Score) + Delta, MIN_int32, MAX_int32));
	}
	static int32 Progress(FBrQuestJournal& Journal, EBrQuestObjective Event, const UBrWeaponDefinition* Weapon = nullptr)
	{
		int32 Completed = 0;
		for (FBrQuestProgress& Quest : Journal.Quests)
		{
			if (Quest.bComplete || Quest.Offer.Objective != Event ||
				(Event == EBrQuestObjective::CollectWeapon && Quest.Offer.Weapon != Weapon)) continue;
			Quest.bComplete = true;
			AddScore(Journal, 2);
			++Completed;
		}
		return Completed;
	}
	static bool Down(FBrQuestJournal& Journal, FBrScoreEpisode& Episode, const FString& Downer)
	{
		if (Episode.bDownOpen || Episode.bFinalCharged) return false;
		Episode.bDownOpen = true;
		Episode.DownerKey = Downer;
		AddScore(Journal, -1);
		return true;
	}
	static void Revive(FBrScoreEpisode& Episode)
	{
		Episode.bDownOpen = false;
		Episode.DownerKey.Reset();
	}
	/** Returns final-elimination credit only for an unresolved opponent down. */
	static FString Resolve(FBrQuestJournal& Journal, FBrScoreEpisode& Episode, bool bFinal)
	{
		if (Episode.bFinalCharged || (!bFinal && !Episode.bDownOpen)) return FString();
		const FString Credit = bFinal && Episode.bDownOpen ? Episode.DownerKey : FString();
		AddScore(Journal, -1);
		Revive(Episode);
		Episode.bFinalCharged = bFinal;
		return Credit;
	}
};
