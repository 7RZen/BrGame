#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "BrFlowWidget.generated.h"

class UVerticalBox;
class UTextBlock;
class UEditableTextBox;
class UBrFlowWidget;

UCLASS()
class BRGAME_API UBrFlowButton : public UButton
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UBrFlowWidget> OwnerWidget;
	FName Action;
	int32 Index = INDEX_NONE;
	UFUNCTION()
	void Clicked();
};

UCLASS()
class BRGAME_API UBrFlowWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void Refresh();
	void Act(FName Action, int32 Index);
protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
private:
	UPROPERTY(Transient)
	TObjectPtr<class UBrAudioSettingsPanel> AudioPanel;
	int32 Phase = -1;
	float AnnouncementStartedTime = -1;
	FString TeamsSignature;
	FName PreviewMapId;
	UPROPERTY(Transient)
	TObjectPtr<class UImage> MapPreview;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MapPlaceholder;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MapLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MapDetails;
	int32 BrowserRevision = -1;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Root;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Teams;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NameField;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> AddressField;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> GroupsField;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> MaximumField;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> RespawnField;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> PasswordField;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Browser;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> PasswordPrompt;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> PromptField;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SessionLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VisibilityLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LockLabel;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> HostControls;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> PasswordControls;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ModeLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SelectionLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FireLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ReviveLabel;
	void Build(int32 NewPhase);
	UTextBlock* Text(UVerticalBox* Box, const FString& Value);
	UEditableTextBox* Field(const FString& Label, const FString& Value);
	UBrFlowButton* Button(UVerticalBox* Box, const FString& Label, FName Action, int32 Index = INDEX_NONE);
};
