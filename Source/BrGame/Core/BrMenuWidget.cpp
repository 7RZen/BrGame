#include "BrMenuWidget.h"
#include "BrAudioSettingsPanel.h"
#include "BrPlayerController.h"
#include "BrGameState.h"
#include "BrInputConfig.h"
#include "BrDeveloperSettings.h"
#include "BrLog.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"

void UBrMenuButton::Clicked() { if (Menu.IsValid()) Menu->Act(Action); }
void UBrMenuWidget::Button(UVerticalBox* Box, const FString& Caption, FName Action)
{
	UBrMenuButton* Control = WidgetTree->ConstructWidget<UBrMenuButton>();
	Control->Menu = this;
	Control->Action = Action;
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = 18;
	Label->SetFont(Font);
	Label->SetText(FText::FromString(Caption));
	Control->AddChild(Label);
	Control->OnClicked.AddDynamic(Control, &UBrMenuButton::Clicked);
	Box->AddChildToVerticalBox(Control);
	if (Action == TEXT("Unstuck")) UnstuckButton = Control;
}
void UBrMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.03f, 0.94f));
	Background->SetPadding(FMargin(32));
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Root = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Root);
	Background->SetContent(Scroll);
	WidgetTree->RootWidget = Background;
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("Menu — online game continues")));
	Root->AddChildToVerticalBox(Title);
	const ABrGameState* State = GetWorld()->GetGameState<ABrGameState>();
	const ABrPlayerController* Owner = Cast<ABrPlayerController>(GetOwningPlayer());
	Button(Root, State && !State->bLobby && !State->bResults && Owner && !Owner->IsFlowSpectating() ?
		TEXT("Resume") : TEXT("Close menu"), TEXT("Resume"));
	Button(Root, TEXT("Settings (show / hide)"), TEXT("Settings"));
	Settings = CreateWidget<UBrAudioSettingsPanel>(GetOwningPlayer());
	if (Settings)
	{
		Root->AddChildToVerticalBox(Settings);
		Settings->SetVisibility(ESlateVisibility::Collapsed);
	}
	else UE_LOG(LogBr, Error, TEXT("Menu could not create settings panel"));
	const ABrPlayerController* PC = Cast<ABrPlayerController>(GetOwningPlayer());
	Button(Root, FString::Printf(TEXT("Unstuck — stay still %.1fs"), UBrDeveloperSettings::Get()->UnstuckHoldTime), TEXT("Unstuck"));
	Status = WidgetTree->ConstructWidget<UTextBlock>();
	Root->AddChildToVerticalBox(Status);
	Button(Root, TEXT("Leave game..."), TEXT("Leave"));
	Confirmation = WidgetTree->ConstructWidget<UVerticalBox>();
	Root->AddChildToVerticalBox(Confirmation);
	UTextBlock* Warning = WidgetTree->ConstructWidget<UTextBlock>();
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	Warning->SetText(FText::FromString(PC && PC->HasAuthority() ?
		TEXT("Leave? The match ends for everyone.") : GS && !GS->bLobby && !GS->bResults ?
		TEXT("Leave? You immediately forfeit this round. Rejoining will not restore you.") : TEXT("Leave this game?")));
	Confirmation->AddChildToVerticalBox(Warning);
	Button(Confirmation, TEXT("Confirm leave"), TEXT("Confirm"));
	Button(Confirmation, TEXT("Cancel"), TEXT("Cancel"));
	Confirmation->SetVisibility(ESlateVisibility::Collapsed);
	LeaveFailure = WidgetTree->ConstructWidget<UVerticalBox>();
	Root->AddChildToVerticalBox(LeaveFailure);
	UTextBlock* FailureText = WidgetTree->ConstructWidget<UTextBlock>();
	FailureText->SetText(FText::FromString(TEXT("Couldn't reach the host\nLeave anyway disconnects like closing the game; normal disconnect/rejoin rules apply.")));
	FailureText->SetAutoWrapText(true);
	LeaveFailure->AddChildToVerticalBox(FailureText);
	Button(LeaveFailure, TEXT("Try again"), TEXT("Retry"));
	Button(LeaveFailure, TEXT("Leave anyway"), TEXT("Anyway"));
	RefreshStatus();
}
void UBrMenuWidget::RefreshStatus()
{
	const ABrPlayerController* PC = Cast<ABrPlayerController>(GetOwningPlayer());
	if (!PC || !Status || !UnstuckButton) return;
	LeaveFailure->SetVisibility(PC->HasLeaveFailed() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (PC->HasLeaveFailed() || PC->IsLeaveWaiting()) Confirmation->SetVisibility(ESlateVisibility::Collapsed);
	Root->SetIsEnabled(!PC->IsLeaveWaiting());
	UnstuckButton->SetVisibility(PC->CanUnstuck() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const float Remaining = FMath::Max(0.f, PC->UnstuckCooldownEnd - PC->MenuServerTime());
	UnstuckButton->SetIsEnabled(Remaining <= 0 && PC->UnstuckEnd <= 0);
	Status->SetText(FText::FromString(FString::Printf(TEXT("%s%s"), *PC->UnstuckMessage,
		Remaining > 0 ? *FString::Printf(TEXT("\nUnstuck cooldown: %.1fs"), Remaining) : TEXT(""))));
}
void UBrMenuWidget::Act(FName Action)
{
	ABrPlayerController* PC = Cast<ABrPlayerController>(GetOwningPlayer());
	if (!PC) return;
	if (Action == TEXT("Resume")) PC->CloseMenu();
	else if (Action == TEXT("Settings") && Settings)
	{
		Settings->RefreshBindings();
		Settings->SetVisibility(Settings->GetVisibility() == ESlateVisibility::Collapsed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	else if (Action == TEXT("Unstuck"))
	{
		PC->ServerUnstuck();
		PC->CloseMenu(); // Progress on unobscured HUD; gameplay resumes and can cancel.
	}
	else if (Action == TEXT("Leave")) Confirmation->SetVisibility(ESlateVisibility::Visible);
	else if (Action == TEXT("Cancel")) Confirmation->SetVisibility(ESlateVisibility::Collapsed);
	else if (Action == TEXT("Confirm") || Action == TEXT("Retry")) { PC->ConfirmLeave(); RefreshStatus(); }
	else if (Action == TEXT("Anyway")) PC->LeaveAnyway();
}
FReply UBrMenuWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Settings && Settings->IsCapturingKey()) return Super::NativeOnPreviewKeyDown(Geometry, Event);
	if (IsCloseKey(Event.GetKey()))
	{
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(GetOwningPlayer())) PC->ToggleMenu();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event); // Tilde/Period reach viewport console.
}
bool UBrMenuWidget::IsCloseKey(FKey Key) const
{
	bool bClose = Key == EKeys::Escape;
	if (const ULocalPlayer* LP = GetOwningLocalPlayer())
		if (const UEnhancedInputLocalPlayerSubsystem* Input = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			if (const UEnhancedInputUserSettings* User = Input->GetUserSettings())
				if (const UEnhancedPlayerMappableKeyProfile* Profile = User->GetActiveKeyProfile())
					if (const FKeyMappingRow* Row = Profile->GetPlayerMappingRows().Find(TEXT("Br.Menu")))
						for (const FPlayerKeyMapping& Mapping : Row->Mappings) bClose |= Mapping.GetCurrentKey() == Key;
	return bClose;
}
FReply UBrMenuWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Settings && Settings->IsCapturingKey())
	{
		Settings->CaptureMouseKey(Event.GetEffectingButton());
		return FReply::Handled();
	}
	// Let buttons consume their clicks before a mouse-bound Menu action reaches
	// the viewport. Otherwise binding Menu to LMB would make Settings/Leave unusable.
	return Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
}
FReply UBrMenuWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Settings && Settings->IsCapturingKey())
	{
		Settings->CaptureMouseKey(EKeys::MouseWheelAxis);
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(Geometry, Event);
}
