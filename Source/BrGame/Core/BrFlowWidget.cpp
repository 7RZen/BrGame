#include "BrFlowWidget.h"
#include "BrAudioSettingsPanel.h"
#include "BrMenuWidget.h"
#include "BrFlowRules.h"
#include "BrGameInstance.h"
#include "BrSessionSubsystem.h"
#include "BrGameState.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrLog.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Engine/World.h"
#include "String/LexFromString.h"

void UBrFlowButton::Clicked() { if (OwnerWidget.IsValid()) OwnerWidget->Act(Action, Index); }
FReply UBrFlowWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (AudioPanel && AudioPanel->IsCapturingKey())
	{
		AudioPanel->CaptureMouseKey(Event.GetEffectingButton());
		return FReply::Handled();
	}
	return Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
}
FReply UBrFlowWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (AudioPanel && AudioPanel->IsCapturingKey())
	{
		AudioPanel->CaptureMouseKey(EKeys::MouseWheelAxis);
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(Geometry, Event);
}
void UBrFlowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Refresh();
}
UTextBlock* UBrFlowWidget::Text(UVerticalBox* Box, const FString& Value)
{
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
	Label->SetText(FText::FromString(Value));
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Label->SetAutoWrapText(true);
	Box->AddChildToVerticalBox(Label);
	return Label;
}
UBrFlowButton* UBrFlowWidget::Button(UVerticalBox* Box, const FString& Label, FName Action, int32 Index)
{
	UBrFlowButton* Control = WidgetTree->ConstructWidget<UBrFlowButton>();
	Control->OwnerWidget = this;
	Control->Action = Action;
	Control->Index = Index;
	UTextBlock* Caption = WidgetTree->ConstructWidget<UTextBlock>();
	Caption->SetText(FText::FromString(Label));
	Control->AddChild(Caption);
	Control->OnClicked.AddDynamic(Control, &UBrFlowButton::Clicked);
	Box->AddChildToVerticalBox(Control);
	return Control;
}
UEditableTextBox* UBrFlowWidget::Field(const FString& Label, const FString& Value)
{
	Text(Root, Label);
	UEditableTextBox* Control = WidgetTree->ConstructWidget<UEditableTextBox>();
	Control->SetText(FText::FromString(Value));
	Root->AddChildToVerticalBox(Control);
	return Control;
}
void UBrFlowWidget::Build(int32 NewPhase)
{
	Phase = NewPhase;
	AudioPanel = nullptr;
	TeamsSignature.Empty();
	HostControls.Empty();
	PasswordControls.Empty();
	Teams = nullptr;
	Browser = PasswordPrompt = nullptr;
	PasswordField = PromptField = nullptr;
	BrowserRevision = -1;
	NameField = AddressField = GroupsField = MaximumField = RespawnField = nullptr;
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.03f, 0.96f));
	Background->SetPadding(FMargin(32));
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Root = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Root);
	Background->SetContent(Scroll);
	WidgetTree->RootWidget = Background;
	const UBrGameInstance* GI = GetGameInstance<UBrGameInstance>();
	if (Phase == 0)
	{
		Text(Root, TEXT("BrGame — Host / Join"));
		Button(Root, TEXT("Settings (show / hide)"), TEXT("AudioSettings"));
		AudioPanel = CreateWidget<UBrAudioSettingsPanel>(GetOwningPlayer(), UBrAudioSettingsPanel::StaticClass());
		if (AudioPanel)
		{
			Root->AddChildToVerticalBox(AudioPanel);
			AudioPanel->SetVisibility(ESlateVisibility::Collapsed);
		}
		else UE_LOG(LogBr, Error, TEXT("Cannot create audio settings panel"));
		NameField = Field(TEXT("Player name (saved locally)"), GI ? GI->PlayerName : TEXT("Player"));
		Button(Root, TEXT("Host"), TEXT("Host"));
		SessionLabel = Text(Root, TEXT(""));
		Button(Root, TEXT("Refresh servers"), TEXT("RefreshServers"));
		Browser = WidgetTree->ConstructWidget<UVerticalBox>();
		Root->AddChildToVerticalBox(Browser);
		PasswordPrompt = WidgetTree->ConstructWidget<UVerticalBox>();
		Root->AddChildToVerticalBox(PasswordPrompt);
		Text(PasswordPrompt, TEXT("This server requires a password (invites do not bypass it)."));
		PromptField = WidgetTree->ConstructWidget<UEditableTextBox>();
		PromptField->SetIsPassword(true);
		PasswordPrompt->AddChildToVerticalBox(PromptField);
		Button(PasswordPrompt, TEXT("Connect"), TEXT("PasswordConnect"));
		Button(PasswordPrompt, TEXT("Cancel"), TEXT("PasswordCancel"));
		AddressField = Field(TEXT("IPv4 address[:port]"), TEXT("127.0.0.1:7777"));
		PasswordField = Field(TEXT("IP server password (optional)"), TEXT(""));
		PasswordField->SetIsPassword(true);
		Button(Root, TEXT("Join"), TEXT("Join"));
		Button(Root, TEXT("Quit"), TEXT("Quit"));
	}
	if (Phase == 1)
	{
		Text(Root, TEXT("Lobby"));
		const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
		const FBrMatchSettings Settings = GS ? GS->MatchSettings : FBrMatchSettings();
		auto Setting = [&](const FString& Label, FName Action, TObjectPtr<UTextBlock>& Caption)
		{
			UBrFlowButton* Control = Button(Root, Label, Action);
			Caption = Cast<UTextBlock>(Control->GetContent());
			HostControls.Add(Control);
		};
		Setting(TEXT("Mode"), TEXT("Mode"), ModeLabel);
		Setting(TEXT("Visibility"), TEXT("Visibility"), VisibilityLabel);
		LockLabel = Text(Root, TEXT(""));
		PasswordField = Field(TEXT("Host password (blank removes password; maximum 64 characters)"), TEXT(""));
		PasswordControls.Add(Root->GetChildAt(Root->GetChildrenCount() - 2));
		PasswordControls.Add(PasswordField);
		PasswordField->SetIsPassword(true);
		HostControls.Add(PasswordField);
		UBrFlowButton* PasswordButton = Button(Root, TEXT("Set / remove password"), TEXT("SetPassword"));
		HostControls.Add(PasswordButton);
		PasswordControls.Add(PasswordButton);
		GroupsField = Field(TEXT("Number of groups (2..1024)"), FString::FromInt(Settings.NumGroups));
		HostControls.Add(GroupsField);
		Setting(TEXT("Team selection"), TEXT("Selection"), SelectionLabel);
		Setting(TEXT("Friendly fire"), TEXT("Fire"), FireLabel);
		Setting(TEXT("Revive rule"), TEXT("Revive"), ReviveLabel);
		MaximumField = Field(TEXT("Max players (0 = unlimited)"), FString::FromInt(Settings.MaxPlayers));
		RespawnField = Field(TEXT("Pickup respawn seconds"), FString::SanitizeFloat(Settings.PickupRespawnTime));
		HostControls.Add(MaximumField);
		HostControls.Add(RespawnField);
		HostControls.Add(Button(Root, TEXT("Apply numeric settings"), TEXT("Apply")));
		HostControls.Add(Button(Root, TEXT("Start match"), TEXT("Start")));
		Teams = WidgetTree->ConstructWidget<UVerticalBox>();
		Root->AddChildToVerticalBox(Teams);
		Button(Root, TEXT("Leave"), TEXT("Leave"));
	}
	Status = Text(Root, FString());
	if (Phase == 3) Button(Root, TEXT("Leave"), TEXT("Leave"));
}
void UBrFlowWidget::Refresh()
{
	const ABrGameState* GS = GetWorld() ? GetWorld()->GetGameState<ABrGameState>() : nullptr;
	const int32 NewPhase = !GS ? 0 : GS->bLobby ? 1 : GS->bResults ? 3 : 2;
	if (NewPhase != Phase)
	{
		// Remove the cached Slate tree before reconstructing the native widget hierarchy.
		if (Phase != -1) { RemoveFromParent(); ReleaseSlateResources(true); }
		Build(NewPhase);
		if (!IsInViewport()) AddToViewport();
	}
	if (Phase == 2) { SetVisibility(ESlateVisibility::Collapsed); return; }
	SetVisibility(ESlateVisibility::Visible);
	const UBrGameInstance* GI = GetGameInstance<UBrGameInstance>();
	if (Phase == 0)
	{
		const UBrSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UBrSessionSubsystem>() : nullptr;
		Status->SetText(FText::FromString(GI ? GI->FlowError : TEXT("Missing game instance")));
		SessionLabel->SetText(FText::FromString(Sessions ? Sessions->Status() : TEXT("Online sessions unavailable")));
		PasswordPrompt->SetVisibility(Sessions && Sessions->bPasswordPrompt ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (Sessions && BrowserRevision != Sessions->Revision)
		{
			BrowserRevision = Sessions->Revision;
			Browser->ClearChildren();
			for (int32 I = 0; I < Sessions->Results().Num(); ++I)
				Button(Browser, TEXT("Join: ") + Sessions->Describe(I), TEXT("Server"), I);
		}
		return;
	}
	if (Phase == 3)
	{
		Status->SetColorAndOpacity(FSlateColor(GS->WinnerColor));
		Status->SetText(FText::FromString(GS->WinnerText + FString::Printf(TEXT("\nBack to lobby in %.0fs\n"),
			FMath::Max(0.0f, GS->ResultsEndTime - GS->GetServerWorldTimeSeconds())) + GS->ResultsTable));
		return;
	}
	const ABrPlayerState* Local = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<ABrPlayerState>() : nullptr;
	const bool bHost = Local && Local->bHost;
	for (UWidget* Control : HostControls) if (Control) Control->SetIsEnabled(bHost);
	for (UWidget* Control : PasswordControls) if (Control) Control->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const FBrMatchSettings& Settings = GS->MatchSettings;
	VisibilityLabel->SetText(FText::FromString(Settings.Visibility == EBrSessionVisibility::Public ? TEXT("Visibility: Public") :
		Settings.Visibility == EBrSessionVisibility::FriendsOnly ? TEXT("Visibility: Friends only") : TEXT("Visibility: Invite only")));
	LockLabel->SetText(FText::FromString(Settings.bPasswordProtected ? TEXT("[LOCK] Password protected") : TEXT("No password")));
	ModeLabel->SetText(FText::FromString(Settings.Mode == EBrMode::Grouped ? TEXT("Mode: Grouped") : TEXT("Mode: All vs all")));
	SelectionLabel->SetText(FText::FromString(Settings.TeamSelection == EBrTeamSelection::AutoEven ? TEXT("Teams: Auto even") : TEXT("Teams: Player choice")));
	FireLabel->SetText(FText::FromString(Settings.bFriendlyFire ? TEXT("Friendly fire: On") : TEXT("Friendly fire: Off")));
	FireLabel->GetParent()->SetIsEnabled(bHost && Settings.Mode == EBrMode::Grouped);
	ReviveLabel->SetText(FText::FromString(Settings.ReviveRule == EBrReviveRule::Anyone ? TEXT("Revive: Anyone") : TEXT("Revive: Teammates only")));
	if (!bHost)
	{
		GroupsField->SetText(FText::AsNumber(Settings.NumGroups));
		MaximumField->SetText(FText::AsNumber(Settings.MaxPlayers));
		RespawnField->SetText(FText::AsNumber(Settings.PickupRespawnTime));
	}
	FString List = TEXT("Players:\n");
	TArray<int32> Sizes;
	Sizes.Init(0, Settings.NumGroups);
	for (APlayerState* Entry : GS->PlayerArray)
		if (const ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
		{
			List += FString::Printf(TEXT("%s%s | Group %d\n"), *PS->GetPlayerName(), PS->bHost ? TEXT(" [Host]") : TEXT(""), PS->Team + 1);
			if (Sizes.IsValidIndex(PS->Team)) ++Sizes[PS->Team];
		}
	Status->SetText(FText::FromString(List + (GI ? GI->FlowError : FString())));
	FString Signature = List + FString::FromInt(static_cast<int32>(Settings.Mode)) +
		FString::FromInt(static_cast<int32>(Settings.TeamSelection)) + FString::FromInt(Settings.NumGroups) +
		FString::FromInt(Local && Local->bCanChooseColor);
	for (int32 Color : GS->GroupColors) Signature += FString::Printf(TEXT("|%d"), Color);
	if (Signature == TeamsSignature) return;
	TeamsSignature = Signature;
	Teams->ClearChildren();
	if (Settings.Mode == EBrMode::Grouped)
	{
		for (int32 I = 0; I < Sizes.Num(); ++I)
		{
			UBrFlowButton* Control = Button(Teams, FString::Printf(TEXT("Group %d (%d players)"), I + 1, Sizes[I]), TEXT("Team"), I);
			Control->SetIsEnabled(Settings.TeamSelection == EBrTeamSelection::PlayerChoice);
			if (GS->GroupColors.IsValidIndex(I) && GS->GroupColors[I] >= 0)
				Cast<UTextBlock>(Control->GetContent())->SetColorAndOpacity(FSlateColor(FBrFlowRules::Palette(GS->GroupColors[I])));
		}
		Text(Teams, TEXT("First entrant chooses color (others cannot change it):"));
		for (int32 I = 0; I < 10; ++I)
		{
			UBrFlowButton* Control = Button(Teams, FString::Printf(TEXT("Color %d"), I + 1), TEXT("Color"), I);
			Control->SetIsEnabled(Local && Local->bCanChooseColor);
			Cast<UTextBlock>(Control->GetContent())->SetColorAndOpacity(FSlateColor(FBrFlowRules::Palette(I)));
		}
	}
}
void UBrFlowWidget::Act(FName Action, int32 Index)
{
	if (Action == TEXT("AudioSettings") && AudioPanel)
	{
		AudioPanel->RefreshValues();
		AudioPanel->RefreshBindings();
		AudioPanel->SetVisibility(AudioPanel->GetVisibility() == ESlateVisibility::Collapsed ?
			ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		return;
	}
	UBrGameInstance* GI = GetGameInstance<UBrGameInstance>();
	ABrPlayerController* PC = Cast<ABrPlayerController>(GetOwningPlayer());
	if (!GI || !PC) { UE_LOG(LogBr, Error, TEXT("Flow UI missing game instance/controller")); return; }
	UBrSessionSubsystem* Sessions = GI->GetSubsystem<UBrSessionSubsystem>();
	if (Action == TEXT("RefreshServers")) { Sessions->Find(); return; }
	if (Action == TEXT("Server")) { GI->SaveName(NameField->GetText().ToString()); Sessions->Select(Index); return; }
	if (Action == TEXT("PasswordConnect")) { Sessions->SubmitPassword(PromptField->GetText().ToString()); PromptField->SetText(FText::GetEmpty()); return; }
	if (Action == TEXT("PasswordCancel")) { Sessions->CancelPassword(); PromptField->SetText(FText::GetEmpty()); return; }
	if (Action == TEXT("Host") || Action == TEXT("Join"))
	{
		GI->SaveName(NameField->GetText().ToString());
		if (Action == TEXT("Host")) GI->Host(); else Sessions->JoinIP(AddressField->GetText().ToString(), PasswordField->GetText().ToString());
		PasswordField->SetText(FText::GetEmpty());
		return;
	}
	if (Action == TEXT("Quit")) { Sessions->Quit(); return; }
	if (Action == TEXT("Leave"))
	{
		if (!PC->IsMenuOpen()) PC->ToggleMenu();
		if (UBrMenuWidget* Menu = PC->GetMenuWidget()) Menu->Act(TEXT("Leave"));
		else UE_LOG(LogBr, Error, TEXT("Flow Leave could not open confirmation menu"));
		return;
	}
	if (Action == TEXT("Start")) { PC->ServerStartRound(); return; }
	if (Action == TEXT("Team")) { PC->ServerChooseTeam(Index, INDEX_NONE); return; }
	if (Action == TEXT("Color"))
	{
		if (const ABrPlayerState* PS = PC->GetPlayerState<ABrPlayerState>()) PC->ServerChooseTeam(PS->Team, Index);
		return;
	}
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!GS) return;
	FBrMatchSettings Settings = GS->MatchSettings;
	if (Action == TEXT("Visibility")) Settings.Visibility = static_cast<EBrSessionVisibility>((static_cast<int32>(Settings.Visibility) + 1) % 3);
	if (Action == TEXT("SetPassword"))
	{
		const ABrPlayerState* PS = PC->GetPlayerState<ABrPlayerState>();
		if (!PS || !PS->bHost || GetWorld()->GetNetMode() == NM_Client) return;
		if (!Sessions->SetHostPassword(PasswordField->GetText().ToString())) { GI->FlowError = TEXT("Password must be at most 64 characters."); return; }
		PasswordField->SetText(FText::GetEmpty());
	}
	if (Action == TEXT("Mode")) Settings.Mode = Settings.Mode == EBrMode::AllVsAll ? EBrMode::Grouped : EBrMode::AllVsAll;
	if (Action == TEXT("Selection")) Settings.TeamSelection = Settings.TeamSelection == EBrTeamSelection::AutoEven ? EBrTeamSelection::PlayerChoice : EBrTeamSelection::AutoEven;
	if (Action == TEXT("Fire")) Settings.bFriendlyFire = !Settings.bFriendlyFire;
	if (Action == TEXT("Revive")) Settings.ReviveRule = Settings.ReviveRule == EBrReviveRule::Anyone ? EBrReviveRule::TeammatesOnly : EBrReviveRule::Anyone;
	if (Action == TEXT("Apply"))
	{
		if (!LexTryParseString(Settings.NumGroups, *GroupsField->GetText().ToString()) ||
			!LexTryParseString(Settings.MaxPlayers, *MaximumField->GetText().ToString()) ||
			!LexTryParseString(Settings.PickupRespawnTime, *RespawnField->GetText().ToString()))
		{
			GI->FlowError = TEXT("Numeric settings are invalid.");
			return;
		}
	}
	PC->ServerSettings(Settings);
}
