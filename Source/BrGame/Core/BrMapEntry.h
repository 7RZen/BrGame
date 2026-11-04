#pragma once

#include "CoreMinimal.h"
#include "BrMapEntry.generated.h"

class UWorld;
class UTexture2D;

/** Developer allowlist. Random is reserved for the server-side round-start draw. */
USTRUCT(BlueprintType)
struct BRGAME_API FBrMapEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Maps")
	FName Id;
	UPROPERTY(EditAnywhere, Category="Maps")
	FText DisplayName;
	UPROPERTY(EditAnywhere, Category="Maps")
	TSoftObjectPtr<UWorld> Map;
	UPROPERTY(EditAnywhere, Category="Maps", meta=(MultiLine=true))
	FText Description;
	UPROPERTY(EditAnywhere, Category="Maps")
	TSoftObjectPtr<UTexture2D> Preview;
	UPROPERTY(EditAnywhere, Category="Maps", meta=(ClampMin="1"))
	int32 RecommendedMinPlayers = 2;
	UPROPERTY(EditAnywhere, Category="Maps", meta=(ClampMin="1"))
	int32 RecommendedMaxPlayers = 16;
	/** Crates requested at round start on this map. Zero disables only the initial wave. */
	UPROPERTY(EditAnywhere, Category="Maps|Crates", meta=(ClampMin="0", ClampMax="100"))
	int32 InitialCrates = 4;
	/** Crates requested each host CrateDropInterval. Each has an independent bounded landing search. */
	UPROPERTY(EditAnywhere, Category="Maps|Crates", meta=(ClampMin="1", ClampMax="100"))
	int32 CratesPerDrop = 2;
};
