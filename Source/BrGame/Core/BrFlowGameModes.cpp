#include "BrFlowGameModes.h"
#include "BrSafeZone.h"
#include "BrPickupManager.h"
#include "BrFlowRules.h"
#include "BrGameInstance.h"
#include "BrSessionSubsystem.h"
#include "BrGameState.h"
#include "BrPlayerState.h"
#include "BrPlayerController.h"
#include "BrCharacter.h"
#include "BrLifeComponent.h"
#include "BrDeveloperSettings.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "BrDevCheatPolicy.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameSession.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/LocalPlayer.h"
#include "UnrealClient.h"

ABrLobbyGameMode::ABrLobbyGameMode()
{
	DefaultPawnClass = nullptr;
	HUDClass = AHUD::StaticClass();
	bUseSeamlessTravel = true;
}
ABrMenuGameMode::ABrMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ABrPlayerController::StaticClass();
	HUDClass = AHUD::StaticClass();
}
void ABrGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	const UBrSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UBrSessionSubsystem>();
	// Bind before Super/admission/duplicate checks ever use a claimed ID.
	if (!Sessions || !Sessions->VerifyLoginIdentity(FindLoginConnection(Address), UniqueId))
	{
		ErrorMessage = TEXT("Steam login identity does not match the authenticated connection.");
		return;
	}
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (ErrorMessage.IsEmpty() && (!Sessions || !Sessions->ApprovePassword(UGameplayStatics::ParseOption(Options, TEXT("BrProof")))))
	{
		ErrorMessage = TEXT("Incorrect or missing server password.");
	}
	const ABrGameState* GS = GetGameState<ABrGameState>();
	if (ErrorMessage.IsEmpty() && GS && !FBrFlowRules::Admit(GS->PlayerArray.Num(), GS->MatchSettings.MaxPlayers))
	{
		ErrorMessage = TEXT("Server is full.");
	}
	if (ErrorMessage.IsEmpty() && GS && UniqueId.IsValid())
	{
		for (const TWeakObjectPtr<AController>& Admitted : AdmittedControllers)
		{
			if (const ABrPlayerState* PS = Admitted.IsValid() ? Admitted->GetPlayerState<ABrPlayerState>() : nullptr)
			{
				if (PS->GetUniqueId() == UniqueId) { ErrorMessage = TEXT("Identity already connected."); break; }
			}
		}
	}
}
UNetConnection* ABrGameMode::FindLoginConnection(const FString& Address) const
{
	const UNetDriver* Driver = GetWorld()->GetNetDriver();
	UNetConnection* Found = nullptr;
	if (Driver)
	{
		for (UNetConnection* Connection : Driver->ClientConnections)
		{
			if (Connection && Connection->GetConnectionState() != USOCK_Closed &&
				Connection->LowLevelGetRemoteAddress() == Address)
			{
				// PreLoginAsync receives an engine-supplied transport address, not a
				// connection pointer. Ambiguity must fail closed, not pick by claimed ID.
				if (Found)
				{
					return nullptr;
				}
				Found = Connection;
			}
		}
	}
	return Found;
}
void ABrGameMode::PreLoginAsync(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, const FOnPreLoginCompleteDelegate& OnComplete)
{
	FString Error;
	PreLogin(Options, Address, UniqueId, Error);
	const UBrSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UBrSessionSubsystem>();
	const ABrGameState* GS = GetGameState<ABrGameState>();
	if (!Error.IsEmpty() || !Sessions || !Sessions->IsSteam() || (GS && GS->MatchSettings.Visibility == EBrSessionVisibility::Public))
	{
		OnComplete.ExecuteIfBound(Error);
		return;
	}
	UNetConnection* Connection = FindLoginConnection(Address);
	if (Sessions->IsSteamLobbyMember(Connection, UniqueId))
	{
		OnComplete.ExecuteIfBound(FString());
		return;
	}
	FPendingSteamLogin Pending;
	Pending.Connection = Connection;
	Pending.Id = UniqueId;
	Pending.Options = Options;
	Pending.Address = Address;
	Pending.Complete = OnComplete;
	Pending.Started = FPlatformTime::Seconds();
	PendingSteamLogins.Add(MoveTemp(Pending));
	if (!GetWorldTimerManager().IsTimerActive(SteamLoginTimer))
	{
		GetWorldTimerManager().SetTimer(SteamLoginTimer, this, &ABrGameMode::ServiceSteamLogins, 0.1f, true);
	}
}
void ABrGameMode::ServiceSteamLogins()
{
	const UBrSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UBrSessionSubsystem>();
	const ABrGameState* GS = GetGameState<ABrGameState>();
	for (int32 I = PendingSteamLogins.Num() - 1; I >= 0; --I)
	{
		FPendingSteamLogin& Pending = PendingSteamLogins[I];
		UNetConnection* Connection = Pending.Connection.Get();
		FString Error;
		const bool bVerified = Connection && Sessions && Sessions->VerifyLoginIdentity(Connection, Pending.Id) &&
			Connection->PlayerId == Pending.Id && FindLoginConnection(Pending.Address) == Connection;
		const auto Decision = FBrSessionRules::LobbyAdmission(true, GS ? GS->MatchSettings.Visibility : EBrSessionVisibility::InviteOnly,
			bVerified, bVerified && Sessions->IsSteamLobbyMember(Connection, Pending.Id), FPlatformTime::Seconds() - Pending.Started);
		if (Decision == FBrSessionRules::EAdmission::Wait)
		{
			continue;
		}
		if (Decision == FBrSessionRules::EAdmission::Reject)
		{
			Error = TEXT("Steam lobby membership required or identity no longer valid. Join through Steam and retry.");
		}
		else
		{
			PreLogin(Pending.Options, Pending.Address, Pending.Id, Error); // Recheck password, cap and duplicates after the wait.
		}
		FOnPreLoginCompleteDelegate Complete = Pending.Complete;
		PendingSteamLogins.RemoveAtSwap(I);
		Complete.ExecuteIfBound(Error);
	}
	if (PendingSteamLogins.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(SteamLoginTimer);
	}
}
void ABrMenuGameMode::ChangeName(AController* Controller, const FString& NewName, bool bNameChange)
{
	Super::ChangeName(Controller, FBrFlowRules::SanitizeName(NewName), bNameChange);
}
void ABrGameMode::ChangeName(AController* Controller, const FString& NewName, bool bNameChange)
{
	if (!HasAuthority() || !Controller)
	{
		return;
	}
	if (!IsLobbyMode() && (IsAdmitted(Controller) || RoundRecords.Contains(RoundKey(Controller))))
	{
		return;
	}
	Super::ChangeName(Controller, FBrFlowRules::SanitizeName(NewName), bNameChange);
}
bool ABrGameMode::IsAdmitted(const AController* Controller) const
{
	for (const TWeakObjectPtr<AController>& Admitted : AdmittedControllers)
	{
		if (Admitted.IsValid() && Admitted.Get() == Controller)
		{
			return true;
		}
	}
	return false;
}
bool ABrGameMode::AdmitController(APlayerController* Player)
{
	if (!Player || !Player->GetPlayerState<ABrPlayerState>())
	{
		return false;
	}
	if (IsAdmitted(Player))
	{
		return true;
	}
	const FString Key = RoundKey(Player);
	bool bDuplicate = Key.IsEmpty();
	for (const TWeakObjectPtr<AController>& Admitted : AdmittedControllers)
	{
		if (Admitted.IsValid() && Admitted.Get() != Player && RoundKey(Admitted.Get()) == Key)
		{
			bDuplicate = true;
		}
	}
	if (const FBrRoundRecord* Record = RoundRecords.Find(Key))
	{
		if (Record->ActiveController.IsValid() && Record->ActiveController.Get() != Player)
		{
			bDuplicate = true;
		}
	}
	if (bDuplicate)
	{
		UE_LOG(LogBr, Warning, TEXT("Flow admission rejected: invalid/duplicate identity for %s"), *GetNameSafe(Player));
		if (GameSession)
		{
			GameSession->KickPlayer(Player, FText::FromString(TEXT("Invalid or already connected identity.")));
		}
		return false;
	}
	AdmittedControllers.Add(Player);
	return true;
}
void ABrGameMode::PostSeamlessTravel()
{
	// Register the entire travelling roster BEFORE Super initializes only the loaded clients.
	// Do not reset or spawn any of these carried PlayerStates in this snapshot phase.
	if (!IsLobbyMode())
	{
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			const ABrPlayerState* PS = PC ? PC->GetPlayerState<ABrPlayerState>() : nullptr;
			const FString Key = RoundKey(PC);
			if (!PS || Key.IsEmpty() || RoundRecords.Contains(Key))
			{
				continue;
			}
			FBrRoundRecord Pending;
			Pending.Team = PS->Team;
			Pending.TeamColor = PS->TeamColor;
			Pending.PlayerId = PS->GetPlayerId();
			Pending.PlayerName = FBrFlowRules::SanitizeName(PS->GetPlayerName());
			Pending.ActiveController = PC;
			Pending.bAwaitingInitialization = true;
			Pending.bMatchStartContestant = true;
			Pending.ReconnectDeadline = GetWorld()->GetTimeSeconds() + ReconnectGrace();
			RoundRecords.Add(Key, Pending);
			Participants.Add(InitialMatchSettings.Mode == EBrMode::Grouped ? Pending.Team : Pending.PlayerId);
			UE_LOG(LogBr, Display, TEXT("Flow travel pending: %s Deadline=%.2f"), *Pending.PlayerName, Pending.ReconnectDeadline);
		}
	}
	Super::PostSeamlessTravel();
}
bool ABrGameMode::MustSpectate_Implementation(APlayerController* Player) const
{
	// Results-only spectators become ordinary lobby participants. This also makes the
	// engine's pre-seamless-travel counters include them before InitSeamless resets the PS.
	return !IsLobbyMode() && Super::MustSpectate_Implementation(Player);
}
bool ABrGameMode::CanSpectate_Implementation(APlayerController* Viewer, APlayerState* ViewTarget)
{
	// Inherited spectator RPCs cannot select a different view outside our authoritative target.
	const ABrPlayerController* PC = Cast<ABrPlayerController>(Viewer);
	return PC && IsAdmitted(PC) && PC->IsFlowSpectating() && ViewTarget && PC->GetSpectatorTarget() == ViewTarget;
}
void ABrGameMode::InitSeamlessTravelPlayer(AController* Controller)
{
	const UBrSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UBrSessionSubsystem>();
	if (!Sessions || !Sessions->VerifyPlayerIdentity(Controller))
	{
		UE_LOG(LogBr, Warning, TEXT("Seamless player rejected: no authenticated identity binding."));
		if (GameSession)
		{
			GameSession->KickPlayer(Cast<APlayerController>(Controller), FText::FromString(TEXT("Steam identity binding lost. Rejoin.")));
		}
		return;
	}
	if (ABrPlayerState* PS = Controller->GetPlayerState<ABrPlayerState>())
	{
		PS->ResetRound();
		PS->SetIsOnlyASpectator(false);
	}
	Super::InitSeamlessTravelPlayer(Controller);
}
APlayerController* ABrGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	const UBrSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UBrSessionSubsystem>();
	UNetConnection* Connection = Cast<UNetConnection>(NewPlayer);
	const ULocalPlayer* Local = Cast<ULocalPlayer>(NewPlayer);
	const bool bLocalHost = Local && Local->GetGameInstance() == GetGameInstance();
	if (!Sessions || !Sessions->VerifyLoginIdentity(Connection, UniqueId, bLocalHost))
	{
		ErrorMessage = TEXT("Steam login identity does not match the authenticated connection.");
		return nullptr;
	}
	const ABrGameState* GS = GetGameState<ABrGameState>();
	if (Sessions->IsSteam() && !bLocalHost &&
		(!GS || GS->MatchSettings.Visibility != EBrSessionVisibility::Public) &&
		!Sessions->IsSteamLobbyMember(Connection, UniqueId))
	{
		ErrorMessage = TEXT("Steam lobby membership required. Join through Steam and retry.");
		return nullptr;
	}
	// GameModeBase::Login calls InitNewPlayer BEFORE World::SpawnPlayActor calls
	// PC::SetPlayer. The new PC has no connection/local-player association yet.
	// Scope the real engine-provided UPlayer through this synchronous call instead.
	TGuardValue<TWeakObjectPtr<UPlayer>> SourceGuard(LoginSource, NewPlayer);
	return Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
}
FString ABrGameMode::InitNewPlayer(APlayerController* Player, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	UBrSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UBrSessionSubsystem>();
	const ULocalPlayer* Local = Cast<ULocalPlayer>(LoginSource.Get());
	const bool bLocalHost = Local && Local->GetGameInstance() == GetGameInstance();
	if (!Player || !Sessions || !Sessions->BindPlayerIdentity(Player, Cast<UNetConnection>(LoginSource.Get()), UniqueId, bLocalHost))
	{
		return TEXT("Steam login identity does not match the authenticated connection.");
	}
	if (UGameplayStatics::GetIntOption(Options, TEXT("SpectatorOnly"), 0) != 0)
	{
		return TEXT("Players may only spectate after elimination or during results.");
	}
	const FString Error = Super::InitNewPlayer(Player, UniqueId, Options, Portal);
	if (Error.IsEmpty() && Player && Player->PlayerState)
	{
		const FString Name = UGameplayStatics::ParseOption(Options, TEXT("Name"));
		if (!Name.IsEmpty())
		{
			ChangeName(Player, Name, true);
		}
	}
	return Error;
}
TArray<int32> ABrGameMode::GroupSizes(const ABrPlayerState* Ignore) const
{
	TArray<int32> Sizes;
	const ABrGameState* GS = GetGameState<ABrGameState>();
	if (!GS)
	{
		return Sizes;
	}
	Sizes.Init(0, GS->MatchSettings.NumGroups);
	for (APlayerState* Entry : GS->PlayerArray)
	{
		if (const ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
		{
			if (PS != Ignore && Sizes.IsValidIndex(PS->Team))
			{
				++Sizes[PS->Team];
			}
		}
	}
	return Sizes;
}
void ABrGameMode::AssignPlayer(APlayerController* Player)
{
	ABrGameState* GS = GetGameState<ABrGameState>();
	ABrPlayerState* PS = Player ? Player->GetPlayerState<ABrPlayerState>() : nullptr;
	if (!GS || !PS)
	{
		return;
	}
	PS->bHost = Player->IsLocalController();
	if (GS->MatchSettings.Mode == EBrMode::AllVsAll)
	{
		PS->SetTeam(INDEX_NONE, FBrFlowRules::MilitaryGreen());
	}
	else
	{
		ColorChoosers.SetNum(GS->MatchSettings.NumGroups);
		if (PS->Team < 0 || PS->Team >= GS->MatchSettings.NumGroups)
		{
			PS->Team = FBrFlowRules::Smallest(GroupSizes(PS));
		}
		if (GS->GroupColors[PS->Team] == INDEX_NONE)
		{
			GS->GroupColors[PS->Team] = FBrFlowRules::AllocateColor(GS->GroupColors);
			ColorChoosers[PS->Team] = Player;
		}
		PS->SetTeam(PS->Team, FBrFlowRules::Palette(GS->GroupColors[PS->Team]));
	}
	UE_LOG(LogBr, Display, TEXT("Flow admitted: %s Team=%d Lobby=%d Results=%d"),
		*PS->GetPlayerName(), PS->Team, IsLobbyMode(), GS->bResults);
}
void ABrGameMode::UpdateLobby()
{
	ABrGameState* GS = GetGameState<ABrGameState>();
	if (!GS || !IsLobbyMode() || bTravelling || NumTravellingPlayers > 0)
	{
		return;
	}
	const FBrMatchSettings& Settings = GS->MatchSettings;
	if (Settings.Mode == EBrMode::AllVsAll)
	{
		GS->GroupColors.Init(INDEX_NONE, Settings.NumGroups);
		ColorChoosers.Empty();
		for (APlayerState* Entry : GS->PlayerArray)
		{
			if (ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
			{
				PS->bCanChooseColor = false;
				PS->SetTeam(INDEX_NONE, FBrFlowRules::MilitaryGreen());
			}
		}
	}
	else
	{
		TArray<int32> Existing;
		for (APlayerState* Entry : GS->PlayerArray)
		{
			const ABrPlayerState* PS = Cast<ABrPlayerState>(Entry);
			Existing.Add(PS ? PS->Team : INDEX_NONE);
		}
		TArray<int32> Assignments = FBrFlowRules::Rebalance(MoveTemp(Existing), Settings.NumGroups);
		TArray<int32> Sizes;
		Sizes.Init(0, Settings.NumGroups);
		for (int32 I = 0; I < GS->PlayerArray.Num(); ++I)
		{
			if (ABrPlayerState* PS = Cast<ABrPlayerState>(GS->PlayerArray[I]))
			{
				if (Settings.TeamSelection == EBrTeamSelection::AutoEven)
				{
					PS->Team = Assignments[I];
				}
				if (!Sizes.IsValidIndex(PS->Team) || !FBrFlowRules::HasRoom(Sizes[PS->Team], Settings.MaxPlayers, Settings.NumGroups))
				{
					PS->Team = FBrFlowRules::Smallest(Sizes);
				}
				++Sizes[PS->Team];
			}
		}
		const int32 OldCount = GS->GroupColors.Num();
		GS->GroupColors.SetNum(Settings.NumGroups);
		for (int32 I = OldCount; I < GS->GroupColors.Num(); ++I)
		{
			GS->GroupColors[I] = INDEX_NONE;
		}
		ColorChoosers.SetNum(Settings.NumGroups);
		FBrFlowRules::ReleaseEmpty(GS->GroupColors, Sizes);
		for (int32 I = 0; I < Sizes.Num(); ++I)
		{
			if (!Sizes[I])
			{
				ColorChoosers[I].Reset();
			}
		}
		for (APlayerState* Entry : GS->PlayerArray)
		{
			if (ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
			{
				if (GS->GroupColors[PS->Team] == INDEX_NONE)
				{
					GS->GroupColors[PS->Team] = FBrFlowRules::AllocateColor(GS->GroupColors);
					ColorChoosers[PS->Team] = Cast<AController>(PS->GetOwner());
				}
				AController* Controller = Cast<AController>(PS->GetOwner());
				PS->bCanChooseColor = FBrFlowRules::CanChooseColor(IsAdmitted(Controller) &&
					ColorChoosers[PS->Team].Get() == Controller, true);
				PS->SetTeam(PS->Team, FBrFlowRules::Palette(GS->GroupColors[PS->Team]));
			}
		}
	}
	GS->ForceNetUpdate();
}
void ABrGameMode::ChangeSettings(APlayerController* Requester, FBrMatchSettings Settings)
{
	if (!Requester || !Requester->IsLocalController() || !HasAuthority() || !IsLobbyMode() || bTravelling)
	{
		return;
	}
	if (!UBrDeveloperSettings::Get()->IsMapSelectionValid(Settings.MapId) ||
		Settings.NumGroups < 2 || Settings.NumGroups > 1024 || Settings.MaxPlayers < 0 ||
		(Settings.MaxPlayers > 0 && Settings.MaxPlayers < GetGameState<ABrGameState>()->PlayerArray.Num()) ||
		!FMath::IsFinite(Settings.CrateDropInterval) ||
		(Settings.Mode != EBrMode::AllVsAll && Settings.Mode != EBrMode::Grouped) ||
		(Settings.TeamSelection != EBrTeamSelection::AutoEven && Settings.TeamSelection != EBrTeamSelection::PlayerChoice) ||
		(Settings.ReviveRule != EBrReviveRule::Anyone && Settings.ReviveRule != EBrReviveRule::TeammatesOnly))
	{
		UE_LOG(LogBr, Warning, TEXT("Rejected invalid lobby settings"));
		return;
	}
	Settings.Sanitize();
	Settings.ResolvedMapId = NAME_None; // Client requests cannot choose a Random outcome.
	if (Settings.Visibility != EBrSessionVisibility::Public && Settings.Visibility != EBrSessionVisibility::FriendsOnly &&
		Settings.Visibility != EBrSessionVisibility::InviteOnly)
	{
		return;
	}
	// Protection state comes only from the server's private credential, never an RPC flag.
	Settings.bPasswordProtected = !GetGameInstance()->GetSubsystem<UBrSessionSubsystem>()->ApprovePassword(FString());
	GetGameState<ABrGameState>()->SetMatchSettings(Settings);
	UpdateLobby();
}
void ABrGameMode::ChooseTeam(APlayerController* Requester, int32 Team, int32 Color)
{
	ABrGameState* GS = GetGameState<ABrGameState>();
	ABrPlayerState* PS = Requester ? Requester->GetPlayerState<ABrPlayerState>() : nullptr;
	if (!HasAuthority() || !IsLobbyMode() || !IsAdmitted(Requester) || bTravelling || !GS || !PS ||
		GS->MatchSettings.Mode != EBrMode::Grouped || !GS->GroupColors.IsValidIndex(Team))
	{
		return;
	}
	if (PS->Team != Team)
	{
		if (GS->MatchSettings.TeamSelection != EBrTeamSelection::PlayerChoice ||
			!FBrFlowRules::HasRoom(GroupSizes(PS)[Team], GS->MatchSettings.MaxPlayers, GS->MatchSettings.NumGroups))
		{
			return;
		}
		PS->Team = Team;
		UpdateLobby();
	}
	if (Color >= 0 && Color < 10 && ColorChoosers.IsValidIndex(Team) &&
		FBrFlowRules::CanChooseColor(ColorChoosers[Team].Get() == Requester, PS->Team == Team))
	{
		TArray<int32> Used = GS->GroupColors;
		Used[Team] = INDEX_NONE;
		GS->GroupColors[Team] = FBrFlowRules::AllocateColor(Used, Color);
		UpdateLobby();
	}
}
bool ABrGameMode::TravelFlow(const FString& Map)
{
	if (bTravelling)
	{
		return false;
	}
	if (Map.IsEmpty() || !FPackageName::DoesPackageExist(Map))
	{
		UE_LOG(LogBr, Error, TEXT("Flow destination missing: %s"), *Map);
		return false;
	}
	if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>())
	{
		GI->HostSettings = GetGameState<ABrGameState>()->MatchSettings;
		GI->GroupColors = GetGameState<ABrGameState>()->GroupColors;
		GI->ColorChoosers = ColorChoosers;
		GI->bCarrySettings = true;
	}
	bTravelling = GetWorld()->ServerTravel(Map, true);
	UE_LOG(LogBr, Display, TEXT("Flow ServerTravel: %s Accepted=%d"), *Map, bTravelling);
	return bTravelling;
}
void ABrGameMode::StartRound(APlayerController* Requester)
{
	if (!HasAuthority() || !Requester || !Requester->IsLocalController() || !IsLobbyMode() || bTravelling || NumTravellingPlayers > 0 ||
		GetGameState<ABrGameState>()->PlayerArray.IsEmpty())
	{
		return;
	}
	UpdateLobby();
	ABrGameState* GS = GetGameState<ABrGameState>();
	FBrMapEntry Chosen;
	FRandomStream Random(FMath::Rand());
	if (!UBrDeveloperSettings::Get()->ResolveMap(GS->MatchSettings.MapId, Random, Chosen))
	{
		UE_LOG(LogBr, Error, TEXT("Cannot start round: selected map '%s' is unavailable"), *GS->MatchSettings.MapId.ToString());
		if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>())
		{
			GI->FlowError = TEXT("Selected map is unavailable. Check Project Settings > BrGame > Maps.");
		}
		return;
	}
	FBrMatchSettings Settings = GS->MatchSettings;
	Settings.ResolvedMapId = Chosen.Id;
	GS->SetMatchSettings(Settings);
	UE_LOG(LogBr, Display, TEXT("Round map: Selection=%s Resolved=%s Map: %s"),
		*Settings.MapId.ToString(), *Chosen.Id.ToString(), *Chosen.DisplayName.ToString());
	TravelFlow(Chosen.Map.ToSoftObjectPath().GetLongPackageName());
}
void ABrGameMode::ReturnToLobby()
{
	TravelFlow(TEXT("/Game/BrGame/Maps/L_Lobby"));
}
void ABrGameMode::EliminatePlayer(APlayerController* Requester, int32 Index)
{
#if !UE_BUILD_SHIPPING
	if (!Requester || !FBrDevCheatPolicy::CanExecute(HasAuthority(), Requester->IsLocalController(), GetNetMode()) || IsLobbyMode())
	{
		return;
	}
	const ABrGameState* GS = GetGameState<ABrGameState>();
	if (GS && GS->PlayerArray.IsValidIndex(Index))
	{
		if (ABrCharacter* Pawn = Cast<ABrCharacter>(GS->PlayerArray[Index]->GetPawn()))
		{
			Pawn->GetLife()->Eliminate();
		}
	}
#endif
}
void ABrGameMode::CheckWinner()
{
	ABrGameState* GS = GetGameState<ABrGameState>();
	if (!GS || GS->bResults || IsLobbyMode() || bTravelling)
	{
		return;
	}
	for (const auto& Pair : RoundRecords)
	{
		if (Pair.Value.bAwaitingInitialization && !Pair.Value.bForfeited)
		{
			return;
		}
	}
	TArray<int32> StillIn;
	for (const auto& Pair : RoundRecords)
	{
		if (!Pair.Value.Life.bEliminated)
		{
			StillIn.Add(GS->MatchSettings.Mode == EBrMode::Grouped ? Pair.Value.Team : Pair.Value.PlayerId);
		}
	}
	int32 Winner;
	if (!FBrFlowRules::Evaluate(StillIn, Participants.Num(), UBrDeveloperSettings::Get()->bAllowSoloTesting, Winner))
	{
		return;
	}
	GS->bResults = true;
	if (IsValid(SafeZone)) { SafeZone->Stop(); SafeZone->Destroy(); SafeZone = nullptr; }
	if (IsValid(PickupManager))
	{
		PickupManager->ClearRoundPickups();
	}
	GS->WinnerText = Winner == INDEX_NONE ? TEXT("Draw") : FString::Printf(TEXT("Group %d wins"), Winner + 1);
	if (Winner != INDEX_NONE && GS->MatchSettings.Mode == EBrMode::Grouped)
	{
		GS->WinnerColor = FBrFlowRules::Palette(GS->GroupColors[Winner]);
	}
	GS->ResultsTable = TEXT("Player | Group | Downs | Deaths | Result | Score\n");
	for (const auto& Pair : RoundRecords)
	{
		const FBrRoundRecord& Record = Pair.Value;
		{
			if (GS->MatchSettings.Mode == EBrMode::AllVsAll && Record.PlayerId == Winner)
			{
				GS->WinnerText = Record.PlayerName + TEXT(" wins");
				GS->WinnerColor = Record.TeamColor;
			}
			GS->ResultsTable += FString::Printf(TEXT("%s | %d | %d | %d | %s | %d\n"), *Record.PlayerName,
				Record.Team + 1, Record.DownsInflicted, Record.Life.Deaths,
				Record.Life.bEliminated ? TEXT("Eliminated") : TEXT("Survived"), Record.Journal.Score);
		}
	}
	GS->ResultsEndTime = GS->GetServerWorldTimeSeconds() + FMath::Max(1.0f, UBrDeveloperSettings::Get()->ResultsDuration);
	GS->ForceNetUpdate();
	UE_LOG(LogBr, Display, TEXT("Flow Results: %s\n%s"), *GS->WinnerText, *GS->ResultsTable);
	GetWorldTimerManager().SetTimer(ResultsTimer, this, &ABrGameMode::ReturnToLobby,
		FMath::Max(1.0f, UBrDeveloperSettings::Get()->ResultsDuration), false);
}
void ABrGameMode::FlowService()
{
	if (IsLobbyMode())
	{
		UpdateLobby();
	}
	else
	{
		ABrGameState* State = GetGameState<ABrGameState>();
		if (State && !State->bResults && !bTravelling)
		{
			ServiceRoundRecords();
			for (const TWeakObjectPtr<AController>& Entry : AdmittedControllers)
			{
				AController* Controller = Entry.Get();
				const ABrPlayerState* PS = Controller ? Controller->GetPlayerState<ABrPlayerState>() : nullptr;
				if (PS && !PS->bEliminated && !PS->GetPawn() && CanRestart(Controller))
				{
					RestartPlayer(Controller);
				}
			}
		}
		CheckWinner();
		if (State && !bTravelling)
		{
			for (const TWeakObjectPtr<AController>& Entry : AdmittedControllers)
			{
				if (ABrPlayerController* PC = Cast<ABrPlayerController>(Entry.Get()))
				{
					PC->ServiceSpectator();
				}
			}
		}
	}
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("BrMapSelectorSmoke")))
	{
		ServiceMapSelectorSmoke();
		return;
	}
	UBrGameInstance* GI = GetGameInstance<UBrGameInstance>();
	if (!GI || GI->bSmokeFinished || !FParse::Param(FCommandLine::Get(), TEXT("BrFlowSmoke")))
	{
		return;
	}
	APlayerController* Host = GetWorld()->GetFirstPlayerController();
	if (!Host || !Host->IsLocalController())
	{
		return;
	}
	ABrGameState* GS = GetGameState<ABrGameState>();
	if (IsLobbyMode())
	{
		FString MapId;
		if (FParse::Value(FCommandLine::Get(), TEXT("BrSmokeMapId="), MapId) && GS->MatchSettings.MapId != FName(*MapId))
		{
			FBrMatchSettings Settings = GS->MatchSettings;
			Settings.MapId = FName(*MapId);
			ChangeSettings(Host, Settings);
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("BrMapsShot")))
		{
			if (GetWorld()->GetTimeSeconds() > 5 && ShotSmokeStage == 0)
			{
				UE_LOG(LogBr, Display, TEXT("Shot smoke capture: Name=lobby"));
				FScreenshotRequest::RequestScreenshot(TEXT("Maps_lobby"), true, false);
				++ShotSmokeStage;
			}
			else if (GetWorld()->GetTimeSeconds() > 7 && ShotSmokeStage == 1)
			{
				UE_LOG(LogBr, Display, TEXT("SHOT SMOKE PASS: lobby map selector requested"));
				++ShotSmokeStage;
			}
			return;
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("BrMenuSmoke")))
	{
		ServiceMenuSmoke();
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")))
	{
		ServiceMatchSmoke();
		return;
	}
	const int32 SmokePlayers = FParse::Param(FCommandLine::Get(), TEXT("BrFogSpectatorSmoke")) ||
		(FParse::Param(FCommandLine::Get(), TEXT("BrFogSmoke")) && GS->MatchSettings.Mode == EBrMode::Grouped) ? 3 : 2;
	if (IsLobbyMode() && GS->PlayerArray.Num() >= SmokePlayers)
	{
		StartRound(Host);
	}
	if (!IsLobbyMode() && FParse::Param(FCommandLine::Get(), TEXT("BrFogSpectatorSmoke")))
	{
		ServiceFogSpectatorSmoke();
		return;
	}
	if (!IsLobbyMode() && FParse::Param(FCommandLine::Get(), TEXT("BrFogSmoke")))
	{
		ServiceFogSmoke();
		return;
	}
	if (!IsLobbyMode() && FParse::Param(FCommandLine::Get(), TEXT("BrShotSmoke")))
	{
		ServiceShotSmoke();
		return;
	}
	if (!IsLobbyMode() && FParse::Param(FCommandLine::Get(), TEXT("BrZoneSmoke")))
	{
		if (GS->bResults)
		{
			GI->bSmokeFinished = true;
		}
		return; // Zone smoke must finish through damage/timeouts, never the flow elimination shortcut.
	}
	if (!IsLobbyMode() && FParse::Param(FCommandLine::Get(), TEXT("BrFlowDisconnectSmoke")) &&
		GS->PlayerArray.Num() >= 2 && GetWorld()->GetTimeSeconds() - MatchBeganTime > 2)
	{
		for (APlayerState* Entry : GS->PlayerArray)
		{
			if (const ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
			{
				if (!PS->bHost)
				{
					if (ABrCharacter* Character = Cast<ABrCharacter>(PS->GetPawn()))
					{
						GI->bSmokeFinished = true;
						Character->GetLife()->Down(); // Client leaves when this replicated downed state arrives.
					}
				}
			}
		}
		return;
	}
	if (!IsLobbyMode() && GS->PlayerArray.Num() >= 3 && GetWorld()->GetTimeSeconds() - MatchBeganTime > 20)
	{
		GI->bSmokeFinished = true;
		for (int32 I = GS->PlayerArray.Num() - 1; I >= 0; --I)
		{
			if (const ABrPlayerState* PS = Cast<ABrPlayerState>(GS->PlayerArray[I]))
			{
				if (!PS->bHost)
				{
					EliminatePlayer(Host, I);
				}
			}
		}
	}
#endif
}
#if !UE_BUILD_SHIPPING
void ABrGameMode::ServiceMapSelectorSmoke()
{
	UBrGameInstance* GI = GetGameInstance<UBrGameInstance>();
	ABrPlayerController* Host = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	ABrGameState* GS = GetGameState<ABrGameState>();
	if (!GI || !GS || !Host || !FBrDevCheatPolicy::CanExecute(HasAuthority(), Host->IsLocalController(), GetNetMode()))
	{
		return;
	}
	int32& Stage = GI->MapSelectorSmokeStage;
	FString Requested;
	if (!FParse::Value(FCommandLine::Get(), TEXT("BrSmokeMapId="), Requested))
	{
		Requested = TEXT("TestArena");
	}
	const FName Expected(*Requested);
	auto Fail = [&Stage](const TCHAR* Reason)
	{
		UE_LOG(LogBr, Error, TEXT("Map selector smoke failed: %s"), Reason);
		Stage = -1;
	};
	if (Stage == 0 && IsLobbyMode())
	{
		FBrMatchSettings Settings = GS->MatchSettings;
		Settings.MapId = Expected;
		Host->ServerSettings(Settings); // Exercise the real host RPC handler exactly once.
		if (GS->MatchSettings.MapId != Expected) { Fail(TEXT("initial selection rejected")); return; }
		Stage = 1;
		UE_LOG(LogBr, Display, TEXT("Map selector smoke selected before join: Selection=%s"), *Expected.ToString());
	}
	else if ((Stage == 1 || Stage == 4) && IsLobbyMode() &&
		GS->PlayerArray.Num() >= 3 && NumTravellingPlayers == 0 && GetWorld()->GetTimeSeconds() - MatchBeganTime > 4)
	{
		// In round 1, the script connects player 3 only AFTER observing player 2's
		// initial lobby replication. Round 2 never reapplies or repairs settings.
		if (GS->MatchSettings.MapId != Expected) { Fail(TEXT("selection lost before Start")); return; }
		StartRound(Host);
		if (bTravelling)
		{
			++Stage;
		}
	}
	else if ((Stage == 2 || Stage == 5) && !IsLobbyMode() && !GS->bResults &&
		NumTravellingPlayers == 0 && GetWorld()->GetTimeSeconds() - MatchBeganTime > 5)
	{
		const FBrMapEntry* Resolved = UBrDeveloperSettings::Get()->FindMap(GS->MatchSettings.ResolvedMapId);
		if (GS->MatchSettings.MapId != Expected || !Resolved ||
			GetWorld()->GetMapName() != Resolved->Map.ToSoftObjectPath().GetAssetName() ||
			(Expected != TEXT("Random") && Resolved->Id != Expected))
		{
			Fail(TEXT("round selection/outcome/destination is not allowlisted or does not match")); return;
		}
		UE_LOG(LogBr, Display, TEXT("Map selector smoke match: Round=%d Selection=%s Resolved=%s Valid=1"),
			Stage == 2 ? 1 : 2, *Expected.ToString(), *Resolved->Id.ToString());
		for (int32 I = GS->PlayerArray.Num() - 1; I >= 0; --I)
		{
			if (const ABrPlayerState* PS = Cast<ABrPlayerState>(GS->PlayerArray[I]))
			{
				if (!PS->bHost)
				{
					EliminatePlayer(Host, I);
				}
			}
		}
		++Stage;
	}
	else if ((Stage == 3 || Stage == 6) && IsLobbyMode())
	{
		if (GS->MatchSettings.MapId != Expected || !GS->MatchSettings.ResolvedMapId.IsNone())
		{
			Fail(TEXT("production match -> lobby carry lost selection or retained outcome")); return;
		}
		UE_LOG(LogBr, Display, TEXT("Map selector smoke carried: Round=%d Selection=%s Resolved=None"),
			Stage == 3 ? 1 : 2, *Expected.ToString());
		if (Stage == 6) UE_LOG(LogBr, Display, TEXT("MAP SELECTOR SMOKE PASS: two rounds, retained selection, valid outcomes"));
		++Stage;
	}
}
#endif
void ABrGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(SteamLoginTimer);
	TArray<FPendingSteamLogin> Cancelled = MoveTemp(PendingSteamLogins);
	for (const FPendingSteamLogin& Pending : Cancelled)
	{
		Pending.Complete.ExecuteIfBound(TEXT("Server is travelling or shutting down. Retry joining."));
	}
	if (IsValid(SafeZone)) { SafeZone->Stop(); SafeZone->Destroy(); SafeZone = nullptr; }
	GetWorldTimerManager().ClearTimer(FlowTimer);
	GetWorldTimerManager().ClearTimer(ResultsTimer);
	Super::EndPlay(Reason);
}
