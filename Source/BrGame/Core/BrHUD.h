#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BrViewLayoutRules.h"
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
	void DrawPanels();
	void PanelText(const FString& Text, FLinearColor Color, bool bRight, float& Y, int32 MaxLines = 12);
	void PanelMeter(float Fraction, FLinearColor Color, bool bRight, float& Y);
	FBrViewLayoutRules ViewLayout;
	float HudScale = 1;
	float RightStatusY = 0;
	void DrawCompass();
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
