#include "BrAudioSettingsPanel.h"
#include "BrGameInstance.h"
#include "BrAudioSubsystem.h"
#include "BrVoiceSet.h"
#include "Engine/World.h"
#include "BrLog.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "BrMenuRules.h"
#include "BrDeveloperSettings.h"
#include "BrInputConfig.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"

void UBrAudioSettingsPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Box;
	MasterLabel = WidgetTree->ConstructWidget<UTextBlock>();
	FSlateFontInfo SmallFont = MasterLabel->GetFont();
	SmallFont.Size = 14;
	MasterLabel->SetFont(SmallFont);
	Box->AddChildToVerticalBox(MasterLabel);
	Master = WidgetTree->ConstructWidget<USlider>();
	Master->SetStepSize(0.01f);
	Box->AddChildToVerticalBox(Master);
	EffectsLabel = WidgetTree->ConstructWidget<UTextBlock>();
	EffectsLabel->SetFont(SmallFont);
	Box->AddChildToVerticalBox(EffectsLabel);
	Effects = WidgetTree->ConstructWidget<USlider>();
	Effects->SetStepSize(0.01f);
	Box->AddChildToVerticalBox(Effects);
	VoiceLabel = WidgetTree->ConstructWidget<UTextBlock>();
	VoiceLabel->SetFont(SmallFont);
	Box->AddChildToVerticalBox(VoiceLabel);
	Voice = WidgetTree->ConstructWidget<USlider>();
	Voice->SetStepSize(0.01f);
	Box->AddChildToVerticalBox(Voice);
	Voice->OnValueChanged.AddDynamic(this, &UBrAudioSettingsPanel::VoiceChanged);
	UButton* VoiceSelection = WidgetTree->ConstructWidget<UButton>();
	VoiceSelectionLabel = WidgetTree->ConstructWidget<UTextBlock>();
	VoiceSelectionLabel->SetFont(SmallFont);
	VoiceSelection->AddChild(VoiceSelectionLabel);
	VoiceSelection->OnClicked.AddDynamic(this, &UBrAudioSettingsPanel::NextVoice);
	Box->AddChildToVerticalBox(VoiceSelection);
	SensitivityLabel = WidgetTree->ConstructWidget<UTextBlock>();
	SensitivityLabel->SetFont(SmallFont);
	Box->AddChildToVerticalBox(SensitivityLabel);
	Sensitivity = WidgetTree->ConstructWidget<USlider>();
	Sensitivity->SetMinValue(0.02f);
	Sensitivity->SetMaxValue(2.f);
	Sensitivity->SetStepSize(0.01f);
	Box->AddChildToVerticalBox(Sensitivity);
	Sensitivity->OnValueChanged.AddDynamic(this, &UBrAudioSettingsPanel::SensitivityChanged);
	Master->OnValueChanged.AddDynamic(this, &UBrAudioSettingsPanel::MasterChanged);
	Effects->OnValueChanged.AddDynamic(this, &UBrAudioSettingsPanel::EffectsChanged);
	RefreshValues();
	BindingStatus = WidgetTree->ConstructWidget<UTextBlock>();
	BindingStatus->SetFont(SmallFont);
	BindingStatus->SetAutoWrapText(true);
	BindingStatus->SetText(FText::FromString(TEXT("Bindings: click a key, then press keyboard/mouse (Esc cancels). Conflicts rejected.")));
	Box->AddChildToVerticalBox(BindingStatus);
	BindingsBox = WidgetTree->ConstructWidget<UVerticalBox>();
	Box->AddChildToVerticalBox(BindingsBox);
	RefreshBindings();
}

void UBrAudioSettingsPanel::RefreshValues()
{
	const UBrGameInstance* GI = GetGameInstance<UBrGameInstance>();
	if (!GI || !Master || !Effects)
	{
		UE_LOG(LogBr, Error, TEXT("Audio panel has no game instance or controls"));
		return;
	}
	Master->SetValue(GI->GetMasterVolume());
	Effects->SetValue(GI->GetEffectsVolume());
	Voice->SetValue(GI->GetVoiceVolume());
	VoiceLabel->SetText(FText::FromString(FString::Printf(TEXT("Voice: %.0f%%"), GI->GetVoiceVolume() * 100)));
	UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>();
	UBrVoiceSet* Set = Audio ? Audio->VoiceSet(GI->GetVoiceIndex()) : nullptr;
	const FString Name = Set ? Set->DisplayName.ToString() : FString::Printf(TEXT("Voice %d (unavailable)"), GI->GetVoiceIndex() + 1);
	VoiceSelectionLabel->SetText(FText::FromString(TEXT("My voice: ") + Name + TEXT("  (click to cycle)")));
	Sensitivity->SetValue(GI->GetMouseSensitivity());
	SensitivityLabel->SetText(FText::FromString(FString::Printf(TEXT("Mouse sensitivity: %.2f"), GI->GetMouseSensitivity())));
	MasterLabel->SetText(FText::FromString(FString::Printf(TEXT("Master: %.0f%%"), GI->GetMasterVolume() * 100)));
	EffectsLabel->SetText(FText::FromString(FString::Printf(TEXT("Effects: %.0f%%"), GI->GetEffectsVolume() * 100)));
}

void UBrAudioSettingsPanel::MasterChanged(float Value)
{
	if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>()) GI->SetAudioVolumes(Value, GI->GetEffectsVolume());
	RefreshValues();
}
void UBrAudioSettingsPanel::EffectsChanged(float Value)
{
	if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>()) GI->SetAudioVolumes(GI->GetMasterVolume(), Value);
	RefreshValues();
}
void UBrAudioSettingsPanel::SensitivityChanged(float Value)
{
	if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>()) GI->SetMouseSensitivity(Value);
	RefreshValues();
}

void UBrAudioSettingsPanel::VoiceChanged(float Value)
{
	if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>()) GI->SetVoiceVolume(Value);
	RefreshValues();
}

void UBrAudioSettingsPanel::NextVoice()
{
	if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>())
	{
		const int32 Count = UBrDeveloperSettings::Get()->VoiceSets.Num();
		GI->SetVoiceIndex(Count > 0 ? (GI->GetVoiceIndex() + 1) % Count : 0);
	}
	RefreshValues();
}

namespace
{
	UEnhancedInputUserSettings* UserSettings(UBrAudioSettingsPanel* Panel)
	{
		ULocalPlayer* LP = Panel->GetOwningLocalPlayer();
		UEnhancedInputLocalPlayerSubsystem* Input = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		UEnhancedInputUserSettings* User = Input ? Input->GetUserSettings() : nullptr;
		UBrInputConfig* Config = UBrDeveloperSettings::Get()->InputConfig.LoadSynchronous();
		if (!User || !Config || !Config->DefaultMappingContext) return nullptr;
		if (!User->IsMappingContextRegistered(Config->DefaultMappingContext) &&
			!User->RegisterInputMappingContext(Config->DefaultMappingContext))
		{
			UE_LOG(LogBr, Error, TEXT("Settings: input mapping registration failed"));
			return nullptr;
		}
		return User;
	}
}
void UBrBindingButton::Clicked() { if (Panel.IsValid()) Panel->SelectBinding(MappingName); }
void UBrAudioSettingsPanel::RefreshBindings()
{
	if (!BindingsBox) return;
	BindingsBox->ClearChildren();
	UEnhancedInputUserSettings* User = UserSettings(this);
	const UEnhancedPlayerMappableKeyProfile* Profile = User ? User->GetActiveKeyProfile() : nullptr;
	if (!Profile)
	{
		BindingStatus->SetText(FText::FromString(TEXT("Input settings unavailable.")));
		UE_LOG(LogBr, Error, TEXT("Settings: Enhanced Input user profile missing"));
		return;
	}
	TArray<FName> Names;
	Profile->GetPlayerMappingRows().GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	for (FName Name : Names)
	{
		// Old saved profiles may retain the removed action. It is not registered by the
		// current IMC, must not appear in UI, and must not reserve LeftShift.
		if (Name == TEXT("Br.Walk")) continue;
		for (const FPlayerKeyMapping& Mapping : Profile->GetPlayerMappingRows().FindChecked(Name).Mappings)
		{
			if (Mapping.GetSlot() != EPlayerMappableKeySlot::First || Mapping.GetCurrentKey() == EKeys::MouseX ||
				Mapping.GetCurrentKey() == EKeys::Mouse2D) continue;
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = 14;
			Label->SetFont(Font);
			Label->SetText(FText::FromString(Mapping.GetDisplayName().ToString() + TEXT(" : ")));
			Row->AddChildToHorizontalBox(Label);
			UBrBindingButton* Button = WidgetTree->ConstructWidget<UBrBindingButton>();
			Button->Panel = this;
			Button->MappingName = Name;
			UTextBlock* Key = WidgetTree->ConstructWidget<UTextBlock>();
			Key->SetFont(Font);
			Key->SetText(Mapping.GetCurrentKey().GetDisplayName());
			Button->AddChild(Key);
			Button->OnClicked.AddDynamic(Button, &UBrBindingButton::Clicked);
			Row->AddChildToHorizontalBox(Button);
			BindingsBox->AddChildToVerticalBox(Row);
		}
	}
	UBrBindingButton* Reset = WidgetTree->ConstructWidget<UBrBindingButton>();
	Reset->Panel = this;
	Reset->MappingName = TEXT("Reset");
	UTextBlock* Caption = WidgetTree->ConstructWidget<UTextBlock>();
	FSlateFontInfo Font = Caption->GetFont();
	Font.Size = 14;
	Caption->SetFont(Font);
	Caption->SetText(FText::FromString(TEXT("Reset bindings to defaults")));
	Reset->AddChild(Caption);
	Reset->OnClicked.AddDynamic(Reset, &UBrBindingButton::Clicked);
	BindingsBox->AddChildToVerticalBox(Reset);
}
void UBrAudioSettingsPanel::SelectBinding(FName Name)
{
	if (Name == TEXT("Reset"))
	{
		UEnhancedInputUserSettings* User = UserSettings(this);
		if (!User || !User->GetActiveKeyProfile()) return;
		FGameplayTagContainer Errors;
		for (const auto& Row : User->GetActiveKeyProfile()->GetPlayerMappingRows())
		{
			if (Row.Key == TEXT("Br.Walk")) continue;
			FMapPlayerKeyArgs Args;
			Args.MappingName = Row.Key;
			User->ResetAllPlayerKeysInRow(Args, Errors);
		}
		User->ApplySettings();
		User->SaveSettings();
		BindingStatus->SetText(FText::FromString(Errors.IsEmpty() ? TEXT("Defaults restored.") : Errors.ToString()));
		PendingBinding = NAME_None;
		RefreshBindings();
		return;
	}
	PendingBinding = Name;
	BindingStatus->SetText(FText::FromString(TEXT("Press a key for ") + Name.ToString() + TEXT(" (Esc cancels, wheel selects zoom axis).")));
	SetKeyboardFocus();
}
void UBrAudioSettingsPanel::CommitBinding(FKey Key)
{
	UEnhancedInputUserSettings* User = UserSettings(this);
	if (!User || !User->GetActiveKeyProfile()) { PendingBinding = NAME_None; return; }
	TMap<FName, FKey> Current;
	for (const auto& Row : User->GetActiveKeyProfile()->GetPlayerMappingRows())
	{
		if (Row.Key == TEXT("Br.Walk")) continue;
		for (const FPlayerKeyMapping& Mapping : Row.Value.Mappings)
			Current.Add(Row.Key, Mapping.GetCurrentKey());
	}
	FString Error;
	if (FBrMenuRules::CanBind(PendingBinding, Key, Current, Error))
	{
		FMapPlayerKeyArgs Args;
		Args.MappingName = PendingBinding;
		Args.Slot = EPlayerMappableKeySlot::First;
		Args.NewKey = Key;
		FGameplayTagContainer Errors;
		User->MapPlayerKey(Args, Errors);
		if (Errors.IsEmpty())
		{
			User->ApplySettings();
			User->SaveSettings();
			Error = TEXT("Binding saved.");
		}
		else { Error = Errors.ToString(); UE_LOG(LogBr, Error, TEXT("Rebinding failed: %s"), *Error); }
	}
	PendingBinding = NAME_None;
	BindingStatus->SetText(FText::FromString(Error));
	RefreshBindings();
}
FReply UBrAudioSettingsPanel::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (!IsCapturingKey()) return Super::NativeOnPreviewKeyDown(Geometry, Event);
	if (Event.GetKey() == EKeys::Tilde || Event.GetKey() == EKeys::Period)
	{
		PendingBinding = NAME_None;
		BindingStatus->SetText(FText::FromString(TEXT("Console keys are reserved; selection cancelled.")));
		return FReply::Unhandled();
	}
	if (Event.GetKey() == EKeys::Escape)
	{
		PendingBinding = NAME_None;
		BindingStatus->SetText(FText::FromString(TEXT("Rebinding cancelled.")));
	}
	else CommitBinding(Event.GetKey());
	return FReply::Handled();
}
FReply UBrAudioSettingsPanel::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!IsCapturingKey()) return Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
	CommitBinding(Event.GetEffectingButton());
	return FReply::Handled();
}
FReply UBrAudioSettingsPanel::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!IsCapturingKey()) return Super::NativeOnMouseWheel(Geometry, Event);
	CommitBinding(EKeys::MouseWheelAxis);
	return FReply::Handled();
}
