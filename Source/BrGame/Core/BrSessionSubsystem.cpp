#include "BrSessionSubsystem.h"
#include "BrSessionRules.h"
#include "BrGameInstance.h"
#include "BrGameState.h"
#include "BrLog.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Online/OnlineSessionNames.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/NetConnection.h"
#include "GameFramework/PlayerState.h"
#include "SteamSocketsNetConnection.h"
#include "IPAddress.h"
#include "String/LexFromString.h"

THIRD_PARTY_INCLUDES_START
#include <steam/steam_api.h>
THIRD_PARTY_INCLUDES_END

namespace
{
uint64 ClaimedSteamId(const FUniqueNetIdRepl& Claimed)
{
	uint64 Id = 0;
	if (Claimed.IsValid() && Claimed.IsV1() && Claimed.GetV1()->GetType() == FName(TEXT("STEAM")))
		LexTryParseString(Id, *Claimed.GetV1()->ToString());
	return Id;
}
uint64 SteamTransportPeer(UNetConnection* Connection)
{
	// SteamSocketsNetDriver::OnConnectionCreated obtains this address from
	// FSteamSocket::GetPeerAddress -> GetConnectionInfo.m_identityRemote, NOT
	// NMT_Login's PlayerId/URL. Reject IP transports under the Steam policy.
	if (!Cast<USteamSocketsNetConnection>(Connection)) return 0;
	const TSharedPtr<const FInternetAddr> Address = Connection->GetRemoteAddr();
	if (!Address) return 0;
	const TArray<uint8> Raw = Address->GetRawIp();
	// IPAddressSteamSockets.cpp serializes SteamID type + uint64 in network order.
	if (Raw.Num() != 9 || Raw[0] != k_ESteamNetworkingIdentityType_SteamID) return 0;
	uint64 Id = 0;
	for (int32 I = 1; I < Raw.Num(); ++I) Id = (Id << 8) | Raw[I];
	return Id;
}
}

bool UBrSessionSubsystem::VerifyLoginIdentity(UNetConnection* Connection, const FUniqueNetIdRepl& Claimed, bool bLocalHost) const
{
	if (!bSteam) return true;
	uint64 Peer = SteamTransportPeer(Connection);
	if (bLocalHost && !Connection)
	{
		IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
		const IOnlineIdentityPtr Identity = OSS ? OSS->GetIdentityInterface() : nullptr;
		const FUniqueNetIdPtr LocalId = Identity ? Identity->GetUniquePlayerId(0) : nullptr;
		if (LocalId) LexTryParseString(Peer, *LocalId->ToString());
	}
	return FBrSessionRules::IdentityMatches(true, ClaimedSteamId(Claimed) != 0, ClaimedSteamId(Claimed), Peer);
}
bool UBrSessionSubsystem::VerifyPlayerIdentity(const AController* Player) const
{
	if (!bSteam) return true;
	const APlayerController* PC = Cast<APlayerController>(Player);
	const APlayerState* PS = PC ? PC->PlayerState : nullptr;
	const FUniqueNetIdRepl* Bound = VerifiedPlayers.Find(Player);
	return PS && Bound && *Bound == PS->GetUniqueId();
}
bool UBrSessionSubsystem::BindPlayerIdentity(APlayerController* Player, UNetConnection* Connection, const FUniqueNetIdRepl& Claimed, bool bLocalHost)
{
	if (!bSteam) return true;
	if (!Player || !Player->HasAuthority() || !VerifyLoginIdentity(Connection, Claimed, bLocalHost)) return false;
	VerifiedPlayers.Add(Player, Claimed);
	return true;
}
bool UBrSessionSubsystem::IsSteamLobbyMember(UNetConnection* Connection, const FUniqueNetIdRepl& Claimed) const
{
	if (!bSteam || !VerifyLoginIdentity(Connection, Claimed)) return false;
	const FNamedOnlineSession* Session = Sessions ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	ISteamMatchmaking* Matchmaking = SteamMatchmaking();
	ISteamUser* User = SteamUser();
	uint64 LobbyValue = 0;
	if (!Session || !Session->bHosting || !Session->SessionSettings.bUseLobbiesIfAvailable ||
		!Matchmaking || !User || !LexTryParseString(LobbyValue, *Session->GetSessionIdStr())) return false;
	const CSteamID Lobby(LobbyValue);
	if (!Lobby.IsLobby() || Matchmaking->GetLobbyOwner(Lobby) != User->GetSteamID()) return false;
	const uint64 Peer = SteamTransportPeer(Connection);
	// RegisteredPlayers is gameplay bookkeeping and is NOT proof of lobby membership.
	for (int32 I = 0; I < Matchmaking->GetNumLobbyMembers(Lobby); ++I)
		if (Matchmaking->GetLobbyMemberByIndex(Lobby, I).ConvertToUint64() == Peer) return true;
	return false;
}

void UBrSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
	bSteam = OSS && OSS->GetSubsystemName() == FName(TEXT("STEAM"));
	if (!OSS) OSS = Online::GetSubsystem(GetWorld(), TEXT("NULL"));
	if (OSS) Sessions = OSS->GetSessionInterface();
	if (!Sessions) { Error(TEXT("Online sessions unavailable; use IP join.")); return; }
	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::Created));
	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::Found));
	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::Joined));
	DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::Destroyed));
	UpdateHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(FOnUpdateSessionCompleteDelegate::CreateUObject(this, &ThisClass::Updated));
	InviteHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &ThisClass::Invited));
	FailureHandle = Sessions->AddOnSessionFailureDelegate_Handle(FOnSessionFailureDelegate::CreateUObject(this, &ThisClass::Failed));
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Service), 1.0f);
	UE_LOG(LogBr, Display, TEXT("Sessions ready: %s"), *Status());
}
void UBrSessionSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	if (Sessions)
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateHandle);
		Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
		Sessions->ClearOnSessionFailureDelegate_Handle(FailureHandle);
		if (Operation == EOperation::Finding) Sessions->CancelFindSessions();
		if (Sessions->GetNamedSession(NAME_GameSession)) Sessions->DestroySession(NAME_GameSession);
	}
	Sessions.Reset();
	VerifiedPlayers.Empty();
	HostPassword.Empty();
	JoinProof.Empty();
	Super::Deinitialize();
}
FString UBrSessionSubsystem::Status() const
{
	return bSteam ? TEXT("Steam (App ID from config) — Shift+Tab for invites") :
		TEXT("LAN/IP fallback — Steam unavailable or disabled (-nosteam). LAN browser only.");
}
void UBrSessionSubsystem::Error(const FString& Message)
{
	if (UBrGameInstance* GI = Cast<UBrGameInstance>(GetGameInstance())) GI->FlowError = Message;
	UE_LOG(LogBr, Warning, TEXT("Session: %s"), *Message);
}
void UBrSessionSubsystem::Begin(EOperation Value)
{
	Operation = Value;
	bTimedOut = false;
	OperationStarted = FPlatformTime::Seconds();
}
void UBrSessionSubsystem::Host() { Queue(EAction::Host); }
void UBrSessionSubsystem::Quit() { Queue(EAction::Quit); }
void UBrSessionSubsystem::Leave(const FString& Message)
{
	if (UBrGameInstance* GI = Cast<UBrGameInstance>(GetGameInstance())) GI->FlowError = Message;
	bPasswordPrompt = false;
	Queue(EAction::Menu);
}
void UBrSessionSubsystem::Queue(EAction Action)
{
	if (Pending != EAction::None && Action != EAction::Menu && Action != EAction::Quit) { Error(TEXT("Leaving previous session; please wait.")); return; }
	Pending = Action;
	if (Action != EAction::Join) bPasswordPrompt = false;
	bWantHost = false;
	// In-flight operations finish before destroying; no overlapping named-session operations.
	if (Operation != EOperation::Idle) return;
	Continue();
}
void UBrSessionSubsystem::Continue()
{
	if (Pending == EAction::None || !GetWorld()) return;
	if (Sessions && Sessions->GetNamedSession(NAME_GameSession))
	{
		Begin(EOperation::Destroying);
		if (!Sessions->DestroySession(NAME_GameSession)) Destroyed(NAME_GameSession, false);
		return;
	}
	UBrGameInstance* GI = CastChecked<UBrGameInstance>(GetGameInstance());
	if (Pending == EAction::Quit)
	{
		Pending = EAction::None;
		UKismetSystemLibrary::QuitGame(this, GI->GetFirstLocalPlayerController(), EQuitPreference::Quit, false);
		return;
	}
	if (!GetWorld()->GetMapName().Contains(TEXT("L_MainMenu")) && Pending != EAction::Host)
	{
		bAwaitMenu = true;
		GI->LeaveLocal(GI->FlowError);
		return;
	}
	bAwaitMenu = false;
	if (!GI->GetFirstLocalPlayerController()) { bAwaitMenu = true; return; }
	const EAction Action = Pending;
	Pending = EAction::None;
	if (Action == EAction::Host)
	{
		HostPassword.Empty();
		bWantHost = true;
		bHostAttempted = false;
		Advertisement = FBrAdvertisementState();
		bRecreatingNull = false;
		GI->HostLocal();
	}
	if (Action == EAction::IP) GI->JoinLocal(IPAddress, JoinProof);
	if (Action == EAction::IP) JoinProof.Empty();
	if (Action == EAction::Join)
	{
		bool bLocked = false;
		Selected.Session.SessionSettings.Get(TEXT("BRLOCK"), bLocked);
		if (bLocked && JoinProof.IsEmpty()) bPasswordPrompt = true;
		else JoinSelected();
	}
	if (Action == EAction::Menu) GI->LeaveLocal(GI->FlowError);
}
void UBrSessionSubsystem::Destroyed(FName Name, bool bSuccess)
{
	if (Name != NAME_GameSession || Operation != EOperation::Destroying) return;
	Begin(EOperation::Idle);
	UE_LOG(LogBr, Display, TEXT("Session destroyed: Success=%d"), bSuccess);
	if (bRecreatingNull)
	{
		if (!bSuccess)
		{
			PublicationComplete(false);
			// Destroy may have removed the named session before failing to repair
			// LAN status. Retain the repair intent and handle either state on retry.
			if (Pending == EAction::None) return;
			bRecreatingNull = false;
		}
		else
		{
			Advertisement.Published.Empty(); // Nothing advertised until recreation succeeds.
			if (Pending == EAction::None && bWantHost) { Create(); return; }
			bRecreatingNull = false;
			Continue();
			return;
		}
	}
	if (!bSuccess)
	{
		const bool bQuit = Pending == EAction::Quit;
		Pending = EAction::None;
		Error(TEXT("Could not leave online session. Return to menu and restart before joining again."));
		if (bQuit)
		{
			UKismetSystemLibrary::QuitGame(this, GetGameInstance()->GetFirstLocalPlayerController(), EQuitPreference::Quit, false);
			return;
		}
		CastChecked<UBrGameInstance>(GetGameInstance())->LeaveLocal(CastChecked<UBrGameInstance>(GetGameInstance())->FlowError);
		return;
	}
	Presence();
	Continue();
}
FOnlineSessionSettings UBrSessionSubsystem::Settings() const
{
	FOnlineSessionSettings Out;
	const ABrGameState* GS = GetWorld() ? GetWorld()->GetGameState<ABrGameState>() : nullptr;
	const FBrMatchSettings Match = GS ? GS->MatchSettings : FBrMatchSettings();
	const UBrGameInstance* GI = CastChecked<UBrGameInstance>(GetGameInstance());
	Out.bIsLANMatch = !bSteam;
	Out.bUsesPresence = bSteam;
	Out.bUseLobbiesIfAvailable = bSteam;
	Out.bAllowJoinInProgress = true;
	Out.NumPublicConnections = FBrSessionRules::Connections(Match.MaxPlayers, bSteam);
	FBrSessionRules::Visibility(Out, Match.Visibility);
	// Null has no friends/invite privacy primitive; do not broadcast private LAN sessions.
	if (!bSteam && Match.Visibility != EBrSessionVisibility::Public) Out.bShouldAdvertise = false;
	// Keep the OSS default engine build id: Null/Steam discard mismatches before
	// our callback. BRPROTO is the independent gameplay compatibility filter.
	const auto Advertise = EOnlineDataAdvertisementType::ViaOnlineServiceAndPing;
	Out.Set(TEXT("BRGAME"), FString(FBrSessionRules::GameId), Advertise);
	Out.Set(TEXT("BRPROTO"), FBrSessionRules::Protocol, Advertise);
	Out.Set(TEXT("BRNAME"), GI->PlayerName + TEXT("'s game"), Advertise);
	Out.Set(TEXT("BRLOCK"), !HostPassword.IsEmpty(), Advertise);
	Out.Set(TEXT("BRCOUNT"), GS ? GS->PlayerArray.Num() : 1, Advertise);
	Out.Set(TEXT("BRMAX"), Match.MaxPlayers, Advertise);
	Out.Set(TEXT("BRMODE"), Match.Mode == EBrMode::Grouped ? FString(TEXT("Grouped")) : FString(TEXT("All vs all")), Advertise);
	Out.Set(TEXT("BRPLAY"), GS && !GS->bLobby && !GS->bResults, Advertise);
	return Out;
}
void UBrSessionSubsystem::Create()
{
	bHostAttempted = true;
	if (!Sessions) { Error(TEXT("Session interface missing; IP hosting remains available.")); return; }
	FOnlineSessionSettings Value = Settings();
	AttemptedSettings = Value;
	Advertisement.Attempted = AdvertisementSignature(Value);
	Begin(EOperation::Creating);
	if (!Sessions->CreateSession(0, NAME_GameSession, Value)) Created(NAME_GameSession, false);
}
void UBrSessionSubsystem::Created(FName Name, bool bSuccess)
{
	if (Name != NAME_GameSession || Operation != EOperation::Creating) return;
	Begin(EOperation::Idle);
	UE_LOG(LogBr, Display, TEXT("Session created: Success=%d Steam=%d"), bSuccess, bSteam);
	const bool bWasRecreating = bRecreatingNull;
	if (bWasRecreating)
	{
		PublicationComplete(bSuccess);
		if (!bSuccess) { Continue(); return; } // Keep gameplay; bounded-backoff retry of missing session.
		bRecreatingNull = false;
		// Null Destroy/Create affects discovery only, not the live net driver. Restore
		// registered players so subsequent departures/capacity bookkeeping are correct.
		TArray<FUniqueNetIdRef> Players;
		if (const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>())
			for (const APlayerState* PS : GS->PlayerArray)
				if (PS && PS->GetUniqueId().IsValid() && PS->GetUniqueId().IsV1()) Players.Add(PS->GetUniqueId().GetV1().ToSharedRef());
		if (!Players.IsEmpty() && !Sessions->RegisterPlayers(NAME_GameSession, Players, false))
			Error(TEXT("LAN session recreated but player registration failed; rehost if capacity becomes stale."));
	}
	if (!bSuccess)
	{
		bWantHost = false;
		Leave(TEXT("Session creation failed. Check Steam login/network/relay connectivity; retry hosting."));
		return;
	}
	if (!bWasRecreating) PublicationComplete(true);
	Presence();
	Continue();
}
void UBrSessionSubsystem::Find()
{
	if (!Sessions || Operation != EOperation::Idle || Pending != EAction::None) { Error(TEXT("Sessions busy or unavailable.")); return; }
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = !bSteam;
	Search->MaxSearchResults = 200;
	Search->TimeoutInSeconds = 15;
	if (bSteam) Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	Search->QuerySettings.Set(TEXT("BRGAME"), FString(FBrSessionRules::GameId), EOnlineComparisonOp::Equals);
	Search->QuerySettings.Set(TEXT("BRPROTO"), FBrSessionRules::Protocol, EOnlineComparisonOp::Equals);
	Begin(EOperation::Finding);
	Error(TEXT("Searching for compatible BrGame sessions..."));
	if (!Sessions->FindSessions(0, Search.ToSharedRef())) Found(false);
}
void UBrSessionSubsystem::Found(bool bSuccess)
{
	if (Operation != EOperation::Finding) return;
	Begin(EOperation::Idle);
	Servers.Empty();
	if (bSuccess && Search)
		for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
			if (Result.IsValid() && FBrSessionRules::Compatible(Result.Session.SessionSettings)) Servers.Add(Result);
	++Revision;
	Error(bSuccess ? FString::Printf(TEXT("Found %d compatible BrGame sessions."), Servers.Num()) :
		TEXT("Server search failed. Check Steam/network, or use LAN/IP fallback."));
	UE_LOG(LogBr, Display, TEXT("Session search complete: Success=%d Compatible=%d"), bSuccess, Servers.Num());
	Continue();
}
FString UBrSessionSubsystem::Describe(int32 Index) const
{
	if (!Servers.IsValidIndex(Index)) return FString();
	const auto& Result = Servers[Index];
	const auto& Value = Result.Session.SessionSettings;
	FString Name, Mode;
	int32 Count = 0, Max = 0;
	bool Lock = false, Playing = false;
	Value.Get(TEXT("BRNAME"), Name); Value.Get(TEXT("BRMODE"), Mode);
	Value.Get(TEXT("BRCOUNT"), Count); Value.Get(TEXT("BRMAX"), Max);
	Value.Get(TEXT("BRLOCK"), Lock); Value.Get(TEXT("BRPLAY"), Playing);
	return FString::Printf(TEXT("%s%s | %d/%s | %s | %s | %s"), Lock ? TEXT("[LOCK] ") : TEXT(""),
		*Name, Count, Max ? *FString::FromInt(Max) : TEXT("unlimited"), *Mode,
		Playing ? TEXT("In progress") : TEXT("Lobby/results"),
		Result.PingInMs < 9999 ? *FString::Printf(TEXT("%d ms"), Result.PingInMs) : TEXT("Ping unavailable"));
}
void UBrSessionSubsystem::Select(int32 Index)
{
	if (!Servers.IsValidIndex(Index) || Operation != EOperation::Idle) return;
	Selected = Servers[Index];
	JoinProof.Empty();
	Queue(EAction::Join);
}
void UBrSessionSubsystem::SubmitPassword(const FString& Password)
{
	if (!bPasswordPrompt || !FBrSessionRules::ValidPassword(Password)) { Error(TEXT("Password must be at most 64 characters.")); return; }
	JoinProof = FBrSessionRules::PasswordProof(Password);
	bPasswordPrompt = false;
	JoinSelected();
}
void UBrSessionSubsystem::CancelPassword() { bPasswordPrompt = false; JoinProof.Empty(); Selected = FOnlineSessionSearchResult(); }
void UBrSessionSubsystem::JoinIP(const FString& Address, const FString& Password)
{
	if (!FBrSessionRules::ValidPassword(Password)) { Error(TEXT("Password must be at most 64 characters.")); return; }
	IPAddress = Address;
	JoinProof = FBrSessionRules::PasswordProof(Password);
	Queue(EAction::IP);
}
void UBrSessionSubsystem::JoinSelected()
{
	if (!Sessions || Operation != EOperation::Idle || !Selected.IsValid()) { Error(TEXT("Session unavailable; refresh the browser.")); return; }
	if (!FBrSessionRules::Compatible(Selected.Session.SessionSettings)) { Error(TEXT("Incompatible BrGame session. Update both games.")); return; }
	Begin(EOperation::Joining);
	if (!Sessions->JoinSession(0, NAME_GameSession, Selected)) Joined(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
}
void UBrSessionSubsystem::Joined(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
	if (Name != NAME_GameSession || Operation != EOperation::Joining) return;
	Begin(EOperation::Idle);
	if (Pending != EAction::None) { Continue(); return; }
	FString Address;
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions->GetResolvedConnectString(NAME_GameSession, Address))
	{
		Leave(FString::Printf(TEXT("Join failed (%s). Server full, offline, or Steam/relay unreachable."), LexToString(Result)));
		return;
	}
	UBrGameInstance* GI = CastChecked<UBrGameInstance>(GetGameInstance());
	GI->PrepareConnection();
	if (APlayerController* PC = GI->GetFirstLocalPlayerController())
		PC->ClientTravel(Address + TEXT("?Name=") + GI->PlayerName + TEXT("?BrProof=") + JoinProof, TRAVEL_Absolute);
	else Leave(TEXT("Join failed: local controller unavailable."));
	Presence();
	JoinProof.Empty();
}
void UBrSessionSubsystem::Invited(bool bSuccess, int32 ControllerId, FUniqueNetIdPtr User, const FOnlineSessionSearchResult& Result)
{
	if (!bSuccess || !Result.IsValid()) { Error(TEXT("Steam invitation unavailable or expired.")); return; }
	if (!FBrSessionRules::Compatible(Result.Session.SessionSettings)) { Error(TEXT("Invitation is not a compatible BrGame build.")); return; }
	if (Pending != EAction::None || bPasswordPrompt) { Error(TEXT("Invitation received while busy; accept it again after returning to menu.")); return; }
	Selected = Result;
	JoinProof.Empty();
	Queue(EAction::Join);
}
void UBrSessionSubsystem::Failed(const FUniqueNetId& User, ESessionFailure::Type Failure)
{
	Leave(TEXT("Online session lost. Check Steam login, NAT/firewall and relay connectivity."));
}
bool UBrSessionSubsystem::SetHostPassword(const FString& Password)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !FBrSessionRules::ValidPassword(Password)) return false;
	HostPassword = Password;
	return true;
}
bool UBrSessionSubsystem::ApprovePassword(const FString& Proof) const { return FBrSessionRules::ApprovePassword(HostPassword, Proof); }
void UBrSessionSubsystem::Updated(FName Name, bool bSuccess)
{
	if (Name != NAME_GameSession || Operation != EOperation::Updating) return;
	Begin(EOperation::Idle);
	PublicationComplete(bSuccess);
	UE_LOG(LogBr, Display, TEXT("Session updated: Success=%d"), bSuccess);
	Presence();
	Continue();
}
FString UBrSessionSubsystem::AdvertisementSignature(const FOnlineSessionSettings& Value)
{
	TArray<FName> Keys;
	Value.Settings.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());
	FString Signature = FString::Printf(TEXT("%d/%d/%d/%d/%d/%d"),
		Value.NumPublicConnections, Value.NumPrivateConnections, Value.bShouldAdvertise,
		Value.bAllowInvites, Value.bAllowJoinViaPresence, Value.bAllowJoinViaPresenceFriendsOnly);
	for (const FName Key : Keys)
	{
		const FString Text = Value.Settings[Key].Data.ToString();
		Signature += FString::Printf(TEXT("|%s:%d:%s"), *Key.ToString(), Text.Len(), *Text);
	}
	return Signature;
}
void UBrSessionSubsystem::PublicationComplete(bool bSuccess)
{
	Advertisement.Complete(bSuccess, FPlatformTime::Seconds());
	if (bSuccess)
	{
		PublishedSettings = AttemptedSettings;
		if (!bSteam) UE_LOG(LogBr, Display, TEXT("Session LAN visibility published: Public=%d"), PublishedSettings.bShouldAdvertise);
	}
	else Error(FString::Printf(TEXT("Session publication failed; retrying in %.0fs. Browser settings may be stale."),
		Advertisement.RetryAt - FPlatformTime::Seconds()));
}
void UBrSessionSubsystem::Publish(FOnlineSessionSettings Value)
{
	AttemptedSettings = Value;
	Advertisement.Attempted = AdvertisementSignature(Value);
	if (!bSteam && (bRecreatingNull || Value.bShouldAdvertise != PublishedSettings.bShouldAdvertise))
	{
		// Null UpdateSession only copies settings; its active beacon continues to
		// answer IsSessionJoinable (which treats bIsLANMatch as advertised).
		// Destroy/Create is the public OSS API that actually runs UpdateLANStatus.
		bRecreatingNull = true;
		Begin(EOperation::Destroying);
		if (!Sessions->DestroySession(NAME_GameSession)) Destroyed(NAME_GameSession, false);
		return;
	}
	Begin(EOperation::Updating);
	if (!Sessions->UpdateSession(NAME_GameSession, Value, true)) Updated(NAME_GameSession, false);
}
void UBrSessionSubsystem::Presence()
{
	if (!bSteam) return;
	IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
	const IOnlineIdentityPtr Identity = OSS ? OSS->GetIdentityInterface() : nullptr;
	const IOnlinePresencePtr OnlinePresence = OSS ? OSS->GetPresenceInterface() : nullptr;
	const FUniqueNetIdPtr Id = Identity ? Identity->GetUniquePlayerId(0) : nullptr;
	if (!Id || !OnlinePresence) { Error(TEXT("Steam presence unavailable; friends-list joining may not work.")); return; }
	// Steam's implementation clears previous keys and derives 'connect' from
	// NAME_GameSession, omitting it for invite-only sessions and after destroy.
	FOnlineUserPresenceStatus Value;
	Value.StatusStr = TEXT("BrGame");
	OnlinePresence->SetPresence(*Id, Value, IOnlinePresence::FOnPresenceTaskCompleteDelegate::CreateWeakLambda(this,
		[this](const FUniqueNetId&, bool bSuccess)
		{
			if (!bSuccess) Error(TEXT("Steam presence update failed; use browser or overlay invite."));
		}));
}
bool UBrSessionSubsystem::Service(float Delta)
{
	if (!GetWorld()) return true;
	for (auto It = VerifiedPlayers.CreateIterator(); It; ++It)
		if (!It.Key().IsValid()) It.RemoveCurrent();
	if (Operation != EOperation::Idle && !bTimedOut && FPlatformTime::Seconds() - OperationStarted > 45)
	{
		// Do not overlap an OSS operation whose completion is unknown.
		bTimedOut = true;
		bWantHost = false;
		Pending = EAction::Menu;
		Error(TEXT("Online operation timed out. Restart the game if Steam does not recover."));
		UBrGameInstance* GI = CastChecked<UBrGameInstance>(GetGameInstance());
		GI->LeaveLocal(GI->FlowError);
	}
	if (bAwaitMenu && GetWorld()->GetMapName().Contains(TEXT("L_MainMenu")) &&
		GetGameInstance()->GetFirstLocalPlayerController()) Continue();
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (bWantHost && GS && GetWorld()->GetNetMode() != NM_Client && Operation == EOperation::Idle)
	{
		if (!bHostAttempted) Create();
		else if (Sessions)
		{
			FOnlineSessionSettings Value = Settings();
			const FString Signature = AdvertisementSignature(Value);
			if (Advertisement.CanAttempt(Signature, FPlatformTime::Seconds()))
			{
				if (!Sessions->GetNamedSession(NAME_GameSession) && bRecreatingNull) Create();
				else if (Sessions->GetNamedSession(NAME_GameSession)) Publish(MoveTemp(Value));
			}
		}
	}
#if !UE_BUILD_SHIPPING
	// Bounded external smoke drives the real public subsystem path, not a mock.
	if (FParse::Param(FCommandLine::Get(), TEXT("BrSessionSmokeHost")) && SmokeStage == 0)
	{
		SmokeStage = 1;
		Host();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("BrSessionSmokeHost")) && SmokeStage == 1 && GS)
	{
		SetHostPassword(TEXT("smoke-password"));
		if (ABrGameState* MutableGS = GetWorld()->GetGameState<ABrGameState>()) MutableGS->MatchSettings.bPasswordProtected = true;
		SmokeStage = 2;
		SmokeStarted = FPlatformTime::Seconds();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("BrSessionPrivacySmoke")) && GS)
	{
		if (SmokeStage == 2 && GS->PlayerArray.Num() > 1) SmokeStage = 3;
		else if (SmokeStage == 3 && GS->PlayerArray.Num() == 1)
		{
			GetWorld()->GetGameState<ABrGameState>()->MatchSettings.Visibility = EBrSessionVisibility::InviteOnly;
			SmokeStage = 4;
			SmokeStarted = FPlatformTime::Seconds();
		}
		else if (SmokeStage == 4 && FPlatformTime::Seconds() - SmokeStarted > 25)
		{
			GetWorld()->GetGameState<ABrGameState>()->MatchSettings.Visibility = EBrSessionVisibility::Public;
			SmokeStage = 5;
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("BrSessionSmokeSearchOnly")))
	{
		if (SmokeStage == 0 || (Operation == EOperation::Idle && FPlatformTime::Seconds() - SmokeStarted > 6))
		{ SmokeStage = 1; SmokeStarted = FPlatformTime::Seconds(); Find(); }
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("BrSessionCheckExit")) && SmokeStage == 2 &&
		Operation == EOperation::Idle && FPlatformTime::Seconds() - SmokeStarted > 6)
	{
		SmokeStage = 3;
		Quit();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("BrSessionSmokeClient")))
	{
		if (SmokeStage == 0) { SmokeStage = 1; SmokeStarted = FPlatformTime::Seconds(); Find(); }
		else if (SmokeStage == 1 && Operation == EOperation::Idle && !Servers.IsEmpty())
		{
			SmokeStage = 2;
			Select(0);
			if (bPasswordPrompt) SubmitPassword(FParse::Param(FCommandLine::Get(), TEXT("BrWrongPassword")) ? TEXT("wrong") : TEXT("smoke-password"));
		}
		else if (SmokeStage == 1 && Operation == EOperation::Idle && FPlatformTime::Seconds() - SmokeStarted > 5)
		{ SmokeStarted = FPlatformTime::Seconds(); Find(); }
		else if (SmokeStage == 2 && GS && GetWorld()->GetNetMode() == NM_Client)
		{ SmokeStage = 3; SmokeStarted = FPlatformTime::Seconds(); }
		else if (SmokeStage == 3 && FPlatformTime::Seconds() - SmokeStarted > 3)
		{ SmokeStage = 4; Leave(TEXT("Session smoke successful leave.")); }
	}
#endif
	return true;
}
