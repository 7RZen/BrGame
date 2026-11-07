#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrLifeRules.h"
#include "BrInventoryComponent.h"
#include "BrLifeComponent.generated.h"

class ABrCharacter;

UCLASS(ClassGroup=(BrGame), meta=(BlueprintSpawnableComponent))
class BRGAME_API UBrLifeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBrLifeComponent();
	UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Life")
	EBrLifeState State = EBrLifeState::Alive;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	float DownedEndTime = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	float InvulnerabilityEndTime = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	float RespawnHoldEndTime = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	TObjectPtr<ABrCharacter> Reviver;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	TObjectPtr<ABrCharacter> ReviveTarget;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	float ReviveStartTime = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	float ReviveEndTime = 0;

	bool IsAlive() const { return State == EBrLifeState::Alive; }
	bool IsInvulnerable() const;
	float ServerTime() const;
	void Down();
	/** Synchronous authority damage context, cleared immediately after TakeDamage. */
	void SetPendingDowner(const FString& Key);
	void Eliminate();
	/** Server restore only; preserves the old absolute deadline, not a new dying interval. */
	void RestoreDowned(float Deadline);
	const FBrInventoryState& GetDownedGear() const { return GearAtDown; }
	void CancelRevive();
	void DamageReceived();
	void Fired();
	void GrantSpawnProtection();
	ABrCharacter* FindReviveTarget() const;
	bool CanRevive(const ABrCharacter* Target) const;
	void StartInteract();
	void StopInteract();
	void StartRespawnHold();
	void StopRespawnHold();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend class FBrPenaltyRuntimeTest;
	FString PendingDownerKey;
	bool bAudioPresentationInitialized = false;
	EBrLifeState AudioPresentedState = EBrLifeState::Alive;
	UPROPERTY()
	FBrInventoryState GearAtDown;
	FTimerHandle ServiceTimer;
	ABrCharacter* Character() const;
	void EnsureServiceTimer();
	void Service();
	void ServiceDowned(float Now);
	void FinishRevive();
	void ResolveDowned(bool bVoluntary);
	UFUNCTION()
	void HealthDepleted();
	UFUNCTION()
	void OnRep_State();
	UFUNCTION(Server, Reliable)
	void ServerStartRevive();
	UFUNCTION(Server, Reliable)
	void ServerStopRevive();
	UFUNCTION(Server, Reliable)
	void ServerRespawnHold(bool bHeld);
};
