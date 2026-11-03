#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "BrMenuWidget.generated.h"

class UBrAudioSettingsPanel;
class UVerticalBox;
class UTextBlock;
class UBrMenuWidget;

UCLASS()
class BRGAME_API UBrMenuButton : public UButton
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UBrMenuWidget> Menu;
	FName Action;
	UFUNCTION()
	void Clicked();
};

/** Local overlay only. Never pauses the world. */
UCLASS()
class BRGAME_API UBrMenuWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void Act(FName Action);
	void RefreshStatus();
protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
private:
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Root;
	UPROPERTY(Transient)
	TObjectPtr<UBrAudioSettingsPanel> Settings;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Confirmation;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> LeaveFailure;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient)
	TObjectPtr<UBrMenuButton> UnstuckButton;
	bool IsCloseKey(FKey Key) const;
	void Button(UVerticalBox* Box, const FString& Caption, FName Action);
};
