#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrPickupManager.generated.h"

class ABrPickup;
class UBrPickupTable;

USTRUCT()
struct FBrPickupSlot
{
	GENERATED_BODY()
	FTransform Transform;
	UPROPERTY()
	TObjectPtr<ABrPickup> Pickup;
	float ReadyTime = 0;
};

/** Server-only slot ownership. One deadline timer, no actor tick or per-frame replication. */
UCLASS()
class BRGAME_API ABrPickupManager : public AActor
{
	GENERATED_BODY()
public:
	ABrPickupManager();
	void Initialize(int32 PlayersAtStart);
	void RespawnAllPickups();
	void RegisterRoundDrop(ABrPickup* Pickup);
	/** Step-6 round reset hook: clears slots, pending timers and all registered drops, including lifetime=0. */
	void ClearRoundPickups();
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY()
	TObjectPtr<UBrPickupTable> Table;
	UPROPERTY()
	TArray<FBrPickupSlot> Slots;
	FRandomStream Stream;
	FTimerHandle RespawnTimer;
	FTimerHandle PlacementTimer;
	int32 InitialPlayers = 0;
	int32 NavigationWaits = 0;
	bool bInitialized = false;
	TSet<TWeakObjectPtr<ABrPickup>> RoundDrops;
	UFUNCTION()
	void RoundDropDestroyed(AActor* DestroyedActor);
	void PlaceSlots();
	void Consumed(ABrPickup* Pickup);
	bool Reroll(FBrPickupSlot& Slot);
	void ServiceRespawns();
	void ScheduleNext();
	float RespawnDelay() const;
};
