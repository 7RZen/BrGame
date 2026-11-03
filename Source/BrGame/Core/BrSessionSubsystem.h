#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Containers/Ticker.h"
#include "BrSessionRules.h"
#include "BrSessionSubsystem.generated.h"

UCLASS()
class BRGAME_API UBrSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void Host();
	void Find();
	void Select(int32 Index);
	void SubmitPassword(const FString& Password);
	void CancelPassword();
	void JoinIP(const FString& Address, const FString& Password);
	void Leave(const FString& Message);
	void Quit();
	bool SetHostPassword(const FString& Password);
	bool ApprovePassword(const FString& Proof) const;
	bool IsSteam() const { return bSteam; }
	bool VerifyLoginIdentity(class UNetConnection* Connection, const FUniqueNetIdRepl& Claimed, bool bLocalHost = false) const;
	bool BindPlayerIdentity(class APlayerController* Player, class UNetConnection* Connection, const FUniqueNetIdRepl& Claimed, bool bLocalHost);
	bool VerifyPlayerIdentity(const class AController* Player) const;
	bool IsSteamLobbyMember(class UNetConnection* Connection, const FUniqueNetIdRepl& Claimed) const;
	FString Status() const;
	const TArray<FOnlineSessionSearchResult>& Results() const { return Servers; }
	int32 Revision = 0;
	bool bPasswordPrompt = false;
	FString Describe(int32 Index) const;
private:
	enum class EOperation { Idle, Creating, Finding, Joining, Updating, Destroying };
	enum class EAction { None, Host, Join, IP, Menu, Quit };
	IOnlineSessionPtr Sessions;
	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FOnlineSessionSearchResult> Servers;
	FOnlineSessionSearchResult Selected;
	FDelegateHandle CreateHandle, FindHandle, JoinHandle, DestroyHandle, InviteHandle, UpdateHandle, FailureHandle;
	FTSTicker::FDelegateHandle Ticker;
	EOperation Operation = EOperation::Idle;
	EAction Pending = EAction::None;
	bool bSteam = false, bWantHost = false, bHostAttempted = false, bAwaitMenu = false;
	bool bTimedOut = false;
	FString HostPassword, JoinProof, IPAddress;
	FBrAdvertisementState Advertisement;
	FOnlineSessionSettings AttemptedSettings, PublishedSettings;
	bool bRecreatingNull = false;
	// Server-created controller lifetime binding survives OnNetCleanup clearing its
	// net connection BEFORE Destroyed/Logout captures the round's final snapshot.
	TMap<TWeakObjectPtr<const AController>, FUniqueNetIdRepl> VerifiedPlayers;
	double OperationStarted = 0;
	int32 SmokeStage = 0;
	double SmokeStarted = 0;
	void Error(const FString& Message);
	void Presence();
	void Begin(EOperation Value);
	void Queue(EAction Action);
	void Continue();
	void Create();
	void Publish(FOnlineSessionSettings Value);
	void PublicationComplete(bool bSuccess);
	static FString AdvertisementSignature(const FOnlineSessionSettings& Value);
	void JoinSelected();
	FOnlineSessionSettings Settings() const;
	bool Service(float Delta);
	void Created(FName Name, bool bSuccess);
	void Found(bool bSuccess);
	void Joined(FName Name, EOnJoinSessionCompleteResult::Type Result);
	void Destroyed(FName Name, bool bSuccess);
	void Updated(FName Name, bool bSuccess);
	void Invited(bool bSuccess, int32 ControllerId, FUniqueNetIdPtr User, const FOnlineSessionSearchResult& Result);
	void Failed(const FUniqueNetId& User, ESessionFailure::Type Failure);
};
