#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrPickupManager.generated.h"

class ABrPickup;
class ABrSupplyCrate;
class UBrPickupDefinition;
class UBrWeaponDefinition;
class UBrQuestPickupDefinition;

/** Server-only crate scheduler and round drop ownership. No Tick. */
UCLASS()
class BRGAME_API ABrPickupManager : public AActor
{
	GENERATED_BODY()
public:
	ABrPickupManager();
	void Initialize(int32 PlayersAtStart);
	void RespawnAllPickups();
	void RegisterRoundDrop(ABrPickup* Pickup);
	void ClearRoundPickups();
	bool LandCrate(ABrSupplyCrate* Crate);
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY()
	TArray<TObjectPtr<UBrPickupDefinition>> Definitions;
	UPROPERTY()
	TObjectPtr<UBrQuestPickupDefinition> QuestDefinition;
	UPROPERTY()
	TArray<TObjectPtr<UBrWeaponDefinition>> QuestWeapons;
	FRandomStream Stream;
	FTimerHandle DropTimer;
	FBox PlayableBounds = FBox(ForceInit);
	FTimerHandle NavigationProbeTimer;
	double NavigationProbeStarted = 0;
	int32 NavigationProbes = 0;
	void ProbeNavigation();
	FName MapId;
	int32 InitialCrateCount = 0;
	int32 IntervalCrateCount = 1;
	bool bInitialized = false;
	TSet<TWeakObjectPtr<ABrPickup>> RoundDrops;
	TSet<TWeakObjectPtr<ABrSupplyCrate>> Crates;
	UFUNCTION()
	void RoundDropDestroyed(AActor* DestroyedActor);
	UFUNCTION()
	void CrateDestroyed(AActor* DestroyedActor);
	void InitialWave();
	bool ResolveMapCounts();
	void DropWave(bool bInitial);
	bool DropOne(TArray<FVector>& WaveLandings, float Spacing);
	void ServiceDrop();
	void ScheduleNext();
	bool ClearGround(const FVector& Candidate, FVector& Floor, bool bCrate, const AActor* Ignore = nullptr,
		bool* bUsedNavigation = nullptr) const;
	bool InsideZone(const FVector& Point) const;
};
