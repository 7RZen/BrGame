#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BrMovement.h"
#include "BrCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UBrCharacterMovementComponent;
class UBrInputConfig;
class UBrInventoryComponent;
class UBrHealthComponent;
class UStaticMeshComponent;
class UBrLifeComponent;
class UTextRenderComponent;
class USoundBase;
struct FInputActionValue;

UCLASS()
class BRGAME_API ABrCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ABrCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Restart() override;
	virtual void NotifyControllerChanged() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnRep_IsCrouched() override;
	virtual void OnStartCrouch(float HeightAdjust, float ScaledHeightAdjust) override;
	virtual void OnEndCrouch(float HeightAdjust, float ScaledHeightAdjust) override;
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

	UFUNCTION(BlueprintPure, Category="Movement")
	EBrPosture GetPosture() const;

	virtual bool CanMove() const;
	virtual bool CanChangePosture() const;
	bool IsGameplayInputBlocked() const;
	UBrLifeComponent* GetLife() const { return Life; }
	void ApplyLifePresentation();
	void ApplyTeamColor();
	virtual void OnRep_PlayerState() override;
	void SetSpectatorCameraActive() { TopDownCameraActivate(); }
	virtual float GetWeaponRunSpeedMultiplier() const;
	virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* InstigatorController, AActor* Causer) override;
	UBrInventoryComponent* GetInventory() const { return Inventory; }
	UBrHealthComponent* GetHealth() const { return Health; }
	class UBrHitFeedbackComponent* GetHitFeedback() const { return HitFeedback; }
	void UpdateWeaponVisual();
	/** Game-thread post-animation callback; cosmetic only, no firing-origin changes. */
	void UpdatePostureWeaponPose();
	/** Synchronously publish the current pose while still fog-hidden, before any primitive is unhidden. */
	void PrepareForFogReveal();
	/** Coalesced server-only retry of cached auto-pickup overlaps, after state mutation completes. */
	void RequestAutoPickupRetry();
	void PlayTracer(const FVector& Origin, const FVector& Direction, float Speed, float Range);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastTracer(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction, float Speed, float Range);
	float GetCameraYaw() const;
	UBrCharacterMovementComponent* GetBrMovement() const;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category="Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category="Camera")
	TObjectPtr<UCameraComponent> TopDownCamera;

	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin="1.0", Units="cm"))
	float OrthoWidth = 2500.0f;

private:
	friend class FBrSprintIntegrationTest;
	friend class FBrSprintPickupRetryTest;
	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<class UBrHitFeedbackComponent> HitFeedback;
	FTimerHandle FootstepTimer;
	double LastFootstepService = 0;
	float FootstepProgress = 0;
	void ServiceFootsteps();
	void ApplyMeshPose(const FVector& Location, const FRotator& Rotation, bool bPreserveSmoothing = false);
	void ApplyLifeMeshPose(bool bPreserveSmoothing = false);
#if !UE_BUILD_SHIPPING
public:
	void BeginSprintSmoke();
private:
	void ServiceSprintSmoke(float DeltaSeconds);
	bool bSprintSmokeRunning = false;
	float SprintSmokeElapsed = 0;
	int32 SprintSmokeCorrections = 0;
	friend class UBrCharacterMovementComponent;
	void ServiceMeshPoseSmoke(float DeltaSeconds);
	void ServicePostureShot(float DeltaSeconds);
	int32 PostureShotLast = INDEX_NONE;
	float PostureShotSettle = 0;
	uint8 PostureShotMask = 0;
	float MeshPoseSmokeSeconds = 0;
	int32 MeshPoseSmokeSamples = 0;
	bool bMeshPoseSmokeDowned = false;
	bool bMeshPoseSmokeDownPassed = false;
	bool bMeshPoseSmokeRevivePassed = false;
	bool bMeshPoseSmokeFailed = false;
	EBrPosture VisualSmokePosture = EBrPosture::Standing;
	float VisualSmokeSeconds = 0;
	bool bVisualCrouchPassed = false;
	bool bVisualPronePassed = false;
#endif
	void TopDownCameraActivate();
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> TeamMaterials;
	UPROPERTY(VisibleAnywhere, Category="Life")
	TObjectPtr<UBrLifeComponent> Life;
	UPROPERTY(VisibleAnywhere, Category="Life")
	TObjectPtr<UTextRenderComponent> HelpText;
	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<UBrInventoryComponent> Inventory;
	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<UBrHealthComponent> Health;
	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<UStaticMeshComponent> WeaponVisual;
	UPROPERTY(Transient)
	TObjectPtr<UBrInputConfig> LoadedInputConfig;

	float TargetOrthoWidth = 0.0f;
	FTimerHandle AutoPickupRetryTimer;
	bool bAutoPickupRetryQueued = false;
	void RetryAutoPickups();

	float ClampCameraWidth(float Width) const;
	void InitializeCameraZoom();
	void Zoom(const FInputActionValue& Value);
	void UpdateCameraActivation();
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void SprintStarted();
	void SprintStopped();
	void ToggleCrouch();
	void ToggleProne();
	void FireStarted();
	void FireStopped();
	void Reload();
	void ThrowGrenade();
	void InteractStarted();
	void InteractStopped();
	void RespawnStarted();
	void RespawnStopped();
};
