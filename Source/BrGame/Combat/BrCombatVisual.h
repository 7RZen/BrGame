#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrCombatVisual.generated.h"

class UStaticMeshComponent;

/** Local, collision-free placeholder. Never replicated and never participates in gameplay. */
UCLASS(NotBlueprintable, Transient)
class BRGAME_API ABrCombatVisual : public AActor
{
	GENERATED_BODY()
public:
	ABrCombatVisual();
	void InitTracer(const FVector& Direction, float Speed, float Range);
	void InitExplosion(float Radius);
	virtual void Tick(float DeltaSeconds) override;
private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;
	FVector Velocity = FVector::ZeroVector;
	float ExplosionRadius = 0;
	float Age = 0;
};
