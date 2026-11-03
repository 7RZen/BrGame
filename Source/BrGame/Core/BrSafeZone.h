#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrZoneRules.h"
#include "BrSafeZone.generated.h"

class UStaticMeshComponent;

UCLASS()
class BRGAME_API ABrSafeZone : public AActor
{
	GENERATED_BODY()
public:
	ABrSafeZone();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(ReplicatedUsing=OnRep_State)
	FBrZoneState State;
	double ServerTime() const;
	FBrZoneCircle Circle() const { return State.At(ServerTime()); }
	bool IsInside(const FVector& Location) const { return Circle().IsInside(Location); }
	bool IsSuddenDeath() const { return State.IsSuddenDeath(ServerTime()); }
	void Stop();
	void NextStage();
	void SetTimeScale(float Value);
	void TogglePause();
private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CurrentRing;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> TargetRing;
	TArray<FBrZoneStage> Stages;
	FBox PlayableBounds = FBox(ForceInit);
	FRandomStream Stream;
	FTimerHandle ServiceTimer;
	float TimeScale = 1;
	bool bSuddenDeathLogged = false;
	bool bTargetSelectionPending = false;
	bool bNavigationHold = false;
	double TargetSearchDeadline = 0;
	float PendingTargetRadius = 0;
	bool SelectPlayableTarget(const FBrZoneCircle& Current, float Radius, FBrZoneCircle& Target, bool& bNavVerified);
	void RetryTargetSelection(double Now);
	void LogStage() const;
	void StartStage(int32 Index, FBrZoneCircle Initial, double StartTime);
	void Service();
	void FindInitialBounds();
	UFUNCTION()
	void OnRep_State();
	void UpdateVisuals();
};
