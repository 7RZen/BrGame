#include "BrPlayerController.h"
#include "BrMenuWidget.h"
#include "BrMenuRules.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrGameInstance.h"
#include "BrCharacter.h"
#include "BrLifeComponent.h"
#include "BrInventoryComponent.h"
#include "BrCharacterMovementComponent.h"
#include "BrPlayerState.h"
#include "BrDeveloperSettings.h"
#include "BrSafeZone.h"
#include "EngineUtils.h"
#include "BrLog.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"

FString ABrPlayerController::GetBindingLabel(FName MappingName) const
{
	const ULocalPlayer* LP = GetLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* Input = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	const UEnhancedInputUserSettings* User = Input ? Input->GetUserSettings() : nullptr;
	const UEnhancedPlayerMappableKeyProfile* Profile = User ? User->GetActiveKeyProfile() : nullptr;
	if (Profile)
		if (const FKeyMappingRow* Row = Profile->GetPlayerMappingRows().Find(MappingName))
			for (const FPlayerKeyMapping& Mapping : Row->Mappings)
				if (Mapping.GetSlot() == EPlayerMappableKeySlot::First) return Mapping.GetCurrentKey().GetDisplayName().ToString();
	return TEXT("[input unavailable]"); // Input installation already logs missing config/settings.
}

float ABrPlayerController::MenuServerTime() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}
bool ABrPlayerController::IsGameplayInputBlocked() const
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	return IsMenuOpen() || bMapOpen || bLeaving || !GS || GS->bLobby || GS->bResults;
}
void ABrPlayerController::RefreshCursorMode()
{
	if (!IsLocalController() || !GetWorld() || GetWorld()->bIsTearingDown) return;
	bShowMouseCursor = IsGameplayInputBlocked();
	if (bShowMouseCursor)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		if (MenuWidget) Mode.SetWidgetToFocus(MenuWidget->TakeWidget());
		SetInputMode(Mode);
	}
	else
	{
		FInputModeGameOnly Mode;
		// UE's CapturePermanently consumes the click that regains viewport capture.
		// Slate releases capture on deactivation; standard viewport focus-loss handling flushes held input.
		Mode.SetConsumeCaptureMouseDown(true);
		SetInputMode(Mode);
	}
}
void ABrPlayerController::ToggleMenu()
{
	if (!IsLocalController() || bLeaving) return;
	if (MenuDispatchFrame == GFrameCounter) return;
	MenuDispatchFrame = GFrameCounter;
	if (bMapOpen) { CloseMap(); return; }
	// Fixed non-consuming Escape may have dismissed the map earlier this input frame.
	if (MapClosedAt == GetWorld()->GetTimeSeconds()) return;
	if (MenuWidget) { CloseMenu(); return; }
	if (!GetWorld()->GetGameState<ABrGameState>()) return; // Start page already has Settings.
	if (ABrCharacter* BrPawn = Cast<ABrCharacter>(GetPawn()))
	{
		BrPawn->GetInventory()->StopFire();
		BrPawn->ConsumeMovementInputVector();
		BrPawn->GetCharacterMovement()->StopMovementImmediately();
		BrPawn->GetBrMovement()->SetWalking(false);
		BrPawn->GetLife()->StopInteract();
		BrPawn->GetLife()->StopRespawnHold();
	}
	MenuWidget = CreateWidget<UBrMenuWidget>(this);
	if (!MenuWidget) { UE_LOG(LogBr, Error, TEXT("Cannot create in-game menu")); return; }
	MenuWidget->AddToViewport(100);
	MenuWidget->SetKeyboardFocus();
	RefreshCursorMode();
}
void ABrPlayerController::CloseMenu()
{
	if (MenuWidget) MenuWidget->RemoveFromParent();
	MenuWidget = nullptr;
	if (!IsLocalController()) return;
	RefreshCursorMode();
}
void ABrPlayerController::ConfirmLeave()
{
	if (!IsLocalController() || bLeaving) return;
	bLeaving = true;
	bLeaveFailed = false;
	if (HasAuthority()) { FinishLeave(); return; } // Existing host session teardown ends everyone's match.
	LeaveDeadline = GetWorld()->GetRealTimeSeconds() +
		FMath::Clamp(UBrDeveloperSettings::Get()->ForfeitAckTimeout, 0.1f, 10.f);
	GetWorldTimerManager().SetTimer(LeaveTimer, this, &ThisClass::ServiceLeave,
		FMath::Clamp(UBrDeveloperSettings::Get()->ForfeitRetryInterval, 0.1f, 2.f), true);
	ServiceLeave();
}
void ABrPlayerController::ServiceLeave()
{
	const UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (!Driver || !Driver->ServerConnection || Driver->ServerConnection->GetConnectionState() == USOCK_Closed)
	{
		FinishLeave(); // Already disconnected: no live server can acknowledge.
		return;
	}
	if (GetWorld()->GetRealTimeSeconds() >= LeaveDeadline)
	{
		GetWorldTimerManager().ClearTimer(LeaveTimer);
		bLeaving = false;
		bLeaveFailed = true;
		if (MenuWidget) MenuWidget->RefreshStatus();
		UE_LOG(LogBr, Warning, TEXT("Forfeit acknowledgement timed out; awaiting player choice, not disconnecting."));
		return;
	}
	ServerForfeit(); // Idempotent on authority; keep trying while waiting.
}
void ABrPlayerController::LeaveAnyway()
{
	if (IsLocalController() && bLeaveFailed) FinishLeave();
}
void ABrPlayerController::ServerForfeit_Implementation()
{
	CancelUnstuck(TEXT("Leaving game."));
	ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	ClientForfeitAck(Mode && Mode->ForfeitPlayer(this));
}
void ABrPlayerController::ClientForfeitAck_Implementation(bool bAccepted)
{
	if (!bLeaving && !bLeaveFailed) return;
	if (bAccepted) FinishLeave();
	else UE_LOG(LogBr, Warning, TEXT("Forfeit not yet accepted; remaining connected."));
}
void ABrPlayerController::FinishLeave()
{
	GetWorldTimerManager().ClearTimer(LeaveTimer);
	bLeaving = true;
	bLeaveFailed = false;
	if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>()) GI->Leave(TEXT("Left game."));
	else UE_LOG(LogBr, Error, TEXT("Leave failed: no BrGameInstance"));
}
bool ABrPlayerController::CanUnstuck() const
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	const ABrCharacter* BrPawn = Cast<ABrCharacter>(GetPawn());
	const ABrPlayerState* PS = GetPlayerState<ABrPlayerState>();
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	bool bSuddenDeath = Mode && Mode->AreRespawnsDisabled();
	if (!HasAuthority())
		for (TActorIterator<ABrSafeZone> It(GetWorld()); It; ++It) { bSuddenDeath = It->IsSuddenDeath(); break; }
	return FBrUnstuckRules::Eligible(GS && !GS->bLobby && !GS->bResults,
		!HasAuthority() || (Mode && Mode->IsAdmitted(this)),
		BrPawn && BrPawn->GetLife()->IsAlive() && PS && !PS->bEliminated,
		bSuddenDeath);
}
void ABrPlayerController::ServerUnstuck_Implementation()
{
	if (UnstuckEnd > 0) return;
	if (!CanUnstuck() || !HasClientLoadedCurrentWorld())
	{
		UnstuckMessage = TEXT("Unstuck unavailable: requires an alive player in an active match, outside sudden death.");
		ForceNetUpdate();
		return;
	}
	ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	const float Now = MenuServerTime();
	UnstuckCooldownEnd = Mode->GetUnstuckCooldown(this);
	if (Now < UnstuckCooldownEnd)
	{
		UnstuckMessage = TEXT("Unstuck is cooling down.");
		ForceNetUpdate();
		return;
	}
	ABrCharacter* BrPawn = CastChecked<ABrCharacter>(GetPawn());
	BrPawn->GetLife()->CancelRevive();
	UnstuckPawn = BrPawn;
	UnstuckOrigin = BrPawn->GetActorLocation();
	UnstuckPosture = static_cast<uint8>(BrPawn->GetPosture());
	bUnstuckInside = Mode->IsLocationInSafeZone(UnstuckOrigin);
	UnstuckStart = Now;
	UnstuckEnd = Now + FMath::Max(0.1f, UBrDeveloperSettings::Get()->UnstuckHoldTime);
	UnstuckMessage = TEXT("Unstuck: stay still. Damage, movement or combat cancels.");
	GetWorldTimerManager().SetTimer(UnstuckTimer, this, &ThisClass::ServiceUnstuck,
		FMath::Clamp(UBrDeveloperSettings::Get()->UnstuckServiceInterval, 0.01f, 0.25f), true);
	ForceNetUpdate();
}
void ABrPlayerController::CancelUnstuck(const FString& Reason)
{
	if (!HasAuthority() || UnstuckEnd <= 0) return;
	GetWorldTimerManager().ClearTimer(UnstuckTimer);
	UnstuckStart = UnstuckEnd = 0;
	UnstuckPawn.Reset();
	UnstuckMessage = Reason;
	ForceNetUpdate();
}
void ABrPlayerController::ValidateUnstuckMovement()
{
	if (!HasAuthority() || UnstuckEnd <= 0) return;
	const ABrCharacter* BrPawn = UnstuckPawn.Get();
	if (!BrPawn || BrPawn != GetPawn() || FVector::DistSquared(UnstuckOrigin, BrPawn->GetActorLocation()) >
		FMath::Square(FMath::Clamp(UBrDeveloperSettings::Get()->UnstuckMovementTolerance, 1.f, 50.f)))
		CancelUnstuck(TEXT("Unstuck cancelled: movement."));
}
void ABrPlayerController::ServiceUnstuck()
{
	ABrCharacter* BrPawn = UnstuckPawn.Get();
	ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	if (!BrPawn || BrPawn != GetPawn() || !Mode ||
		FBrUnstuckRules::Cancelled(CanUnstuck(), false, false,
			static_cast<uint8>(BrPawn->GetPosture()) != UnstuckPosture,
			FVector::DistSquared(UnstuckOrigin, BrPawn->GetActorLocation()),
			FMath::Clamp(UBrDeveloperSettings::Get()->UnstuckMovementTolerance, 1.f, 50.f),
			bUnstuckInside != Mode->IsLocationInSafeZone(BrPawn->GetActorLocation())))
	{
		CancelUnstuck(TEXT("Unstuck cancelled: movement, posture, life or zone changed."));
		return;
	}
	const float Now = MenuServerTime();
	if (Now < UnstuckEnd) return;
	FVector Destination;
	const bool bSuccess = Mode->FindUnstuckLocation(BrPawn, Destination) &&
		BrPawn->TeleportTo(Destination, BrPawn->GetActorRotation(), false, true);
	if (bSuccess)
	{
		BrPawn->GetCharacterMovement()->StopMovementImmediately();
		BrPawn->ForceNetUpdate();
		UnstuckCooldownEnd = FBrUnstuckRules::CompletionCooldown(true, UnstuckCooldownEnd, Now,
			FMath::Max(0.f, UBrDeveloperSettings::Get()->UnstuckCooldown));
		Mode->SetUnstuckCooldown(this, UnstuckCooldownEnd);
	}
	UE_LOG(LogBr, Display, TEXT("Unstuck completion: %s Success=%d CooldownEnd=%.2f"), *GetName(), bSuccess, UnstuckCooldownEnd);
	CancelUnstuck(bSuccess ? TEXT("Unstuck complete.") : TEXT("No valid open spot found. No cooldown consumed."));
}
