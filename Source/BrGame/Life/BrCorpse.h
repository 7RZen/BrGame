#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrInventoryComponent.h"
#include "BrCorpse.generated.h"

class USkeletalMeshComponent;

/** Permanent round-local reference-pose mannequin, lying bone-white; no animation, collision or tick. */
UCLASS()
class BRGAME_API ABrCorpse : public AActor
{
	GENERATED_BODY()
public:
	ABrCorpse();
	/** Server-only gear snapshot, shared by normal elimination and intentional forfeit. */
	void SetGear(const FBrInventoryState& State) { if (HasAuthority()) Gear = State; }
	const FBrInventoryState& GetGear() const { return Gear; }
protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY()
	FBrInventoryState Gear;
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;
};
