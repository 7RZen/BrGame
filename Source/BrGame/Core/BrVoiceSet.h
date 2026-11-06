#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BrVoiceSet.generated.h"

class USoundBase;

/** Extend with additional line categories without changing player preferences. */
UCLASS(BlueprintType)
class BRGAME_API UBrVoiceSet : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Voice")
	FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Voice")
	TArray<TSoftObjectPtr<USoundBase>> DownLines;
};
