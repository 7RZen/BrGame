#include "BrGameMode.h"
#include "BrCharacter.h"
#include "BrGameState.h"
#include "BrLifeComponent.h"
#include "BrPlayerState.h"
#include "BrLog.h"
#include "Engine/World.h"

FBrRoundRecord* ABrGameMode::ActiveQuestRecord(AController* Player)
{
	if (!HasAuthority() || !Player || !IsAdmitted(Player)) return nullptr;
	FBrRoundRecord* Record = RoundRecords.Find(RoundKey(Player));
	return Record && Record->ActiveController.Get() == Player ? Record : nullptr;
}

void ABrGameMode::PublishQuests(const FBrRoundRecord& Record)
{
	if (AController* Controller = Record.ActiveController.Get())
		if (ABrPlayerState* PS = Controller->GetPlayerState<ABrPlayerState>())
			PS->RestoreQuests(Record.Journal);
}

bool ABrGameMode::CanAcceptQuest(AController* Player, const FBrQuestOffer& Offer) const
{
	const ABrGameState* GS = GetGameState<ABrGameState>();
	const FBrRoundRecord* Record = Player ? RoundRecords.Find(RoundKey(Player)) : nullptr;
	const ABrCharacter* Pawn = Player ? Cast<ABrCharacter>(Player->GetPawn()) : nullptr;
	return HasAuthority() && IsAdmitted(Player) && GS && !GS->bLobby && !GS->bResults &&
		Record && Record->ActiveController.Get() == Player && !Record->Life.bEliminated &&
		Pawn && Pawn->GetLife()->IsAlive() && !AcceptedQuestOffers.Contains(Offer.Id) &&
		FBrQuestRules::CanAccept(Record->Journal, Offer);
}

bool ABrGameMode::AcceptQuest(AController* Player, const FBrQuestOffer& Offer)
{
	if (!CanAcceptQuest(Player, Offer)) return false;
	FBrRoundRecord* Record = ActiveQuestRecord(Player);
	if (!Record || !FBrQuestRules::Accept(Record->Journal, Offer)) return false;
	AcceptedQuestOffers.Add(Offer.Id);
	PublishQuests(*Record);
	UE_LOG(LogBr, Display, TEXT("Quest accepted: Player=%s Offer=%s Objective=%d Weapon=%s"),
		*Record->PlayerName, *Offer.Id.ToString(), static_cast<int32>(Offer.Objective), *GetNameSafe(Offer.Weapon));
	return true;
}

void ABrGameMode::ProgressQuest(AController* Player, EBrQuestObjective Event, const UBrWeaponDefinition* Weapon)
{
	const ABrGameState* GS = GetGameState<ABrGameState>();
	FBrRoundRecord* Record = ActiveQuestRecord(Player);
	if (!GS || GS->bLobby || GS->bResults || !Record || Record->Life.bEliminated) return;
	if (FBrQuestRules::Progress(Record->Journal, Event, Weapon) > 0) PublishQuests(*Record);
}

FString ABrGameMode::CaptureOpponentDowner(const FBrDamageSource& Source, const ABrCharacter* Victim, bool bZone) const
{
	const ABrPlayerState* PS = Victim ? Victim->GetPlayerState<ABrPlayerState>() : nullptr;
	const ABrGameState* GS = GetGameState<ABrGameState>();
	const FBrRoundRecord* Downer = RoundRecords.Find(Source.RoundKey);
	if (!HasAuthority() || !GS || GS->bLobby || GS->bResults || bZone || !PS || !Source.IsValid() ||
		!Downer || Downer->PlayerId != Source.PlayerId || Source.IsSelf(PS->GetPlayerId()) ||
		(GS->MatchSettings.Mode == EBrMode::Grouped && Source.IsFriendly(PS->Team))) return FString();
	return Source.RoundKey;
}

void ABrGameMode::CommitQuestDown(AController* Victim, const FString& DownerKey)
{
	FBrRoundRecord* Record = ActiveQuestRecord(Victim);
	if (!Record || !FBrQuestRules::Down(Record->Journal, Record->ScoreEpisode, DownerKey)) return;
	PublishQuests(*Record);
	if (FBrRoundRecord* Downer = RoundRecords.Find(DownerKey))
	{
		if (FBrQuestRules::Progress(Downer->Journal, EBrQuestObjective::DownOpponent) > 0)
			PublishQuests(*Downer);
	}
}

void ABrGameMode::ClearQuestDown(AController* Player)
{
	if (FBrRoundRecord* Record = ActiveQuestRecord(Player)) FBrQuestRules::Revive(Record->ScoreEpisode);
}

void ABrGameMode::AwardPlantScore(AController* Player)
{
	const ABrGameState* GS = GetGameState<ABrGameState>();
	FBrRoundRecord* Record = ActiveQuestRecord(Player);
	if (!GS || GS->bLobby || GS->bResults || !Record || Record->Life.bEliminated)
	{
		UE_LOG(LogBr, Error, TEXT("Plant score rejected: missing active round ledger."));
		return;
	}
	FBrQuestRules::AddScore(Record->Journal, 1);
	PublishQuests(*Record); // Update ledger first, then replicated journal, in every match mode.
}

void ABrGameMode::ResolveQuestRecord(FBrRoundRecord& Record, bool bFinal)
{
	const FString Credit = FBrQuestRules::Resolve(Record.Journal, Record.ScoreEpisode, bFinal);
	PublishQuests(Record);
	if (FBrRoundRecord* Downer = RoundRecords.Find(Credit))
	{
		FBrQuestRules::AddScore(Downer->Journal, 1);
		PublishQuests(*Downer);
	}
}

void ABrGameMode::ResolveQuestLife(AController* Player, bool bFinal)
{
	if (FBrRoundRecord* Record = ActiveQuestRecord(Player)) ResolveQuestRecord(*Record, bFinal);
}
