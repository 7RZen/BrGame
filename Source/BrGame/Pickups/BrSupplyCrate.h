#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrSupplyCrate.generated.h"

class UStaticMeshComponent;
class ABrPickupManager;

/** Stationary network actor; only its noncolliding visual interpolates during flight. */
UCLASS()
class BRGAME_API ABrSupplyCrate : public AActor
{
	GENERATED_BODY()
public:
	ABrSupplyCrate();
	void Initialize(ABrPickupManager* Manager);
	bool IsLanded() const { return bLanded; }
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Crate;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Marker;
	UPROPERTY(Replicated)
	double StartTime = 0;
	UPROPERTY(Replicated)
	float Height = 3000;
	UPROPERTY(Replicated)
	float Duration = 4;
	UPROPERTY(ReplicatedUsing=OnRep_Landed)
	bool bLanded = false;
	TWeakObjectPtr<ABrPickupManager> DropManager;
	FTimerHandle LandingTimer;
	void Land();
	void UpdateVisual();
	UFUNCTION()
	void OnRep_Landed();
};
