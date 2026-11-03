#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrPickupSpawnPoint.generated.h"

/** Optional authored pickup-floor position. Otherwise the manager samples navigation. */
UCLASS()
class BRGAME_API ABrPickupSpawnPoint : public AActor
{
	GENERATED_BODY()
public:
	ABrPickupSpawnPoint();
};
