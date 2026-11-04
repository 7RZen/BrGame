#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrPickupManager.generated.h"

class ABrPickup;
class ABrSupplyCrate;
class UBrPickupDefinition;

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
	FRandomStream Stream;
	FTimerHandle DropTimer;
	int32 NavigationWaits = 0;
	bool bInitialized = false;
	TSet<TWeakObjectPtr<ABrPickup>> RoundDrops;
	TSet<TWeakObjectPtr<ABrSupplyCrate>> Crates;
	UFUNCTION()
	void RoundDropDestroyed(AActor* DestroyedActor);
	UFUNCTION()
	void CrateDestroyed(AActor* DestroyedActor);
	void InitialWave();
	void DropOne();
	void ServiceDrop();
	void ScheduleNext();
	bool ClearGround(const FVector& Candidate, FVector& Floor, bool bCrate, const AActor* Ignore = nullptr) const;
	bool InsideZone(const FVector& Point) const;
};
