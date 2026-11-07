#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BrPenaltyDefinition.generated.h"

class UNiagaraSystem;
class USoundBase;

UENUM(BlueprintType)
enum class EBrPenaltyTrail : uint8 { None, Flame, Smoke };

/** IDs are open-ended; gameplay is data driven rather than a closed item enum. */
UCLASS(BlueprintType)
class BRGAME_API UBrPenaltyDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty")
	FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty")
	FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty", meta=(ClampMin="0.1", ClampMax="300", Units="s"))
	float Duration = 30;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty", meta=(ClampMin="0.1", ClampMax="4"))
	float SpeedMultiplier = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty")
	bool bForcedForward = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty")
	bool bDisablesSprint = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty")
	bool bForcesStanding = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty")
	FLinearColor TubeColor = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty")
	EBrPenaltyTrail Trail = EBrPenaltyTrail::None;
	/** Optional replacement for bounded mesh puffs. Must not spawn independent fog-exempt actors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty")
	TSoftObjectPtr<UNiagaraSystem> VFX;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Penalty")
	TSoftObjectPtr<USoundBase> LoopSound;
};

USTRUCT()
struct FBrPenaltyState
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<UBrPenaltyDefinition> Definition;
	UPROPERTY()
	float StartTime = 0;
	UPROPERTY()
	float EndTime = 0;
	UPROPERTY()
	bool bPlantedThisDown = false;
};
