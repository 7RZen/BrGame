#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "BrRoundRecord.h"
#include "BrMatchSettings.h"
#include "BrDamageSource.h"
#include "BrGameMode.generated.h"

class ABrPlayerController;

class ABrCharacter;
class ABrPlayerState;
class ABrPickupManager;
class ABrSafeZone;
struct FBrInventoryState;

UCLASS()
class BRGAME_API ABrGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ABrGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PreLoginAsync(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, const FOnPreLoginCompleteDelegate& OnComplete) override;
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* Player, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void InitSeamlessTravelPlayer(AController* NewController) override;
	virtual void PostSeamlessTravel() override;
	virtual void ChangeName(AController* Controller, const FString& NewName, bool bNameChange) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual bool IsLobbyMode() const { return false; }
	virtual bool MustSpectate_Implementation(APlayerController* Player) const override;
	virtual bool CanSpectate_Implementation(APlayerController* Viewer, APlayerState* ViewTarget) override;
	void UpdateLobby();
	void ChangeSettings(APlayerController* Requester, FBrMatchSettings Settings);
	void ChooseTeam(APlayerController* Requester, int32 Team, int32 Color);
	void StartRound(APlayerController* Requester);
	void EliminatePlayer(APlayerController* Requester, int32 Index);
	ABrPickupManager* GetPickupManager() const { return PickupManager; }
	ABrSafeZone* GetSafeZone() const { return SafeZone; }
	bool AreRespawnsDisabled() const;

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;
	virtual void RestartPlayerAtTransform(AController* NewPlayer, const FTransform& SpawnTransform) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	/** Called before pawn destruction on disconnect, and on authoritative life transitions. */
	void RecordPlayer(AController* Player);
	/** Step 6 must also reset active PlayerStates/pawns when beginning a new round. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Life")
	void ClearRoundRecords();
	/** Single authority-side policy hook for future teams/friendly fire. */
	virtual bool CanDamage(AController* InstigatorController, const AActor* Victim) const;
	bool CanDamageSource(const FBrDamageSource& Source, const AActor* Victim, bool bZoneDamage = false) const;
	FBrDamageSource CaptureDamageSource(AController* Controller) const;
	void CreditDown(const FBrDamageSource& Source);
	bool IsAdmitted(const AController* Controller) const;
	virtual bool IsEnemy(const ABrCharacter* A, const ABrCharacter* B) const;
	virtual bool AreTeammates(const ABrCharacter* A, const ABrCharacter* B) const;
	virtual bool IsLocationInSafeZone(const FVector& Location) const;
	virtual void OnPlayerEliminated(ABrPlayerState* Player);
	bool RespawnPlayer(ABrCharacter* OldPawn, const FBrInventoryState& Gear, bool bVoluntary);
	void LeaveCorpse(const ABrCharacter* Character);
	bool ForfeitPlayer(ABrPlayerController* Player);
	float GetUnstuckCooldown(AController* Player) const;
	void SetUnstuckCooldown(AController* Player, float Deadline);
	bool FindUnstuckLocation(const ABrCharacter* Character, FVector& Location) const;
protected:
	virtual void HandleMatchHasStarted() override;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FBrMenuSameFrameForfeitTest;
	friend class FBrServerAimTest;
	friend class FBrSprintPendingCombatTest;
#endif
	FTimerHandle FlowTimer, ResultsTimer;
	struct FPendingSteamLogin
	{
		TWeakObjectPtr<class UNetConnection> Connection;
		FUniqueNetIdRepl Id;
		FString Options, Address;
		FOnPreLoginCompleteDelegate Complete;
		double Started = 0;
	};
	TArray<FPendingSteamLogin> PendingSteamLogins;
	FTimerHandle SteamLoginTimer;
	TWeakObjectPtr<UPlayer> LoginSource;
	class UNetConnection* FindLoginConnection(const FString& Address) const;
	void ServiceSteamLogins();
	TArray<TWeakObjectPtr<AController>> ColorChoosers;
	TSet<TWeakObjectPtr<AController>> AdmittedControllers;
	TSet<int32> Participants;
	bool bTravelling = false;
	float MatchBeganTime = 0;
	int32 FogSmokeStage = 0;
	void ServiceFogSmoke();
	void ServiceFogSpectatorSmoke();
	int32 ShotSmokeStage = 0;
	float ShotSmokeNextTime = 0;
	void ServiceShotSmoke();
#if !UE_BUILD_SHIPPING
	void ServiceMinimapShot();
	void ServiceMenuSmoke();
	void ServiceMenuShot();
	int32 MenuSmokeStage = 0;
	float MenuSmokeNext = 0;
	FString MenuSmokeIdentity;
	FVector MenuSmokeOrigin;
	TWeakObjectPtr<ABrPlayerController> MenuSmokeClient;
	TWeakObjectPtr<ABrCharacter> MinimapShotFriend, MinimapShotEnemy;
#endif
	int32 MatchSmokeStage = 0;
	float MatchSmokeDeadline = 0;
	TWeakObjectPtr<ABrCharacter> MatchSmokeTarget;
	TWeakObjectPtr<AController> MatchSmokeTargetController;
	TWeakObjectPtr<ABrCharacter> MatchSmokeReviver;
	TWeakObjectPtr<ABrCharacter> MatchSmokeGrenadeTarget;
	TWeakObjectPtr<ABrCharacter> MatchSmokeGrenadeOutside;
	float MatchSmokeTargetHealth = 0;
	float MatchSmokeExpectedDamage = 0;
	float MatchSmokeRevivedHealth = 0;
	float MatchSmokeGrenadeTargetHealth = 0;
	float MatchSmokeGrenadeOutsideHealth = 0;
	float MatchSmokeExpectedGrenadeDamage = 0;
	float MatchSmokeExpectedMaxHealth = 0;
	int32 MatchSmokeGearLoaded = 0;
	int32 MatchSmokeGearReserve = 0;
	int32 MatchSmokeGearGrenades = 0;
	int32 MatchSmokeTimeoutDeaths = 0;
	TWeakObjectPtr<class UBrWeaponDefinition> MatchSmokeGearWeapon;
	EBrReviveRule MatchSmokeOriginalReviveRule = EBrReviveRule::Anyone;
	bool bMatchSmokeReviveRuleChanged = false;
	bool MatchSmokeOriginalFriendlyFire = false;
	bool bMatchSmokeFriendlyFireChanged = false;
	void ServiceMatchSmoke();
#if !UE_BUILD_SHIPPING
	bool ServiceSprintSmoke(ABrPlayerController* Runner);
	int32 SprintSmokeStage = 0;
	float SprintSmokeDeadline = 0;
	FVector SprintSmokeOriginalLocation;
	float SprintSmokePeakSpeed = 0;
	float SprintSmokeFirstActive = 0;
	int32 SprintSmokeLoaded = 0;
	bool ServiceAudioSmoke(ABrPlayerController* Host, ABrPlayerController* Listener);
	bool ServiceHitSmoke(ABrPlayerController* Host, ABrPlayerController* Observer);
	int32 HitSmokeStage = 0;
	float HitSmokeDeadline = 0;
	TWeakObjectPtr<ABrCharacter> HitSmokeVictim;
	FVector HitSmokeVictimLocation, HitSmokeObserverLocation, HitSmokeOrigin;
	int32 HitSmokeVictimTeam = -1, HitSmokeObserverTeam = -1;
	float HitSmokeHealth = 0;
	uint8 HitSmokeVictimMode = 0, HitSmokeObserverMode = 0;
	int32 AudioSmokeStage = 0;
	float AudioSmokeDeadline = 0, AudioSmokeOriginalVision = 0;
	FVector AudioSmokeHostLocation, AudioSmokeListenerLocation;
	int32 AudioSmokeOriginalTeam = -1;
	uint8 AudioSmokeReceiverMovementMode = 0, AudioSmokeReceiverCustomMode = 0;
#endif
	void AssignPlayer(APlayerController* Player);
	void FlowService();
	void CheckWinner();
	void ReturnToLobby();
	bool TravelFlow(const FString& Map);
	TArray<int32> GroupSizes(const ABrPlayerState* Ignore = nullptr) const;
	UPROPERTY(Transient)
	TObjectPtr<ABrPickupManager> PickupManager;
	UPROPERTY(Transient)
	TObjectPtr<ABrSafeZone> SafeZone;
	FBrMatchSettings InitialMatchSettings;
	UPROPERTY(Transient)
	TMap<FString, FBrRoundRecord> RoundRecords;
	FString RoundKey(const AController* Player) const;
	bool PreparePlayer(AController* Player);
	bool AdmitController(APlayerController* Player);
	void ServiceRoundRecords();
	void ServiceRoundRecord(const FString& Key, FBrRoundRecord& Record);
	float ReconnectGrace() const;
	bool CanRestart(AController* Controller) const;
	void FailedSpawn(AController* Controller);
	void BlockSpawn(AController* Player);
	void LeaveRecordedCorpse(const FBrRoundRecord& Record);
	bool FindRespawnLocation(const ABrCharacter* Character, FVector& OutLocation, const ABrPlayerState* SpawnPS = nullptr, bool bIgnoreZone = false) const;
};
