#include "BrPickupSpawnPoint.h"
#include "Components/SceneComponent.h"

ABrPickupSpawnPoint::ABrPickupSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}
