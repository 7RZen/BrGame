#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrArmorRules.h"
#include "BrArmorComponent.generated.h"

class ABrCharacter;

/** Private controller mirror. Target and values travel together to avoid stale target HUDs. */
USTRUCT()
struct FBrSpectatorArmor
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<ABrCharacter> Pawn;
	UPROPERTY()
	TArray<float> Plates;
};

/** Authority mutates armor; only the pawn owner receives this component's values. */
UCLASS()
class BRGAME_API UBrArmorComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBrArmorComponent();
	const TArray<float>& GetPlates() const { return Plates; }
	bool CanAddPlate() const;
	bool AddPlate();
	FBrArmorRules::FAbsorption Absorb(float Damage, bool bBypass);
	/** Called after possession; never save an empty new pawn over its retained PlayerState. */
	void RestoreFromPlayerState();
	void Clear();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UPROPERTY(Replicated)
	TArray<float> Plates;
	void Publish();
	void PlayBreaks(int32 Count);
};
