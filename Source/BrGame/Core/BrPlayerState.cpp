#include "BrPlayerState.h"
#include "BrDeveloperSettings.h"
#include "BrSoundEventRules.h"
#include "Net/UnrealNetwork.h"
#include "BrCharacter.h"
#include "BrArmorComponent.h"

void ABrPlayerState::SetTeam(int32 NewTeam, FLinearColor Color)
{
	if (!HasAuthority()) return;
	Team = NewTeam;
	TeamColor = Color;
	OnRep_Team();
	ForceNetUpdate();
}
void ABrPlayerState::OnRep_Team()
{
	if (ABrCharacter* Character = Cast<ABrCharacter>(GetPawn())) Character->ApplyTeamColor();
}
void ABrPlayerState::ResetRound()
{
	if (!HasAuthority()) return;
	DeathCount = VoluntaryRespawnCount = DownsInflicted = 0;
	RoundScore = 0;
	Quests.Reset();
	bEliminated = false;
	ResetArmor();
	ForceNetUpdate();
}

void ABrPlayerState::StoreArmorPlates(const TArray<float>& Values)
{
	if (HasAuthority()) ArmorPlates = bEliminated ? TArray<float>() : Values;
}

void ABrPlayerState::ResetArmor()
{
	if (!HasAuthority()) return;
	ArmorPlates.Reset();
	if (ABrCharacter* C = Cast<ABrCharacter>(GetPawn())) C->GetArmor()->Clear();
}

void ABrPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrPlayerState, DeathCount);
	DOREPLIFETIME(ABrPlayerState, VoluntaryRespawnCount);
	DOREPLIFETIME(ABrPlayerState, bEliminated);
	DOREPLIFETIME(ABrPlayerState, Team);
	DOREPLIFETIME(ABrPlayerState, TeamColor);
	DOREPLIFETIME(ABrPlayerState, bHost);
	DOREPLIFETIME(ABrPlayerState, bCanChooseColor);
	DOREPLIFETIME(ABrPlayerState, DownsInflicted);
	DOREPLIFETIME(ABrPlayerState, RoundScore);
	DOREPLIFETIME_CONDITION(ABrPlayerState, Quests, COND_OwnerOnly);
}

void ABrPlayerState::RestoreQuests(const FBrQuestJournal& Journal)
{
	if (!HasAuthority()) return;
	RoundScore = Journal.Score;
	Quests = Journal.Quests; // Exact ledger replacement, never a monotonic merge.
	ForceNetUpdate();
}

FBrLifeBookkeeping ABrPlayerState::GetLifeBookkeeping() const
{
	FBrLifeBookkeeping Life;
	Life.Deaths = DeathCount;
	Life.VoluntaryRespawns = VoluntaryRespawnCount;
	Life.bEliminated = bEliminated;
	return Life;
}

void ABrPlayerState::RestoreLifeBookkeeping(const FBrLifeBookkeeping& Life)
{
	if (!HasAuthority()) return;
	DeathCount = FMath::Max(0, Life.Deaths);
	VoluntaryRespawnCount = FMath::Max(0, Life.VoluntaryRespawns);
	bEliminated = Life.bEliminated;
	if (bEliminated) ResetArmor();
	ForceNetUpdate();
}

void ABrPlayerState::SetVoiceIndex(int32 Index)
{
	if (!HasAuthority()) return;
	const int32 Valid = FBrSoundEventRules::VoiceIndex(Index, UBrDeveloperSettings::Get()->VoiceSets.Num());
	if (Valid != VoiceIndex) LastDownLine.Reset();
	VoiceIndex = Valid;
}

void ABrPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (ABrPlayerState* Other = Cast<ABrPlayerState>(PlayerState))
	{
		Other->RestoreLifeBookkeeping(GetLifeBookkeeping());
		Other->SetTeam(Team, TeamColor);
		Other->bHost = bHost;
		Other->bCanChooseColor = bCanChooseColor;
		Other->DownsInflicted = DownsInflicted;
		Other->RoundScore = RoundScore;
		Other->Quests = Quests;
		Other->StoreArmorPlates(ArmorPlates);
		Other->SetVoiceIndex(VoiceIndex);
		Other->LastDownLine = LastDownLine;
	}
}

void ABrPlayerState::OverrideWith(APlayerState* PlayerState)
{
	Super::OverrideWith(PlayerState);
	if (const ABrPlayerState* Other = Cast<ABrPlayerState>(PlayerState))
	{
		RestoreLifeBookkeeping(FBrReconnectRules::Merge(GetLifeBookkeeping(), Other->GetLifeBookkeeping()));
		SetVoiceIndex(Other->VoiceIndex);
		LastDownLine = Other->LastDownLine;
		if (Other->Team >= 0) SetTeam(Other->Team, Other->TeamColor);
		DownsInflicted = FMath::Max(DownsInflicted, Other->DownsInflicted);
		// Retain signed score/quest history here. PreparePlayer replaces it from the
		// authoritative round ledger (including progress earned while disconnected).
	}
}
