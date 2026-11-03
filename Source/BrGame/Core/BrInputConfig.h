#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BrInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS()
class BRGAME_API UBrInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> LookAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ProneAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> FireAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ThrowGrenadeAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ReloadAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> RespawnHoldAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ScoreboardAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MenuAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MapAction;
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ZoomAction;
};
