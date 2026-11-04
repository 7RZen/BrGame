#include "BrGameState.h"
#include "BrLog.h"
#include "Net/UnrealNetwork.h"

void ABrGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrGameState, bFogDisabled);
	DOREPLIFETIME(ABrGameState, MatchSettings);
	DOREPLIFETIME(ABrGameState, bLobby);
	DOREPLIFETIME(ABrGameState, bResults);
	DOREPLIFETIME(ABrGameState, WinnerText);
	DOREPLIFETIME(ABrGameState, WinnerColor);
	DOREPLIFETIME(ABrGameState, ResultsTable);
	DOREPLIFETIME(ABrGameState, ResultsEndTime);
	DOREPLIFETIME(ABrGameState, GroupColors);
}

void ABrGameState::SetMatchSettings(const FBrMatchSettings& Settings)
{
	if (!HasAuthority())
	{
		return;
	}
	MatchSettings = Settings;
	OnRep_MatchSettings();
	ForceNetUpdate();
}

void ABrGameState::OnRep_MatchSettings()
{
	UE_LOG(LogBr, Display, TEXT("Match settings: Mode=%s Groups=%d MaxPlayers=%d CrateDropInterval=%.1fs"),
	       MatchSettings.Mode == EBrMode::Grouped ? TEXT("Grouped") : TEXT("AllVsAll"),
	       MatchSettings.NumGroups, MatchSettings.MaxPlayers, MatchSettings.CrateDropInterval);
	UE_LOG(LogBr, Display, TEXT("Map settings: Selection=%s Resolved=%s"), *MatchSettings.MapId.ToString(),
	       *MatchSettings.ResolvedMapId.ToString());
}
