#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrPickupDefinition.h"
#include "BrPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

USTRUCT()
struct FBrPickupState
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<UBrPickupDefinition> Definition;
	UPROPERTY()
	FBrPickupPayload Payload;
	UPROPERTY()
	bool bAvailable = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FBrPickupConsumed, class ABrPickup*);

UCLASS()
class BRGAME_API ABrPickup : public AActor
{
	GENERATED_BODY()
public:
	ABrPickup();
	static ABrPickup* Spawn(UWorld* World, const FTransform& Transform, UBrPickupDefinition* Definition,
		const FBrPickupPayload& Payload, bool bManaged = false);
	static ABrPickup* FindInteractPickup(const ABrCharacter* Character);
	bool CanCollect(const ABrCharacter* Character) const;
	bool TryCollect(ABrCharacter* Character);
	bool TryAutoCollect(ABrCharacter* Character);
	void ResetPickup(UBrPickupDefinition* Definition);
	FText GetPickupLabel() const;
	FBrPickupConsumed OnConsumed;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY()
	TObjectPtr<USphereComponent> Trigger;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY()
	TObjectPtr<UTextRenderComponent> Label;
	UPROPERTY(ReplicatedUsing=OnRep_State)
	FBrPickupState State;
	bool bManagedSlot = false;
	bool bCollecting = false;
	FTimerHandle ActivationTimer;
	void EnableCollection();
	UFUNCTION()
	void OnRep_State();
	UFUNCTION()
	void Overlap(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent,
		int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
};
