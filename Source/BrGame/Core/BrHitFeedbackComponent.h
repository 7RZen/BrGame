#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrHitFeedbackComponent.generated.h"

class ABrCharacter;
class AHUD;
class UCanvas;
class UMaterialInterface;
struct FMinimalViewInfo;

struct FBrHitIndicator
{
	FVector Origin = FVector::ZeroVector;
	float Time = 0;
};

/** Transient cosmetics only. Owner receives origin, observers receive only victim-relative impulse. */
UCLASS()
class BRGAME_API UBrHitFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBrHitFeedbackComponent();
	void ServerDamage(float LostHealth, const FVector& Origin, bool bExplosion);
	void Camera(FMinimalViewInfo& View) const;
	void Draw(AHUD* HUD, UCanvas* Canvas) const;
	/** Called by the character's existing tick; true also on the final restoration frame. */
	bool UpdateReaction();
	FVector MeshOffset() const;
	const TArray<FBrHitIndicator>& GetIndicators() const { return Indicators; }
	uint32 OwnerEvents = 0;
	uint32 RemoteEvents = 0;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend class FBrHitFeedbackCameraTest;
	friend class FBrHitFeedbackSmoothingTest;
	UFUNCTION(Client, Reliable)
	void ClientDamage(FVector_NetQuantize Origin, bool bExplosion, uint8 Damage);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastReaction(FVector_NetQuantizeNormal Away);
	ABrCharacter* Character() const;
	bool LocalVictim() const;
	float Now() const;
	TArray<FBrHitIndicator> Indicators;
	float ShakeTime = -1000;
	float ShakeDuration = 0;
	float ShakeAmplitude = 0;
	float JoltAmplitude = 0;
	FVector ShakeSeed = FVector::ZeroVector;
	float FlashTime = -1000;
	float FlashStrength = 0;
	float ReactionTime = -1000;
	FVector ReactionAway = FVector::ZeroVector;
	bool bReactionApplied = false;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Overlay;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PreviousOverlay;
};
