#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrPenaltyDefinition.h"
#include "BrPenaltyComponent.generated.h"

class ABrCharacter;
class UStaticMeshComponent;
class UAudioComponent;
class UNiagaraComponent;
struct FBrDownedExit;

UCLASS()
class BRGAME_API UBrPenaltyComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBrPenaltyComponent();
	UPROPERTY(ReplicatedUsing=OnRep_Penalty)
	FBrPenaltyState Penalty;
	UPROPERTY(Replicated)
	TObjectPtr<ABrCharacter> PlantTarget;
	UPROPERTY(Replicated)
	float PlantStartTime = 0;
	UPROPERTY(Replicated)
	float PlantEndTime = 0;
	ABrCharacter* FindPlantTarget() const;
	bool CanPlant(const ABrCharacter* Target, bool bResolveDeadline = false) const;
	void StartPlant();
	void StopPlant();
	void CancelPlant();
	void NewDown();
	void LifeChanged();
	void ResolvePlants(float Now, const FBrDownedExit& Exit);
	void Restore(const FBrPenaltyState& State);
	void ResetRound();
	bool IsActive(float Time = -1) const;
	float SpeedMultiplier(float Time = -1) const;
	bool ForcesForward(float Time = -1) const;
	bool DisablesSprint(float Time = -1) const;
	bool ForcesStanding(float Time = -1) const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend class FBrPenaltyRuntimeTest;
	ABrCharacter* Character() const;
	FTimerHandle ServiceTimer;
	void EnsureService();
	void Service();
	void Changed();
	void Cosmetics();
	void ClearCosmetics();
	TArray<TWeakObjectPtr<UBrPenaltyComponent>> Planters;
	void CancelIncomingPlants();
	UPROPERTY()
	TObjectPtr<class UStaticMesh> TubeMesh;
	UPROPERTY()
	TObjectPtr<class UStaticMesh> PuffMesh;
	UPROPERTY()
	TObjectPtr<class UMaterialInterface> PlaceholderMaterial;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBrPenaltyDefinition>> Definitions;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Tube;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Puffs;
	TArray<float> PuffBirths;
	int32 NextPuff = 0;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Audio;
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Niagara;
	UPROPERTY(Transient)
	TObjectPtr<UBrPenaltyDefinition> PresentedDefinition;
	UFUNCTION()
	void OnRep_Penalty();
	UFUNCTION(Server, Reliable)
	void ServerPlant(bool bHeld);
};
