#include "BrPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "BrCharacter.h"

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
	ForceNetUpdate();
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
	ForceNetUpdate();
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
	}
}

void ABrPlayerState::OverrideWith(APlayerState* PlayerState)
{
	Super::OverrideWith(PlayerState);
	if (const ABrPlayerState* Other = Cast<ABrPlayerState>(PlayerState))
	{
		RestoreLifeBookkeeping(FBrReconnectRules::Merge(GetLifeBookkeeping(), Other->GetLifeBookkeeping()));
		if (Other->Team >= 0) SetTeam(Other->Team, Other->TeamColor);
		DownsInflicted = FMath::Max(DownsInflicted, Other->DownsInflicted);
		// Retain signed score/quest history here. PreparePlayer replaces it from the
		// authoritative round ledger (including progress earned while disconnected).
	}
}
