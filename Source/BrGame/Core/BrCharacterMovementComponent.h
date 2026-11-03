#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BrMovement.h"
#include "BrCharacterMovementComponent.generated.h"

struct FBrMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
	EBrPosture Posture = EBrPosture::Standing;
	EBrPosture RequestedPosture = EBrPosture::Standing;

	virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement, const FClientAdjustment& Adjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap) override;
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
	bool IsWalking() const { return bWantsToWalk; }
	bool WantsProne() const { return bWantsToProne; }
	void SetWalking(bool bWalking) { bWantsToWalk = bWalking; }
	void SetProne(bool bProne) { bWantsToProne = bProne; }
	void RequestPosture(EBrPosture Posture);
	void ApplyReplicatedPosture();

protected:
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;

private:
	friend class FBrHitFeedbackSmoothingTest;
	UPROPERTY(ReplicatedUsing=OnRep_Posture)
	EBrPosture ReplicatedPosture = EBrPosture::Standing;

	UPROPERTY(Replicated)
	bool bWantsToWalk = false;

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
