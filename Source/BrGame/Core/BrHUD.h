#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BrHUD.generated.h"

class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;

UCLASS()
class BRGAME_API ABrHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	bool ExportMapPreview();

private:
	void DrawMinimap();
	bool InitializeMap();
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MapImage;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MapMaterial;
	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> MapCapture;
	TWeakObjectPtr<UWorld> MapWorld;
	FBox MapBounds = FBox(ForceInit);
	bool bMapFailed = false;
};
