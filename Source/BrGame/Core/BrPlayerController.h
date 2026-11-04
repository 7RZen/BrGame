#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BrMatchSettings.h"
#include "BrAudioRules.h"
#include "BrMovement.h"
#include "BrPlayerController.generated.h"

class UBrInputConfig;
class UEnhancedInputLocalPlayerSubsystem;
class UBrFlowWidget;
class UBrMenuWidget;
class UBrFogComponent;
class ABrPlayerState;
class ABrCharacter;
class USoundBase;
struct FBrMatchSettings;

UCLASS()
class BRGAME_API ABrPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ABrPlayerController();
	virtual void GetAudioListenerPosition(FVector& Location, FVector& Front, FVector& Right) const override;
	UFUNCTION(Client, Unreliable)
	void ClientSound(EBrSoundKind Kind, const TSoftObjectPtr<USoundBase>& Sound, FVector_NetQuantize Location);
	UFUNCTION(Client, Unreliable)
	void ClientSounds(const TArray<FBrSoundType>& Types, const TArray<FBrSoundEvent>& Events);
	UFUNCTION(Client, Reliable)
	void ClientAudioSmokeProbe(int32 Stage, FVector_NetQuantize Origin);
	UFUNCTION(Client, Reliable)
	void ClientHitSmokeProbe(int32 Stage, bool bVictim, FVector_NetQuantize Origin);
	UFUNCTION(Client, Reliable)
	void ClientSprintSmoke(bool bStart, FVector Location);
	/** Development fixture only; normal predicted CMC input carries posture to authority. */
	UFUNCTION(Client, Reliable)
	void ClientPostureSmoke(EBrPosture Posture);
	UFUNCTION(Server, Reliable)
	void ServerSettings(FBrMatchSettings Settings);
	UFUNCTION(Server, Reliable)
	void ServerChooseTeam(int32 Team, int32 Color);
	UFUNCTION(Server, Reliable)
	void ServerStartRound();
	UFUNCTION(Client, Reliable)
	void ClientEnterFlow();
	UFUNCTION(Exec)
	void BrStartMatch();
	UFUNCTION(Exec)
	void BrEliminatePlayer(int32 Index);
	UFUNCTION(Server, Reliable)
	void ServerEliminatePlayer(int32 Index);
	void CycleSpectator();
	UFUNCTION(Server, Reliable)
	void ServerCycleSpectator();
	void ServiceSpectator();
	void ResetFlowRequests();
	UFUNCTION(BlueprintPure, Category="Spectating")
	ABrPlayerState* GetSpectatorTarget() const { return SpectatorTarget; }
	ABrCharacter* GetFlowSpectatorPawn() const { return FlowSpectatorPawn; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const override;
	bool IsFlowSpectating() const;
	void ToggleMap();
	void CloseMap();
	bool IsMapOpen() const { return bMapOpen; }
	void ToggleMenu();
	void CloseMenu();
	bool IsMenuOpen() const { return MenuWidget != nullptr; }
	UBrMenuWidget* GetMenuWidget() const { return MenuWidget; }
	bool IsGameplayInputBlocked() const;
	void RefreshCursorMode();
	void ConfirmLeave();
	void LeaveAnyway();
	bool IsLeaveWaiting() const { return bLeaving; }
	bool HasLeaveFailed() const { return bLeaveFailed; }
	UFUNCTION(Server, Reliable)
	void ServerForfeit();
	UFUNCTION(Client, Reliable)
	void ClientForfeitAck(bool bAccepted);
	/** Dev-only owning-client menu smoke intent; inert unless explicitly launched with BrMenuSmoke. */
	UFUNCTION(Client, Reliable)
	void ClientMenuSmoke(int32 Stage);
	UFUNCTION(Server, Reliable)
	void ServerUnstuck();
	void CancelUnstuck(const FString& Reason);
	void ValidateUnstuckMovement();
	bool CanUnstuck() const;
	float MenuServerTime() const;
	FString GetBindingLabel(FName MappingName) const;
	UPROPERTY(Replicated)
	float UnstuckStart = 0;
	UPROPERTY(Replicated)
	float UnstuckEnd = 0;
	UPROPERTY(Replicated)
	float UnstuckCooldownEnd = 0;
	UPROPERTY(Replicated)
	FString UnstuckMessage;
	virtual void Destroyed() override;
	/** Development-only operation; implementation is inert in Shipping even for forged RPCs. */
	UFUNCTION(Server, Reliable)
	void ServerBrCheat(FName Command, FName Weapon, float Amount);
	UFUNCTION(Client, Reliable)
	void ClientFogProbe(int32 Stage, bool bExpectedPresent);
	UFUNCTION(Client, Reliable)
	void ClientFogSpectatorProbe(int32 Stage, int32 TargetId, int32 WithheldId, const FString& TargetGuid);
	/** Smoke-only BeginPlay handshake; inert in Shipping and outside BrMatchSmoke. */
	UFUNCTION(Server, Reliable)
	void ServerMatchSmokeReady(ABrCharacter* ObserverPawn);
#if !UE_BUILD_SHIPPING
	void ReportMatchSmokeReady(ABrCharacter* ObserverPawn);
	bool IsMatchSmokeReady() const { return MatchSmokeReadyPawn.IsValid() && MatchSmokeReadyPawn.Get() == GetPawn(); }
#endif
	UFUNCTION(Client, Reliable)
	void ClientPickupFeedback(const FString& Feedback);
	FString PickupFeedback;
	float PickupFeedbackUntil = 0;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBrMenuWidget> MenuWidget;
	FTimerHandle LeaveTimer, UnstuckTimer;
	bool bLeaving = false;
	bool bLeaveFailed = false;
	double LeaveDeadline = 0;
	uint64 MenuDispatchFrame = MAX_uint64;
	float MapClosedAt = -1;
	TWeakObjectPtr<ABrCharacter> UnstuckPawn;
	FVector UnstuckOrigin;
	uint8 UnstuckPosture = 0;
	bool bUnstuckInside = false;
	void ServiceUnstuck();
	void FinishLeave();
	void ServiceLeave();
	bool bMapOpen = false;
#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<APawn> MatchSmokeReadyPawn;
	FVector AudioSmokeOrigin = FVector::ZeroVector;
	int32 AudioSmokeFire = 0, AudioSmokeSteps = 0, AudioSmokeDown = 0, AudioSmokeProneBaseline = 0;
	uint32 HitSmokeBaseline = 0;
#endif
	UPROPERTY(VisibleAnywhere, Category="Fog")
	TObjectPtr<UBrFogComponent> Fog;
	UPROPERTY(Transient)
	TObjectPtr<UBrFlowWidget> FlowWidget;
	FTimerHandle FlowUITimer;
	FTimerHandle FogProbeTimer;
	UPROPERTY(ReplicatedUsing=OnRep_SpectatorTarget)
	TObjectPtr<ABrPlayerState> SpectatorTarget;
	/** PawnPrivate on PlayerState is a derived, nonreplicated cache. Carry the
	 * authoritative pawn too so a newly opened fog channel can resolve directly. */
	UPROPERTY(ReplicatedUsing=OnRep_SpectatorTarget)
	TObjectPtr<ABrCharacter> FlowSpectatorPawn;
	TWeakObjectPtr<APawn> AppliedSpectatorPawn;
	float LastSpectatorRequest = -1;
	float NextSpectatorLog = 0;
	float NextSpectatorRouteLog = 0;
	void SelectSpectator(bool bCycle);
	UFUNCTION()
	void OnRep_SpectatorTarget();
	void RefreshFlow();
	float LastTeamRequest = -1;
	FString LastLoggedFlow;
	int32 FlowInputPhase = -1;
	bool bSmokeDisconnectIssued = false;
	UPROPERTY(Transient)
	TObjectPtr<UBrInputConfig> LoadedInputConfig;

	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
};
