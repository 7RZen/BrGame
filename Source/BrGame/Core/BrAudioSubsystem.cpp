#include "BrAudioSubsystem.h"
#include "BrDeveloperSettings.h"
#include "BrPlayerController.h"
#include "BrCharacter.h"
#include "BrGameInstance.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "BrGameMode.h"
#include "Engine/NetConnection.h"
#include "BrGameState.h"
#include "BrPlayerState.h"
#include "BrLifeComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "AudioDevice.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "BrCombatDefinitions.h"
#include "BrPickupDefinition.h"

bool UBrAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UBrAudioSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (UBrGameInstance* GI = World.GetGameInstance<UBrGameInstance>()) GI->ApplyAudioVolumes();
	if (World.GetNetMode() != NM_Client)
		World.GetTimerManager().SetTimer(BatchTimer, this, &UBrAudioSubsystem::Flush,
			FMath::Clamp(UBrDeveloperSettings::Get()->AudioBatchInterval, 0.05f, 0.25f), true);
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("BrAudioWindowSmoke")))
		World.GetTimerManager().SetTimer(WindowSmokeTimer, this, &UBrAudioSubsystem::ServiceWindowSmoke, 0.5f, true, 2.f);
#endif
}

void UBrAudioSubsystem::Deinitialize()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(BatchTimer);
	Pending.Empty();
	Concurrencies.Empty();
#if !UE_BUILD_SHIPPING
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(WindowSmokeTimer);
#endif
	Sounds.Empty();
	Attenuations.Empty();
	FailedSounds.Empty();
	Super::Deinitialize();
}

#if !UE_BUILD_SHIPPING
void UBrAudioSubsystem::ServiceWindowSmoke()
{
	ABrPlayerController* PC = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC || !PC->GetPawn()) return;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	FVector Location, Front, Right;
	PC->GetAudioListenerPosition(Location, Front, Right);
	if (WindowSmokeStage == 0)
	{
		FRotator CameraRotation;
		CameraRotation = PC->PlayerCameraManager->GetCameraRotation();
		const FRotationMatrix Yaw(FRotator(0, CameraRotation.Yaw, 0));
		if (!GetWorld()->GetAudioDevice().IsValid() || !Location.Equals(ListenerPosition(PC), 1) ||
			!Front.Equals(Yaw.GetUnitAxis(EAxis::X), 0.001f) ||
			!Right.Equals(Yaw.GetUnitAxis(EAxis::Y), 0.001f))
		{
			UE_LOG(LogBr, Error, TEXT("AUDIO WINDOW SMOKE FAIL: device/listener"));
			PC->ConsoleCommand(TEXT("quit"), false);
			return;
		}
		UE_LOG(LogBr, Display, TEXT("AUDIO LISTENER PASS Pitch=%.0f Location=%s Front=%s Right=%s Device=1"),
			S->CameraPitchAngle, *Location.ToString(), *Front.ToString(), *Right.ToString());
	}
	EBrSoundKind Kind = EBrSoundKind::Pickup;
	TSoftObjectPtr<USoundBase> Sound;
	if (WindowSmokeStage < 3)
	{
		const FName Keys[] = {TEXT("Pistol"), TEXT("AssaultRifle"), TEXT("MachineGun")};
		const auto* Entry = S->WeaponRegistry.Find(Keys[WindowSmokeStage]);
		UBrWeaponDefinition* Weapon = Entry ? Entry->LoadSynchronous() : nullptr;
		Sound = Weapon ? Weapon->FireSound : TSoftObjectPtr<USoundBase>();
		Kind = EBrSoundKind::Fire;
	}
	else if (WindowSmokeStage == 3)
	{
		UBrThrowableDefinition* Throwable = S->HandGrenade.LoadSynchronous();
		Sound = Throwable ? Throwable->ExplosionSound : TSoftObjectPtr<USoundBase>();
		Kind = EBrSoundKind::Explosion;
	}
	else if (WindowSmokeStage == 4) { Sound = S->DownedScream; Kind = EBrSoundKind::Downed; }
	else if (WindowSmokeStage < 8)
	{
		Kind = WindowSmokeStage == 5 ? EBrSoundKind::RunStep :
			WindowSmokeStage == 6 ? EBrSoundKind::SprintStep : EBrSoundKind::CrouchStep;
		Sound = StepSound(Kind);
	}
	else if (WindowSmokeStage < 12)
	{
		const FName Keys[] = {TEXT("AssaultRifle"), TEXT("Ammo"), TEXT("Health100"), TEXT("Grenade")};
		const auto* Entry = S->PickupRegistry.Find(Keys[WindowSmokeStage - 8]);
		UBrPickupDefinition* Pickup = Entry ? Entry->LoadSynchronous() : nullptr;
		Sound = Pickup ? Pickup->PickupSound : TSoftObjectPtr<USoundBase>();
	}
	else
	{
		UE_LOG(LogBr, Display, TEXT("AUDIO WINDOW SMOKE PASS: submitted 12 category sounds with a live device"));
		GetWorld()->GetTimerManager().ClearTimer(WindowSmokeTimer);
		PC->ConsoleCommand(TEXT("quit"), false);
		return;
	}
	Play(Kind, Sound, Location + Right * 100);
	++WindowSmokeStage;
}
#endif

float UBrAudioSubsystem::Range(EBrSoundKind Kind) const
{
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	switch (Kind)
	{
	case EBrSoundKind::Fire: return S->GunshotHearingRange;
	case EBrSoundKind::Explosion: return S->ExplosionHearingRange;
	case EBrSoundKind::Downed: return S->DownedHearingRange;
	case EBrSoundKind::RunStep: return S->RunStepHearingRange;
	case EBrSoundKind::SprintStep: return S->SprintStepHearingRange;
	case EBrSoundKind::CrouchStep: return S->CrouchStepHearingRange;
	default: return 0;
	}
}

FVector UBrAudioSubsystem::ListenerPosition(const ABrPlayerController* PC)
{
	const ABrCharacter* Target = ListenerPawn(PC);
	return Target ? Target->GetActorLocation() : FVector::ZeroVector;
}

ABrCharacter* UBrAudioSubsystem::ListenerPawn(const ABrPlayerController* PC)
{
	if (!IsValid(PC) || !PC->HasActorBegunPlay()) return nullptr;
	const UWorld* World = PC->GetWorld();
	const ABrGameState* GS = World ? World->GetGameState<ABrGameState>() : nullptr;
	const ABrGameMode* Mode = World ? World->GetAuthGameMode<ABrGameMode>() : nullptr;
	const ABrPlayerState* PS = PC->GetPlayerState<ABrPlayerState>();
	UNetConnection* Connection = PC->GetNetConnection();
	const bool bLoaded = PC->IsLocalController() || (Connection && !Connection->IsClosingOrClosed() &&
		World && Connection->GetClientWorldPackageName() == World->GetOutermost()->GetFName());
	const bool bReady = World && !World->bIsTearingDown && World->HasBegunPlay() && GS && !GS->bLobby && !GS->bResults &&
		(!PC->HasAuthority() || bLoaded);
	const bool bAdmitted = PS && (!PC->HasAuthority() || (Mode && Mode->IsAdmitted(PC)));
	auto ValidPawn = [World](const ABrCharacter* Pawn)
	{
		return IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Pawn->GetWorld() == World &&
			Pawn->HasActorBegunPlay() && Pawn->GetLife() && Pawn->GetLife()->State != EBrLifeState::Eliminated;
	};
	const bool bSpectating = PC->IsFlowSpectating();
	ABrCharacter* Pawn = Cast<ABrCharacter>(PC->GetPawn());
	ABrCharacter* Target = PC->GetFlowSpectatorPawn();
	const ABrPlayerState* Selected = PC->GetSpectatorTarget();
	const bool bSelected = Selected && ValidPawn(Target) &&
		Target->GetPlayerState<ABrPlayerState>() == Selected && !Selected->bEliminated &&
		(!Mode || Mode->IsAdmitted(Cast<AController>(Selected->GetOwner())));
	if (!FBrAudioRules::ListenerEligible(bReady, bAdmitted,
		ValidPawn(Pawn) && PS && !PS->bEliminated, bSpectating, bSelected)) return nullptr;
	return bSpectating ? Target : Pawn;
}

TSoftObjectPtr<USoundBase> UBrAudioSubsystem::StepSound(EBrSoundKind Kind) const
{
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	// Sprint deliberately reuses generated run placeholders, with its own range/volume.
	const auto& Steps = Kind == EBrSoundKind::RunStep || Kind == EBrSoundKind::SprintStep ? S->RunFootsteps : S->SoftFootsteps;
	return Steps.IsEmpty() ? TSoftObjectPtr<USoundBase>() : Steps[FMath::RandHelper(Steps.Num())];
}

int32 UBrAudioSubsystem::Emit(AActor* Source, EBrSoundKind Kind, TSoftObjectPtr<USoundBase> Sound,
	const FVector& Location, const AController* SourceController)
{
	if (!Source || !Source->HasAuthority() || GetWorld()->GetNetMode() == NM_Client) return 0;
	int32 Sent = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ABrPlayerController* PC = Cast<ABrPlayerController>(It->Get());
		ABrCharacter* Pawn = ListenerPawn(PC);
		const bool bOwn = PC && (Source == PC->GetPawn() || SourceController == PC || Source->GetInstigatorController() == PC);
		if (!FBrAudioRules::SendOther(Pawn != nullptr,
			Pawn && FBrAudioRules::InRange(Location, Pawn->GetActorLocation(), Range(Kind)), bOwn)) continue;
		FPendingListener* Queue = Pending.FindByPredicate([PC](const FPendingListener& Entry) { return Entry.Controller == PC; });
		if (!Queue)
		{
			Queue = &Pending.AddDefaulted_GetRef();
			Queue->Controller = PC;
		}
		if (Queue->Pawn != Pawn)
		{
			Queue->Pawn = Pawn;
			Queue->Types.Reset();
			Queue->Events.Reset();
		}
		int32 TypeIndex = Queue->Types.IndexOfByPredicate([Kind, Sound](const FBrSoundType& Type)
			{ return Type.Kind == Kind && Type.Sound == Sound; });
		// A batch-local byte index can represent at most 256 distinct sound types.
		if (TypeIndex == INDEX_NONE && Queue->Types.Num() == 256)
		{
			for (int32 Start = 0; Start < Queue->Events.Num(); Start += EventsPerBatch)
			{
				TArray<FBrSoundEvent> Batch;
				Batch.Append(Queue->Events.GetData() + Start, FMath::Min(EventsPerBatch, Queue->Events.Num() - Start));
				PC->ClientSounds(Queue->Types, Batch);
			}
			Queue->Types.Reset();
			Queue->Events.Reset();
		}
		if (TypeIndex == INDEX_NONE)
		{
			TypeIndex = Queue->Types.Num();
			FBrSoundType& Type = Queue->Types.AddDefaulted_GetRef();
			Type.Kind = Kind;
			Type.Sound = Sound;
		}
		FBrSoundEvent& Event = Queue->Events.AddDefaulted_GetRef();
		Event.TypeIndex = static_cast<uint8>(TypeIndex);
		Event.Location = Location;
		++Sent;
	}
	return Sent;
}

void UBrAudioSubsystem::Flush()
{
	for (FPendingListener& Queue : Pending)
	{
		ABrPlayerController* PC = Queue.Controller.Get();
		if (!PC || !Queue.Pawn.IsValid() || ListenerPawn(PC) != Queue.Pawn.Get()) continue;
		for (int32 Start = 0; Start < Queue.Events.Num(); Start += EventsPerBatch)
		{
			TArray<FBrSoundEvent> Batch;
			const int32 Count = FMath::Min(EventsPerBatch, Queue.Events.Num() - Start);
			Batch.Append(Queue.Events.GetData() + Start, Count);
			PC->ClientSounds(Queue.Types, Batch);
		}
	}
	Pending.Reset();
}

void UBrAudioSubsystem::Play(EBrSoundKind Kind, TSoftObjectPtr<USoundBase> Sound, const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	if (Kind != EBrSoundKind::Pickup)
	{
		const ABrPlayerController* PC = Cast<ABrPlayerController>(World->GetFirstPlayerController());
		if (!ListenerPawn(PC) || !FBrAudioRules::InRange(Location, ListenerPosition(PC), Range(Kind))) return;
	}
	UE_LOG(LogBr, Verbose, TEXT("Audio playback: Kind=%d Sound=%s Location=%s Device=%d"),
		static_cast<int32>(Kind), *Sound.ToString(), *Location.ToString(), World->GetAudioDevice().IsValid());
	// Headless smokes still exercise routing without touching the audio device.
	if (!World->GetAudioDevice().IsValid()) return;
	const FSoftObjectPath Path = Sound.ToSoftObjectPath();
	if (FailedSounds.Contains(Path)) return;
	USoundBase* Wave = Sounds.FindRef(Path);
	if (!Wave)
	{
		Wave = Sound.LoadSynchronous();
		if (!Wave)
		{
			FailedSounds.Add(Path);
			UE_LOG(LogBr, Error, TEXT("Audio sound missing: %s (Kind=%d)"), *Path.ToString(), static_cast<int32>(Kind));
			return;
		}
		Sounds.Add(Path, Wave);
	}
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const FBrAudioVoicePolicy& Policy = S->VoicePolicy(Kind);
	const EBrSoundKind Category = Kind == EBrSoundKind::SprintStep || Kind == EBrSoundKind::CrouchStep ? EBrSoundKind::RunStep : Kind;
	USoundConcurrency* Concurrency = Concurrencies.FindRef(Category);
	if (!Concurrency)
	{
		Concurrency = Policy.Concurrency.LoadSynchronous();
		if (!Concurrency)
		{
			UE_LOG(LogBr, Error, TEXT("Audio concurrency missing: %s"), *Policy.Concurrency.ToString());
			return;
		}
		Concurrencies.Add(Category, Concurrency);
	}
	if (Kind == EBrSoundKind::Pickup) { UGameplayStatics::PlaySound2D(World, Wave, 1, 1, 0, Concurrency); return; }
	USoundAttenuation* Attenuation = Attenuations.FindRef(Kind);
	if (!Attenuation)
	{
		Attenuation = NewObject<USoundAttenuation>(this);
		FSoundAttenuationSettings& A = Attenuation->Attenuation;
		A.bAttenuate = true;
		A.bSpatialize = true;
		A.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
		A.AttenuationShapeExtents = FVector(Range(Kind) * FMath::Clamp(S->AudioInnerRadiusFraction, 0.f, 0.9f), 0, 0);
		A.FalloffDistance = Range(Kind) - A.AttenuationShapeExtents.X;
		Attenuations.Add(Kind, Attenuation);
	}
	float Volume = Kind == EBrSoundKind::SprintStep ? S->SprintStepVolume :
		Kind == EBrSoundKind::CrouchStep ? S->CrouchStepVolume : 1.f;
	float Pitch = 1.f;
	if (Kind == EBrSoundKind::Fire)
	{
		Pitch += FMath::FRandRange(-S->FirePitchVariation, S->FirePitchVariation);
		Volume *= 1.f + FMath::FRandRange(-S->FireVolumeVariation, S->FireVolumeVariation);
	}
	FVector GroundLocation = Location;
	GroundLocation.Z = ListenerPosition(Cast<ABrPlayerController>(World->GetFirstPlayerController())).Z;
	UGameplayStatics::PlaySoundAtLocation(World, Wave, GroundLocation, Volume, Pitch, 0, Attenuation, Concurrency);
}
