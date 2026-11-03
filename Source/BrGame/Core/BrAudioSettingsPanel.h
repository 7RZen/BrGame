#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "BrAudioSettingsPanel.generated.h"

class USlider;
class UTextBlock;
class UVerticalBox;
class UBrAudioSettingsPanel;

UCLASS()
class BRGAME_API UBrBindingButton : public UButton
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UBrAudioSettingsPanel> Panel;
	FName MappingName;
	UFUNCTION()
	void Clicked();
};

/** Embeddable panel: no viewport, input mode, pause or Escape ownership. Values save on change. */
UCLASS()
class BRGAME_API UBrAudioSettingsPanel : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Settings|Audio")
	void RefreshValues();
	void RefreshBindings();
	void SelectBinding(FName Name);
	bool IsCapturingKey() const { return !PendingBinding.IsNone(); }
	void CaptureMouseKey(FKey Key) { if (IsCapturingKey()) CommitBinding(Key); }
protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
private:
	FName PendingBinding;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> BindingsBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BindingStatus;
	void CommitBinding(FKey Key);
	UPROPERTY(Transient)
	TObjectPtr<USlider> Master;
	UPROPERTY(Transient)
	TObjectPtr<USlider> Effects;
	UPROPERTY(Transient)
	TObjectPtr<USlider> Sensitivity;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SensitivityLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MasterLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EffectsLabel;
	UFUNCTION()
	void MasterChanged(float Value);
	UFUNCTION()
	void EffectsChanged(float Value);
	UFUNCTION()
	void SensitivityChanged(float Value);
};
