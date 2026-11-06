#include "BrPlayerController.h"
#include "BrAudioSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "BrFogComponent.h"
#include "BrVisibilitySubsystem.h"
#include "EngineUtils.h"
#include "BrSafeZone.h"
#include "BrDeveloperSettings.h"
#include "BrInputConfig.h"
#include "BrLog.h"
#include "BrCheatManager.h"
#include "BrDevCheatPolicy.h"
#include "BrCharacter.h"
#include "BrInventoryComponent.h"
#include "BrHealthComponent.h"
#include "BrLifeComponent.h"
#include "BrPlayerState.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrFlowWidget.h"
#include "BrMenuWidget.h"
#include "BrGameInstance.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "BrPickup.h"
#include "BrPickupManager.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/ActorChannel.h"
#include "Engine/PackageMapClient.h"
#include "Engine/NetworkObjectList.h"
#include "String/LexFromString.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "UserSettings/EnhancedInputUserSettings.h"

ABrPlayerController::ABrPlayerController()
{
	Fog = CreateDefaultSubobject<UBrFogComponent>(TEXT("Fog"));
	bShowMouseCursor = false;
	CheatClass = UBrCheatManager::StaticClass();
}

void ABrPlayerController::GetAudioListenerPosition(FVector& Location, FVector& Front, FVector& Right) const
{
	Super::GetAudioListenerPosition(Location, Front, Right);
	if (GetViewTarget() && GetViewTarget()->IsA<ABrCharacter>())
	{
		Location = UBrAudioSubsystem::ListenerPosition(this);
		const FRotator CameraRotation = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;
		const FRotationMatrix Yaw(FRotator(0, CameraRotation.Yaw, 0));
		Front = Yaw.GetUnitAxis(EAxis::X);
		Right = Yaw.GetUnitAxis(EAxis::Y);
	}
}

void ABrPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ClientRequestVoicePreference();
}

void ABrPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	SendVoicePreference();
}

void ABrPlayerController::SendVoicePreference()
{
	if (!IsLocalController()) return;
	if (const UBrGameInstance* GI = GetGameInstance<UBrGameInstance>())
		ServerSetVoiceIndex(GI->GetVoiceIndex());
}

void ABrPlayerController::ServerSetVoiceIndex_Implementation(int32 Index)
{
	if (ABrPlayerState* PS = GetPlayerState<ABrPlayerState>())
	{
		PS->SetVoiceIndex(Index);
#if !UE_BUILD_SHIPPING
		if (Index == 1 && PS->GetVoiceIndex() == 1 && FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")))
			++SoundSmokeVoiceRpcCount;
#endif
	}
}

void ABrPlayerController::ClientRequestVoicePreference_Implementation() { SendVoicePreference(); }

void ABrPlayerController::ClientReloadSound_Implementation(uint32 Id, const TSoftObjectPtr<USoundBase>& Sound, FVector_NetQuantize Location)
{
#if !UE_BUILD_SHIPPING
	if (SoundProbeStage >= 1 && SoundProbeStage <= 3 && FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")))
	{
		++SoundProbeReload;
		SoundProbeReloadId = Id;
		bSoundProbeBad |= Id == 0 || Sound != SoundProbeReloadSound || !FVector(Location).Equals(SoundProbeOrigin, 2);
		ServerSoundSmokeResult(SoundProbeStage, 1);
	}
#endif
	if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>()) Audio->PlayReload(Id, Sound, Location);
}

void ABrPlayerController::ClientStopReloadSound_Implementation(uint32 Id)
{
#if !UE_BUILD_SHIPPING
	if (SoundProbeStage >= 1 && SoundProbeStage <= 3 && FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")))
	{
		++SoundProbeStop;
		bSoundProbeBad |= Id != SoundProbeReloadId || SoundProbeReload != 1;
	}
#endif
	if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>()) Audio->StopLocalReload(Id);
}

void ABrPlayerController::ClientSounds_Implementation(const TArray<FBrSoundType>& Types, const TArray<FBrSoundEvent>& Events)
{
	if (!UBrAudioSubsystem::ListenerPawn(this)) return;
	UE_LOG(LogBr, Verbose, TEXT("Audio batch received: Types=%d Events=%d"), Types.Num(), Events.Num());
	for (const FBrSoundEvent& Event : Events)
	{
		if (!Types.IsValidIndex(Event.TypeIndex))
		{
			UE_LOG(LogBr, Error, TEXT("Audio batch contains invalid type index"));
			continue;
		}
		const FBrSoundType& Type = Types[Event.TypeIndex];
		ClientSound_Implementation(Type.Kind, Type.Sound, Event.Location);
	}
}

void ABrPlayerController::ClientSound_Implementation(EBrSoundKind Kind, const TSoftObjectPtr<USoundBase>& Sound,
	FVector_NetQuantize Location)
{
#if !UE_BUILD_SHIPPING
	TrackSoundSmokeEvent(Kind, Sound, Location);
	if (Kind == EBrSoundKind::ArmorBreak && FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")))
		++ArmorSmokeBreakSounds; // Real received sound route, including -nosound headless runs.
	if (FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) &&
		FVector::DistSquared2D(Location, AudioSmokeOrigin) < 100.f)
	{
		if (Kind == EBrSoundKind::Fire) ++AudioSmokeFire;
		if (Kind == EBrSoundKind::RunStep) ++AudioSmokeSteps;
		if (Kind == EBrSoundKind::Downed) ++AudioSmokeDown;
	}
#endif
	UE_LOG(LogBr, Verbose, TEXT("Audio event received: Kind=%d Location=%s"), static_cast<int32>(Kind), *Location.ToString());
	if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>()) Audio->Play(Kind, Sound, Location);
}

void ABrPlayerController::ClientAudioSmokeProbe_Implementation(int32 Stage, FVector_NetQuantize Origin)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	if (Stage == 0)
	{
		AudioSmokeOrigin = Origin;
		AudioSmokeFire = AudioSmokeSteps = AudioSmokeDown = AudioSmokeProneBaseline = 0;
		return;
	}
	bool bPass = true;
	if (Stage == 1 || Stage == 2) bPass = AudioSmokeFire == 1 && AudioSmokeSteps == 0;
	if (Stage == 3) { bPass = AudioSmokeSteps > 0; AudioSmokeProneBaseline = AudioSmokeSteps; }
	if (Stage == 4) bPass = AudioSmokeSteps == AudioSmokeProneBaseline;
	if (Stage == 5) bPass = AudioSmokeDown == 1;
	if (bPass)
	{
		UE_LOG(LogBr, Display, TEXT("E2E AUDIO PROBE PASS Stage=%d Fire=%d Steps=%d Down=%d"), Stage, AudioSmokeFire, AudioSmokeSteps, AudioSmokeDown);
	}
	else
	{
		UE_LOG(LogBr, Error, TEXT("E2E AUDIO PROBE FAIL Stage=%d Fire=%d Steps=%d Down=%d"), Stage, AudioSmokeFire, AudioSmokeSteps, AudioSmokeDown);
	}
#endif
}

void ABrPlayerController::Destroyed()
{
	// Super destroys/unpossesses the pawn *before* GameMode::Logout. Snapshot while it still exists.
	if (HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>()) Mode->RecordPlayer(this);
	}
	Super::Destroyed();
}

void ABrPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		SendVoicePreference();
		if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>()) GI->ApplyAudioVolumes();
		ClientEnterFlow_Implementation();
#if !UE_BUILD_SHIPPING
		EnableCheats();
#endif
		RefreshCursorMode();

		LoadedInputConfig = UBrDeveloperSettings::Get()->InputConfig.LoadSynchronous();
		UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
		if (!Subsystem || !LoadedInputConfig || !LoadedInputConfig->DefaultMappingContext)
		{
			UE_LOG(LogBr, Error, TEXT("Cannot install Br input mapping context: subsystem or input config is missing."));
			return;
		}

		InputSubsystem = Subsystem;
		if (UEnhancedInputUserSettings* UserSettings = Subsystem->GetUserSettings())
		{
			if (!UserSettings->IsMappingContextRegistered(LoadedInputConfig->DefaultMappingContext) &&
				!UserSettings->RegisterInputMappingContext(LoadedInputConfig->DefaultMappingContext))
			{
				UE_LOG(LogBr, Error, TEXT("Br input mapping context registration failed."));
			}
		}
		else
		{
			UE_LOG(LogBr, Error, TEXT("Enhanced Input user settings are unavailable; check bEnableUserSettings."));
		}
		Subsystem->AddMappingContext(LoadedInputConfig->DefaultMappingContext, 0);
	}
}

void ABrPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if !UE_BUILD_SHIPPING
	GetWorldTimerManager().ClearTimer(ArmorSmokeProbeTimer);
#endif
	CloseMenu();
	GetWorldTimerManager().ClearTimer(LeaveTimer);
	GetWorldTimerManager().ClearTimer(UnstuckTimer);
	GetWorldTimerManager().ClearTimer(FogProbeTimer);
	GetWorldTimerManager().ClearTimer(FlowUITimer);
	if (FlowWidget) FlowWidget->RemoveFromParent();
	FlowWidget = nullptr;
	SpectatorTarget = nullptr;
	FlowSpectatorPawn = nullptr;
	SpectatorArmor = {};
	AppliedSpectatorPawn.Reset();
	LastLoggedFlow.Empty();
	FlowInputPhase = -1;
	if (InputSubsystem.IsValid() && LoadedInputConfig && LoadedInputConfig->DefaultMappingContext)
	{
		InputSubsystem->RemoveMappingContext(LoadedInputConfig->DefaultMappingContext);
	}

	InputSubsystem.Reset();
	LoadedInputConfig = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ABrPlayerController::ClientFogProbe_Implementation(int32 Stage, bool bExpectedPresent)
{
#if !UE_BUILD_SHIPPING
	if (HasAuthority() || !FParse::Param(FCommandLine::Get(), TEXT("BrFogSmoke"))) return;
	GetWorldTimerManager().SetTimer(FogProbeTimer, FTimerDelegate::CreateWeakLambda(this, [this, Stage, bExpectedPresent]()
	{
		int32 HostActors = 0;
		Fog->ValidateSmokeMasks(Stage);
		for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
			if (const ABrPlayerState* PS = It->GetPlayerState<ABrPlayerState>())
				if (PS->bHost) ++HostActors;
		if ((HostActors > 0) != bExpectedPresent)
		{
			UE_LOG(LogBr, Error, TEXT("FOG PROBE FAIL Stage=%d HostCharacters=%d ExpectedPresent=%d"), Stage, HostActors, bExpectedPresent);
		}
		else
		{
			UE_LOG(LogBr, Display, TEXT("FOG PROBE PASS Stage=%d HostCharacters=%d ExpectedPresent=%d Authority=0"),
				Stage, HostActors, bExpectedPresent);
		}
		if (Stage == 3 && FApp::CanEverRender()) ConsoleCommand(TEXT("HighResShot 1"), false);
	}), 1.f, false);
#endif
}

void ABrPlayerController::ClientFogSpectatorProbe_Implementation(int32 Stage, int32 TargetId, int32 WithheldId, const FString& TargetGuid)
{
#if !UE_BUILD_SHIPPING
	if (HasAuthority() || !FParse::Param(FCommandLine::Get(), TEXT("BrFogSpectatorSmoke"))) return;
	GetWorldTimerManager().SetTimer(FogProbeTimer, FTimerDelegate::CreateWeakLambda(this, [this, Stage, TargetId, WithheldId, TargetGuid]()
	{
		ABrCharacter* Target = nullptr;
		int32 WithheldCount = 0;
		for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
			if (const ABrPlayerState* PS = It->GetPlayerState<ABrPlayerState>())
			{
				if (PS->GetPlayerId() == TargetId) Target = *It;
				if (PS->GetPlayerId() == WithheldId || (Stage == 0 && PS != PlayerState)) ++WithheldCount;
			}
		const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
		UNetDriver* Driver = GetWorld()->GetNetDriver();
		UActorChannel* Channel = Target ? Driver->ServerConnection->FindActorChannelRef(Target) : nullptr;
		const FString Guid = Target ? Driver->GetNetGuidCache()->GetNetGUID(Target).ToString() : TEXT("None");
		const bool bTargetValid = Stage == 0 || (IsFlowSpectating() && SpectatorTarget &&
			SpectatorTarget->GetPlayerId() == TargetId && FlowSpectatorPawn == Target && Target &&
			SpectatorTarget->GetPawn() == Target && GetViewTarget() == Target && Guid == TargetGuid && Channel && !Channel->Closing);
		if (!GS || GS->bResults || !UBrVisibilitySubsystem::Active(GetWorld()) || !bTargetValid || WithheldCount)
		{
			UE_LOG(LogBr, Error, TEXT("FOG SPECTATOR FAIL Stage=%d NetGUID=%s ExpectedGUID=%s Channel=%d Cache=%s Direct=%s Withheld=%d Results=%d"),
				Stage, *Guid, *TargetGuid, Channel ? Channel->ChIndex : -1,
				*GetNameSafe(SpectatorTarget ? SpectatorTarget->GetPawn() : nullptr), *GetNameSafe(FlowSpectatorPawn),
				WithheldCount, GS && GS->bResults);
			return;
		}
		UE_LOG(LogBr, Display, TEXT("FOG SPECTATOR PASS Stage=%d TargetId=%d NetGUID=%s Channel=%d Withheld=0 Results=0"),
			Stage, TargetId, *Guid, Channel ? Channel->ChIndex : -1);
		// Exercise the real owning-client request, including server validation, not a fixture assignment.
		if (Stage >= 3 && Stage < 8) ServerCycleSpectator();
	}), 1.f, false);
#endif
}

void ABrPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// Controller bindings also work without a possessed pawn (spectating).
	UBrInputConfig* Config = UBrDeveloperSettings::Get()->InputConfig.LoadSynchronous();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (Config && Config->MapAction && Config->MenuAction)
		{
			Input->BindAction(Config->MapAction, ETriggerEvent::Started, this, &ThisClass::ToggleMap);
			Input->BindAction(Config->MenuAction, ETriggerEvent::Started, this, &ThisClass::ToggleMenu);
		}
		else UE_LOG(LogBr, Error, TEXT("Map input assets missing; run create_input_assets.py."));
	}
	else UE_LOG(LogBr, Error, TEXT("Map requires EnhancedInputComponent."));
	// Fixed dispatcher plus configurable Menu key; ToggleMenu coalesces the frame.
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ThisClass::ToggleMenu).bConsumeInput = false;
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ABrPlayerController::CycleSpectator).bConsumeInput = false;
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ABrPlayerController::CycleSpectator).bConsumeInput = false;
}
void ABrPlayerController::ToggleMap()
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (IsLocalController() && !IsMenuOpen() && GS && !GS->bLobby && !GS->bResults)
	{
		if (bMapOpen) { CloseMap(); return; }
		bMapOpen = true;
		if (ABrCharacter* BrPawn = Cast<ABrCharacter>(GetPawn()))
		{
			BrPawn->GetInventory()->StopFire();
			BrPawn->GetLife()->StopInteract();
			BrPawn->GetLife()->StopRespawnHold();
		}
		RefreshCursorMode();
	}
}
void ABrPlayerController::CloseMap()
{
	if (bMapOpen) MapClosedAt = GetWorld()->GetTimeSeconds();
	bMapOpen = false;
	RefreshCursorMode();
}
void ABrPlayerController::ClientEnterFlow_Implementation()
{
	if (!IsLocalController()) return;
	CloseMenu();
	bLeaving = false;
	bLeaveFailed = false;
	MenuDispatchFrame = MAX_uint64;
	GetWorldTimerManager().ClearTimer(LeaveTimer);
	CloseMap();
	Fog->StartView(); // PlayerController survives seamless travel; world timers/RTs do not.
	if (FlowWidget) FlowWidget->RemoveFromParent();
	FlowWidget = nullptr;
	AppliedSpectatorPawn.Reset();
	LastLoggedFlow.Empty();
	FlowInputPhase = -1;
	GetWorldTimerManager().SetTimer(FlowUITimer, this, &ABrPlayerController::RefreshFlow, 0.25f, true);
}
void ABrPlayerController::RefreshFlow()
{
	if (!IsLocalController() || GetWorld()->bIsTearingDown) return;
	if (!FlowWidget)
	{
		FlowWidget = CreateWidget<UBrFlowWidget>(this);
		if (!FlowWidget) { UE_LOG(LogBr, Error, TEXT("Could not create flow UI")); return; }
		if (!FlowWidget->IsInViewport()) FlowWidget->AddToViewport();
	}
	FlowWidget->Refresh();
	if (MenuWidget) MenuWidget->RefreshStatus();
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	const int32 Phase = !GS ? 0 : GS->bLobby ? 1 : GS->bResults ? 3 : 2;
	if (Phase != FlowInputPhase)
	{
		FlowInputPhase = Phase;
		CloseMenu();
		if (Phase != 2) CloseMap();
		RefreshCursorMode();
	}
	const FString State = FString::Printf(TEXT("%s Phase=%d Pawn=%s"), *GetWorld()->GetMapName(), Phase, *GetNameSafe(GetPawn()));
	if (State != LastLoggedFlow)
	{
		LastLoggedFlow = State;
		UE_LOG(LogBr, Display, TEXT("Flow client state: %s"), *State);
	}
#if !UE_BUILD_SHIPPING
	if (Phase == 0 && GetWorld()->GetMapName().Contains(TEXT("L_MainMenu")) &&
		GetWorld()->GetTimeSeconds() > 2 && FParse::Param(FCommandLine::Get(), TEXT("BrMenuSmoke")))
		if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>())
			if (GI->bMenuSmokeRejoin)
			{
				GI->bMenuSmokeRejoin = false;
				GI->PlayerName = TEXT("MenuClient1"); // Fixture only; never overwrite a saved user profile.
				int32 Port = 18281;
				FParse::Value(FCommandLine::Get(), TEXT("BrMenuSmokePort="), Port);
				GI->Join(FString::Printf(TEXT("127.0.0.1:%d"), Port));
				return;
			}
	if (!HasAuthority() && !bSmokeDisconnectIssued && FParse::Param(FCommandLine::Get(), TEXT("BrFlowDisconnectSmoke")))
		if (const ABrCharacter* BrPawn = Cast<ABrCharacter>(GetPawn()))
			if (BrPawn->GetLife()->State == EBrLifeState::Downed)
				if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>())
				{
					bSmokeDisconnectIssued = true;
					UE_LOG(LogBr, Display, TEXT("Flow smoke: client leaving while downed"));
					GI->Leave(TEXT("Smoke: disconnected while downed."));
					return;
				}
#endif
	if (IsFlowSpectating())
	{
		OnRep_SpectatorTarget(); // Pawn reference can become available after the target PS replicates.
	}
}
bool ABrPlayerController::IsFlowSpectating() const
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	const ABrPlayerState* PS = GetPlayerState<ABrPlayerState>();
	return GS && !GS->bLobby && PS && (PS->bEliminated || (GS->bResults && !GetPawn()));
}
void ABrPlayerController::CycleSpectator()
{
	if (IsLocalController() && !IsGameplayInputBlocked()) ServerCycleSpectator();
}
void ABrPlayerController::ServerCycleSpectator_Implementation()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastSpectatorRequest < 0.2f || !IsFlowSpectating()) return;
	LastSpectatorRequest = Now;
	SelectSpectator(true);
}
void ABrPlayerController::ResetFlowRequests()
{
	CancelUnstuck(TEXT(""));
	UnstuckMessage.Empty();
	UnstuckCooldownEnd = 0;
	if (const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
		UnstuckCooldownEnd = Mode->GetUnstuckCooldown(this);
	LastTeamRequest = LastSpectatorRequest = -1;
	if (HasAuthority())
	{
		SpectatorTarget = nullptr;
		FlowSpectatorPawn = nullptr;
		AppliedSpectatorPawn.Reset();
		RefreshSpectatorArmor();
		ForceNetUpdate();
	}
}
void ABrPlayerController::ServiceSpectator()
{
	if (!HasAuthority()) return;
	if (IsFlowSpectating()) SelectSpectator(false);
	else if (SpectatorTarget)
	{
		SpectatorTarget = nullptr;
		FlowSpectatorPawn = nullptr;
		SetViewTarget(GetPawn() ? static_cast<AActor*>(GetPawn()) : this);
		ForceNetUpdate();
	}
	RefreshSpectatorArmor();
}
void ABrPlayerController::RefreshSpectatorArmor()
{
	if (!HasAuthority()) return;
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	ABrCharacter* Target = Mode && Mode->IsAdmitted(this) && IsFlowSpectating() &&
		SpectatorTarget && FlowSpectatorPawn && SpectatorTarget->GetPawn() == FlowSpectatorPawn &&
		!SpectatorTarget->bEliminated && Mode->IsAdmitted(Cast<AController>(SpectatorTarget->GetOwner())) &&
		FlowSpectatorPawn->GetLife()->IsAlive() ? FlowSpectatorPawn.Get() : nullptr;
	const TArray<float> Values = Target ? Target->GetArmor()->GetPlates() : TArray<float>();
	if (SpectatorArmor.Pawn != Target || SpectatorArmor.Plates != Values)
	{
		SpectatorArmor.Pawn = Target;
		SpectatorArmor.Plates = Values;
		ForceNetUpdate();
	}
}
const TArray<float>* ABrPlayerController::GetSpectatorArmor(const ABrCharacter* ViewedPawn) const
{
	return ViewedPawn && IsFlowSpectating() && GetViewTarget() == ViewedPawn &&
		FlowSpectatorPawn == ViewedPawn && SpectatorArmor.Pawn == ViewedPawn ? &SpectatorArmor.Plates : nullptr;
}
void ABrPlayerController::SelectSpectator(bool bCycle)
{
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	if (!HasAuthority() || !Mode || !Mode->IsAdmitted(this) || !HasClientLoadedCurrentWorld() || !IsFlowSpectating()) return;
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	const ABrPlayerState* Self = GetPlayerState<ABrPlayerState>();
	TArray<ABrPlayerState*> Candidates;
	for (APlayerState* Entry : GS->PlayerArray)
	{
		ABrPlayerState* PS = Cast<ABrPlayerState>(Entry);
		ABrCharacter* Candidate = PS ? Cast<ABrCharacter>(PS->GetPawn()) : nullptr;
		if (Candidate && Candidate->GetLife()->IsAlive() && Mode->IsAdmitted(Cast<AController>(PS->GetOwner())))
			Candidates.Add(PS);
	}
	Candidates.Sort([Self](const ABrPlayerState& A, const ABrPlayerState& B)
	{
		const bool bA = Self->Team >= 0 && A.Team == Self->Team;
		const bool bB = Self->Team >= 0 && B.Team == Self->Team;
		return bA != bB ? bA : A.GetPlayerId() < B.GetPlayerId();
	});
	const int32 Current = Candidates.IndexOfByKey(SpectatorTarget.Get());
	ABrPlayerState* Selected = Candidates.IsEmpty() ? nullptr :
		Candidates[bCycle || Current == INDEX_NONE ? (Current + 1) % Candidates.Num() : Current];
	if (SpectatorTarget != Selected)
	{
		SpectatorTarget = Selected;
		ForceNetUpdate();
		UE_LOG(LogBr, Display, TEXT("Flow spectating (server): %s -> %s"), *GetName(), *GetNameSafe(Selected));
	}
	ABrCharacter* SelectedPawn = Selected ? Cast<ABrCharacter>(Selected->GetPawn()) : nullptr;
	if (FlowSpectatorPawn != SelectedPawn)
	{
		FlowSpectatorPawn = SelectedPawn;
		ForceNetUpdate();
		if (SelectedPawn) SelectedPawn->ForceNetUpdate();
	}
	if (SelectedPawn)
		if (UBrVisibilitySubsystem* Visibility = GetWorld()->GetSubsystem<UBrVisibilitySubsystem>())
			Visibility->RequestDiscovery(SelectedPawn, this);
#if !UE_BUILD_SHIPPING
	if (SelectedPawn && FParse::Param(FCommandLine::Get(), TEXT("BrFogSpectatorSmoke")) &&
		GetWorld()->GetTimeSeconds() >= NextSpectatorRouteLog)
	{
		UNetDriver* Driver = GetWorld()->GetNetDriver();
		UNetConnection* Connection = GetNetConnection();
		UActorChannel* Channel = Connection ? Connection->FindActorChannelRef(SelectedPawn) : nullptr;
		const TSharedPtr<FNetworkObjectInfo> Info = Driver->GetNetworkObjectList().Find(SelectedPawn);
		UE_LOG(LogBr, Display, TEXT("FOG SPECTATOR channel: Viewer=%s Target=%s NetGUID=%s Channel=%d Closing=%d Initialized=%d ClientWorld=%s World=%s Relevant=%d"),
			*GetName(), *SelectedPawn->GetName(), *Driver->GetNetGuidCache()->GetNetGUID(SelectedPawn).ToString(),
			Channel ? Channel->ChIndex : -1, Channel && Channel->Closing,
			Connection && Driver->IsLevelInitializedForActor(SelectedPawn, Connection),
			Connection ? *Connection->GetClientWorldPackageName().ToString() : TEXT("None"),
			*GetWorld()->GetPackage()->GetName(), SelectedPawn->IsNetRelevantFor(this, SelectedPawn, SelectedPawn->GetActorLocation()));
		UE_LOG(LogBr, Display, TEXT("FOG SPECTATOR registry: NetGUID=%s Registered=%d Active=%d Dormancy=%d DormantConnection=%d Role=%d RemoteRole=%d OnlyOwner=%d Temporary=%d NextUpdate=%.3f Platform=%.3f"),
			*Driver->GetNetGuidCache()->GetNetGUID(SelectedPawn).ToString(), Info.IsValid(),
			Driver->GetNetworkObjectList().GetActiveObjects().Contains(SelectedPawn), int32(SelectedPawn->NetDormancy),
			Info && Connection && Info->DormantConnections.Contains(Connection), int32(SelectedPawn->GetLocalRole()),
			int32(SelectedPawn->GetRemoteRole()), SelectedPawn->bOnlyRelevantToOwner, SelectedPawn->bNetTemporary,
			Info ? Info->NextUpdateTime : -1, FPlatformTime::Seconds());
		NextSpectatorRouteLog = GetWorld()->GetTimeSeconds() + 2;
	}
#endif
	AActor* TargetActor = SelectedPawn ? static_cast<AActor*>(SelectedPawn) : this;
	if (GetViewTarget() != TargetActor) SetViewTarget(TargetActor);
	RefreshSpectatorArmor();
	if (IsLocalController()) OnRep_SpectatorTarget();
}
void ABrPlayerController::OnRep_SpectatorTarget()
{
	if (!IsLocalController() || !IsFlowSpectating()) return;
	if (!SpectatorTarget)
	{
		AppliedSpectatorPawn.Reset();
		SetViewTarget(this);
		return;
	}
	ABrCharacter* Target = FlowSpectatorPawn;
	if (!Target)
	{
		// Target references can precede their fog channel. Keep retrying presentation
		// only; never let a client choose a replacement/omniscient view.
		if (GetWorld()->GetTimeSeconds() >= NextSpectatorLog)
		{
			UE_LOG(LogBr, Verbose, TEXT("Fog spectator pending pawn channel: Target=%s Player=%s"),
				*GetNameSafe(SpectatorTarget), *SpectatorTarget->GetPlayerName());
			NextSpectatorLog = GetWorld()->GetTimeSeconds() + 2;
		}
		return;
	}
	Target->SetSpectatorCameraActive();
	if (AppliedSpectatorPawn.Get() != Target || GetViewTarget() != Target)
	{
		AppliedSpectatorPawn = Target;
		SetViewTarget(Target);
		UE_LOG(LogBr, Display, TEXT("Flow spectating: %s -> %s"), *GetName(), *Target->GetName());
	}
}
void ABrPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ABrPlayerController, SpectatorTarget, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABrPlayerController, FlowSpectatorPawn, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABrPlayerController, SpectatorArmor, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABrPlayerController, UnstuckStart, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABrPlayerController, UnstuckEnd, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABrPlayerController, UnstuckCooldownEnd, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ABrPlayerController, UnstuckMessage, COND_OwnerOnly);
}
void ABrPlayerController::GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const
{
	if (HasAuthority() && IsFlowSpectating())
	{
		const APawn* Followed = SpectatorTarget ? SpectatorTarget->GetPawn() : nullptr;
		Location = Followed ? Followed->GetActorLocation() : GetSpawnLocation();
		Rotation = Followed ? Followed->GetActorRotation() : FRotator::ZeroRotator;
		return; // Do not trust ServerSetSpectatorLocation or a stale server camera cache for relevancy.
	}
	Super::GetPlayerViewPoint(Location, Rotation);
}
void ABrPlayerController::ServerSettings_Implementation(FBrMatchSettings Settings)
{
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>()) Mode->ChangeSettings(this, Settings);
}
void ABrPlayerController::ServerChooseTeam_Implementation(int32 Team, int32 Color)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastTeamRequest < 0.2f) return;
	LastTeamRequest = Now;
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>()) Mode->ChooseTeam(this, Team, Color);
}
void ABrPlayerController::ServerStartRound_Implementation()
{
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>()) Mode->StartRound(this);
}
void ABrPlayerController::BrStartMatch()
{
#if !UE_BUILD_SHIPPING
	ServerStartRound();
#endif
}
void ABrPlayerController::BrEliminatePlayer(int32 Index) { ServerEliminatePlayer(Index); }
void ABrPlayerController::ServerEliminatePlayer_Implementation(int32 Index)
{
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>()) Mode->EliminatePlayer(this, Index);
}
void ABrPlayerController::ClientPickupFeedback_Implementation(const FString& Feedback)
{
	PickupFeedback = Feedback;
	PickupFeedbackUntil = GetWorld()->GetTimeSeconds() + 2.5f;
}

void ABrPlayerController::ServerBrCheat_Implementation(FName Command, FName WeaponName, float Amount)
{
#if !UE_BUILD_SHIPPING
	// Local on the receiving server means the listen host, not an owning remote client.
	// No client request or existence of a CheatManager grants permission.
	if (!FBrDevCheatPolicy::CanExecute(HasAuthority(), IsLocalController(), GetNetMode()))
	{
		UE_LOG(LogBr, Warning, TEXT("Rejected Br cheat: controller %s is not the local server host."), *GetName());
		return;
	}
	if (Command == TEXT("FogToggle"))
	{
		if (ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>())
		{
			GS->bFogDisabled = !GS->bFogDisabled;
			GS->ForceNetUpdate();
			UE_LOG(LogBr, Warning, TEXT("DEV BrFogToggle: server filtering AND local fog %s (trusted host only)"),
				GS->bFogDisabled ? TEXT("DISABLED") : TEXT("ENABLED"));
		}
		return;
	}
	if (Command == TEXT("Teleport"))
	{
		double X = 0;
		if (!LexTryParseString(X, *WeaponName.ToString()) || !FMath::IsFinite(X) ||
			!FMath::IsFinite(Amount) || FMath::Abs(X) > 1000000 || FMath::Abs(Amount) > 1000000 || !GetPawn())
		{
			UE_LOG(LogBr, Warning, TEXT("BrTeleport requires finite XY within +/-1000000 and a pawn."));
			return;
		}
		const FVector Destination(X, Amount, GetPawn()->GetActorLocation().Z);
		const bool bMoved = GetPawn()->TeleportTo(Destination, GetPawn()->GetActorRotation());
		UE_LOG(LogBr, Display, TEXT("BrTeleport: %s Success=%d"), *Destination.ToString(), bMoved);
		return;
	}
	if (Command == TEXT("ZoneNext") || Command == TEXT("ZoneScale") || Command == TEXT("ZonePause"))
	{
		ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
		ABrSafeZone* Zone = Mode ? Mode->GetSafeZone() : nullptr;
		if (!IsValid(Zone)) { UE_LOG(LogBr, Warning, TEXT("Zone cheat requires an active match zone.")); return; }
		if (Command == TEXT("ZoneScale"))
		{
			if (!FMath::IsFinite(Amount) || Amount < 0.01f || Amount > 1000)
			{ UE_LOG(LogBr, Warning, TEXT("Zone time scale must be finite in [0.01,1000].")); return; }
			Zone->SetTimeScale(Amount);
		}
		else if (Command == TEXT("ZoneNext")) Zone->NextStage();
		else Zone->TogglePause();
		return;
	}
	ABrCharacter* BrPawn = Cast<ABrCharacter>(GetPawn());
	if (!BrPawn || !FMath::IsFinite(Amount) || Amount < 0 || Amount > 1000000)
	{
		UE_LOG(LogBr, Warning, TEXT("Rejected Br cheat: invalid pawn/amount."));
		return;
	}
	UBrInventoryComponent* Inventory = BrPawn->GetInventory();
	if (Command == TEXT("SpawnPickup"))
	{
		const TSoftObjectPtr<UBrPickupDefinition>* Asset = UBrDeveloperSettings::Get()->PickupRegistry.Find(WeaponName);
		UBrPickupDefinition* Definition = Asset ? Asset->LoadSynchronous() : nullptr;
		if (!Definition)
		{
			UE_LOG(LogBr, Error, TEXT("BrSpawnPickup: unknown/missing registered pickup '%s'."), *WeaponName.ToString());
			return;
		}
		const FVector Ahead = BrPawn->GetActorLocation() + BrPawn->GetActorForwardVector() * 200;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(BrCheatPickupFloor), false, BrPawn);
		FHitResult Floor;
		if (!GetWorld()->LineTraceSingleByChannel(Floor, Ahead + FVector(0, 0, 300),
			Ahead - FVector(0, 0, 1000), ECC_Visibility, Query))
		{
			UE_LOG(LogBr, Error, TEXT("BrSpawnPickup: no floor in front of player."));
			return;
		}
		if (ABrPickup::Spawn(GetWorld(), FTransform(Floor.ImpactPoint + FVector(0, 0, 35)), Definition, FBrPickupPayload()))
			UE_LOG(LogBr, Display, TEXT("BrSpawnPickup: %s"), *WeaponName.ToString());
		return;
	}
	if (Command == TEXT("RespawnPickups"))
	{
		ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
		ABrPickupManager* Manager = Mode ? Mode->GetPickupManager() : nullptr;
		if (IsValid(Manager)) Manager->RespawnAllPickups();
		else UE_LOG(LogBr, Error, TEXT("BrRespawnAllPickups: pickup manager is missing."));
		return;
	}
	if (Command == TEXT("DownAll") || Command == TEXT("DownPlayer"))
	{
		const AGameStateBase* GS = GetWorld()->GetGameState();
		if (!GS) { UE_LOG(LogBr, Error, TEXT("BrDownPlayer: GameState missing.")); return; }
		int32 Index = INDEX_NONE;
		const FString Token = WeaponName.ToString();
		const bool bIndex = LexTryParseString(Index, *Token);
		bool bFound = false;
		for (int32 PlayerIndex = 0; PlayerIndex < GS->PlayerArray.Num(); ++PlayerIndex)
		{
			APlayerState* PS = GS->PlayerArray[PlayerIndex];
			if (!PS) continue;
			if (Command == TEXT("DownAll") || (bIndex ? Index == PlayerIndex : PS->GetPlayerName() == Token))
			{
				if (ABrCharacter* Target = Cast<ABrCharacter>(PS->GetPawn()))
				{
					Target->GetLife()->Down();
					bFound = true;
				}
			}
		}
		if (!bFound) UE_LOG(LogBr, Warning, TEXT("BrDownPlayer: no pawn for '%s'."), *Token);
		return;
	}
	if (Command == TEXT("Weapon"))
	{
		const TSoftObjectPtr<UBrWeaponDefinition>* Definition = UBrDeveloperSettings::Get()->WeaponRegistry.Find(WeaponName);
		if (!Definition || !Inventory->GiveWeapon(Definition->LoadSynchronous()))
		{
			UE_LOG(LogBr, Error, TEXT("BrGiveWeapon failed: %s (use Pistol, AssaultRifle or MachineGun)"), *WeaponName.ToString());
		}
	}
	else if (Command == TEXT("Grenades")) Inventory->GiveGrenades(FMath::FloorToInt(Amount));
	else if (Command == TEXT("Ammo")) Inventory->GiveAmmo(FMath::FloorToInt(Amount));
	else if (Command == TEXT("Health"))
	{
		if (!BrPawn->GetLife()->IsAlive())
		{
			UE_LOG(LogBr, Warning, TEXT("BrSetHealth requires Alive; revive or respawn instead."));
			return;
		}
		BrPawn->GetHealth()->SetHealth(Amount);
	}
	else if (Command == TEXT("Damage")) UGameplayStatics::ApplyDamage(BrPawn, Amount, this, BrPawn, UDamageType::StaticClass());
	else if (Command == TEXT("Down")) BrPawn->GetLife()->Down();
	else if (Command == TEXT("Deaths"))
	{
		if (ABrPlayerState* PS = BrPawn->GetPlayerState<ABrPlayerState>())
		{
			PS->DeathCount = FMath::FloorToInt(Amount);
			PS->ForceNetUpdate();
		}
	}
	else
	{
		UE_LOG(LogBr, Warning, TEXT("Rejected unknown Br cheat: %s"), *Command.ToString());
		return;
	}
	UE_LOG(LogBr, Display, TEXT("BrCheat: %s Player=%s Amount=%.1f Health=%.1f Loaded=%d Reserve=%d Grenades=%d"),
		*Command.ToString(), *BrPawn->GetName(), Amount, BrPawn->GetHealth()->Health,
		Inventory->Loaded, Inventory->GetReserve(), Inventory->Grenades);
#endif
}
