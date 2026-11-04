#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BrMovement.h"
#include "BrCharacterMovementComponent.generated.h"

struct FBrMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
	EBrPosture Posture = EBrPosture::Standing;
	EBrPosture RequestedPosture = EBrPosture::Standing;
	FBrSprintState Sprint;

	virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement, const FClientAdjustment& Adjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap) override;
};

struct FBrNetworkMoveData : public FCharacterNetworkMoveData
{
	FBrSprintState Sprint;
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type) override;
};

struct FBrNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FBrNetworkMoveDataContainer() { NewMoveData = &Moves[0]; PendingMoveData = &Moves[1]; OldMoveData = &Moves[2]; }
	FBrNetworkMoveData Moves[3];
};

UCLASS()
class BRGAME_API UBrCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UBrCharacterMovementComponent();

	virtual float GetMaxSpeed() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void Crouch(bool bClientSimulation = false) override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData,
		FName ClientBaseBoneName, uint8 ClientMovementMode) override;
	virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;

	/** Input-order hint only; never used to authorize damage or bypass CMC timestamp validation. */
	float GetCombatMoveTimeStamp() const;
	bool IsValidCombatMoveTimeStamp(float TimeStamp) const;
	/** Authority-only: an accepted shot/throw also commits its horizontal facing. */
	void ApplyAcceptedCombatAim(const FVector& Aim, float MoveTimeStamp);

	EBrPosture GetPosture() const;
	EBrPosture GetRequestedPosture() const;
	EBrPosture GetServerCorrectionPosture() const { return ServerCorrectionPosture; }
	EBrPosture GetServerCorrectionRequest() const { return ServerCorrectionRequest; }
	bool IsSprinting() const;
	bool WantsSprint() const { return bWantsToSprint; }
	const FBrSprintState& GetSprintState() const { return Sprint; }
	const FBrSprintState& GetServerCorrectionSprint() const { return ServerCorrectionSprint; }
	float GetSprintFraction() const;
	double GetSprintFireReadyTime() const { return SprintFireReadyTime; }
	bool CanSprintFire() const;
	bool WantsProne() const { return bWantsToProne; }
	void SetSprinting(bool bSprinting);
	void RestoreSprintIntent(bool bSprint) { bWantsToSprint = bSprint; }
	/** Move-local replay context; never changes the controller's live input rotation. */
	void PrepareSprintReplay(float FacingYaw, double MoveWorldTime);
	void FinishSprintReplay();
	/** Only native move combining rewinds energy. Correction replay uses the server snapshot instead. */
	void RestoreSprintStateForCombine(const FBrSprintState& State) { Sprint = State; }
	/** Cancels move intent and starts the fire gate immediately, also before the next CMC update. */
	void CancelSprintForCombat();
	void SetProne(bool bProne) { bWantsToProne = bProne; }
	void RequestPosture(EBrPosture Posture);
	void ApplyReplicatedPosture();

protected:
	virtual void BeginPlay() override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;

private:
	friend class FBrPostureRealMeshTest;
	friend class FBrSkeletonPoseTest;
	friend class FBrPostureFogRevealTest;
	friend class FBrHitFeedbackSmoothingTest;
	friend class FBrSprintIntegrationTest;
	friend class FBrSprintReplayYawTest;
	friend class FBrSprintReplayRecoveryTest;
	friend class FBrSprintPendingCombatTest;
	friend class FBrSprintPickupRetryTest;
	UPROPERTY(ReplicatedUsing=OnRep_Posture)
	EBrPosture ReplicatedPosture = EBrPosture::Standing;

	UPROPERTY(Replicated)
	bool bReplicatedSprint = false;

	bool bWantsToSprint = false;
	FBrSprintState Sprint;
	FBrSprintState ServerCorrectionSprint;
	FBrSprintState ClientReportedSprint;
	FBrNetworkMoveDataContainer NetworkMoveData;
	double SprintFireReadyTime = 0;
	TOptional<float> SprintReplayYaw;
	double SprintReplayWorldTime = 0;
	void UpdateSprint(float DeltaSeconds);
	void SprintStopped();

	bool bWantsToProne = false;
	EBrPosture LocalPosture = EBrPosture::Standing;
	uint8 ClientReportedPosture = 0;
	EBrPosture ServerCorrectionPosture = EBrPosture::Standing;
	EBrPosture ServerCorrectionRequest = EBrPosture::Standing;
	TOptional<EBrPosture> RejectedPostureRequest;
	FBrMoveResponseDataContainer MoveResponseData;
	TOptional<float> CombatAimTimeStamp;
	FRotator CombatAimRotation = FRotator::ZeroRotator;

	UFUNCTION()
	void OnRep_Posture();

	void RefreshPosture();
	float GetPostureHalfHeight(EBrPosture Posture) const;
	void ApplyAuthoritativePosture(EBrPosture Posture);
};
