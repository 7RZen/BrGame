#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrZoneBounds.generated.h"

class UBoxComponent;

/** Optional authored playable XY rectangle. The zone initially encloses all its corners. */
UCLASS()
class BRGAME_API ABrZoneBounds : public AActor
{
	GENERATED_BODY()
public:
	ABrZoneBounds();
	FBox GetPlayableBounds() const;
	/** Shared fog/zone map rectangle; world XY, independent of view yaw and zoom. */
	static FBox FindPlayableBounds(UWorld* World);
private:
	UPROPERTY(VisibleAnywhere, Category="Zone")
	TObjectPtr<UBoxComponent> Bounds;
};
