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
#include "BrSoundEventRules.h"
#include "BrVoiceSet.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundClass.h"

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
	for (const auto& Entry : LocalReloads)
		if (UAudioComponent* Component = Entry.Value.Get()) Component->Stop();
	LocalReloads.Empty();
	ReloadListeners.Empty();
	VoiceLibrary.Empty();
	FailedVoiceSets.Empty();
	VoiceClass = nullptr;
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
	case EBrSoundKind::Slide: return S->SlideHearingRange;
	case EBrSoundKind::Reload: return S->ReloadHearingRange;
	case EBrSoundKind::DryFire: return S->DryFireHearingRange;
	case EBrSoundKind::Pain:
	case EBrSoundKind::PainArmor: return S->PainHearingRange;
	case EBrSoundKind::VoiceLine: return S->VoiceLineHearingRange;
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
	const FVector& Location, const AController* SourceController, bool bIncludeOwner)
{
	if (!Source || !Source->HasAuthority() || GetWorld()->GetNetMode() == NM_Client) return 0;
	int32 Sent = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ABrPlayerController* PC = Cast<ABrPlayerController>(It->Get());
		ABrCharacter* Pawn = ListenerPawn(PC);
		const bool bOwn = PC && (Source == PC->GetPawn() || SourceController == PC || Source->GetInstigatorController() == PC);
		if (!FBrAudioRules::SendOther(Pawn != nullptr,
			Pawn && FBrAudioRules::InRange(Location, Pawn->GetActorLocation(), Range(Kind)), bOwn && !bIncludeOwner)) continue;
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
	for (auto It = ReloadListeners.CreateIterator(); It; ++It)
	{
		if (It.Value().Source.IsValid()) continue;
		for (const auto& Listener : It.Value().Listeners)
			if (ABrPlayerController* PC = Listener.Get()) PC->ClientStopReloadSound(It.Key());
		It.RemoveCurrent();
	}
	for (auto It = LocalReloads.CreateIterator(); It; ++It)
		if (!It.Value().IsValid()) It.RemoveCurrent();
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

UAudioComponent* UBrAudioSubsystem::Play(EBrSoundKind Kind, TSoftObjectPtr<USoundBase> Sound, const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return nullptr;
	if (Kind != EBrSoundKind::Pickup && Kind != EBrSoundKind::ArmorBreak)
	{
		const ABrPlayerController* PC = Cast<ABrPlayerController>(World->GetFirstPlayerController());
		if (!ListenerPawn(PC) || !FBrAudioRules::InRange(Location, ListenerPosition(PC), Range(Kind))) return nullptr;
	}
	UE_LOG(LogBr, Verbose, TEXT("Audio playback: Kind=%d Sound=%s Location=%s Device=%d"),
		static_cast<int32>(Kind), *Sound.ToString(), *Location.ToString(), World->GetAudioDevice().IsValid());
	// Headless smokes still exercise routing without touching the audio device.
	if (!World->GetAudioDevice().IsValid()) return nullptr;
	const FSoftObjectPath Path = Sound.ToSoftObjectPath();
	if (FailedSounds.Contains(Path)) return nullptr;
	USoundBase* Wave = Sounds.FindRef(Path);
	if (!Wave)
	{
		Wave = Sound.LoadSynchronous();
		if (!Wave)
		{
			FailedSounds.Add(Path);
			UE_LOG(LogBr, Error, TEXT("Audio sound missing: %s (Kind=%d)"), *Path.ToString(), static_cast<int32>(Kind));
			return nullptr;
		}
		Sounds.Add(Path, Wave);
	}
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const FBrAudioVoicePolicy& Policy = S->VoicePolicy(Kind);
	const EBrSoundKind Category = Kind == EBrSoundKind::SprintStep || Kind == EBrSoundKind::CrouchStep ||
		Kind == EBrSoundKind::Slide ? EBrSoundKind::RunStep :
		Kind == EBrSoundKind::PainArmor ? EBrSoundKind::Pain : Kind;
	USoundConcurrency* Concurrency = Concurrencies.FindRef(Category);
	if (!Concurrency)
	{
		Concurrency = Policy.Concurrency.LoadSynchronous();
		if (!Concurrency)
		{
			if (Kind < EBrSoundKind::Reload)
			{
				UE_LOG(LogBr, Error, TEXT("Audio concurrency missing: %s"), *Policy.Concurrency.ToString());
				return nullptr;
			}
			if (!Policy.Concurrency.IsNull())
				UE_LOG(LogBr, Warning, TEXT("Audio concurrency missing: %s; using configured runtime budget"), *Policy.Concurrency.ToString());
			Concurrency = NewObject<USoundConcurrency>(this);
			Concurrency->Concurrency.MaxCount = FMath::Clamp(Policy.MaxCount, 1, 128);
			Concurrency->Concurrency.ResolutionRule = Policy.ResolutionRule;
			Concurrency->Concurrency.bLimitToOwner = false;
		}
		Concurrencies.Add(Category, Concurrency);
	}
	if (Kind == EBrSoundKind::Pickup || Kind == EBrSoundKind::ArmorBreak)
	{
		UGameplayStatics::PlaySound2D(World, Wave, 1, 1, 0, Concurrency);
		return nullptr;
	}
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
	// Set class routing before Play; never mutate a shared sound asset.
	UAudioComponent* Component = NewObject<UAudioComponent>(World);
	Component->bAutoActivate = false;
	Component->bAutoDestroy = true;
	Component->SetSound(Wave);
	Component->AttenuationSettings = Attenuation;
	Component->ConcurrencySet.Add(Concurrency);
	Component->bOverridePriority = true;
	Component->Priority = Policy.Priority;
	Component->bIsUISound = false;
	Component->SetVolumeMultiplier(Volume);
	Component->SetPitchMultiplier(Pitch);
	if (Kind == EBrSoundKind::VoiceLine)
		Component->SoundClassOverride = SoundClass(Kind);
	Component->SetWorldLocation(GroundLocation);
	Component->RegisterComponentWithWorld(World);
	Component->Play();
	return Component;
}

USoundClass* UBrAudioSubsystem::SoundClass(EBrSoundKind Kind)
{
	if (Kind != EBrSoundKind::VoiceLine) return nullptr;
	if (!bTriedVoiceClass)
	{
		bTriedVoiceClass = true;
		const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
		VoiceClass = S->VoiceSoundClass.LoadSynchronous();
		if (!VoiceClass)
		{
			UE_LOG(LogBr, Warning, TEXT("Voice sound class missing: %s; using effects"), *S->VoiceSoundClass.ToString());
			VoiceClass = S->EffectsSoundClass.LoadSynchronous();
		}
	}
	return VoiceClass;
}

uint32 UBrAudioSubsystem::StartReload(AActor* Source, TSoftObjectPtr<USoundBase> Sound)
{
	if (!IsValid(Source) || !Source->HasAuthority() || GetWorld()->GetNetMode() == NM_Client) return 0;
	do { ++NextReloadId; } while (NextReloadId == 0 || ReloadListeners.Contains(NextReloadId));
	FReloadListeners& Entry = ReloadListeners.Add(NextReloadId);
	Entry.Source = Source;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ABrPlayerController* PC = Cast<ABrPlayerController>(It->Get());
		ABrCharacter* Pawn = ListenerPawn(PC);
		if (!Pawn || !FBrAudioRules::InRange(Source->GetActorLocation(), Pawn->GetActorLocation(), Range(EBrSoundKind::Reload))) continue;
		Entry.Listeners.Add(PC);
		PC->ClientReloadSound(NextReloadId, Sound, Source->GetActorLocation());
	}
	return NextReloadId;
}

void UBrAudioSubsystem::StopReload(AActor* Source, uint32 Id)
{
	FReloadListeners* Entry = ReloadListeners.Find(Id);
	if (!Source || !Source->HasAuthority() || !Entry || Entry->Source != Source) return;
	// Do not re-test range/relevancy: everyone sent a start must receive a stop.
	for (const auto& Listener : Entry->Listeners)
		if (ABrPlayerController* PC = Listener.Get()) PC->ClientStopReloadSound(Id);
	ReloadListeners.Remove(Id);
}

void UBrAudioSubsystem::ForgetReload(uint32 Id) { ReloadListeners.Remove(Id); }

void UBrAudioSubsystem::PlayReload(uint32 Id, TSoftObjectPtr<USoundBase> Sound, const FVector& Location)
{
	StopLocalReload(Id);
	for (auto It = LocalReloads.CreateIterator(); It; ++It)
		if (!It.Value().IsValid()) It.RemoveCurrent();
	if (UAudioComponent* Component = Play(EBrSoundKind::Reload, Sound, Location))
		LocalReloads.Add(Id, Component);
}

void UBrAudioSubsystem::StopLocalReload(uint32 Id)
{
	if (const auto* Entry = LocalReloads.Find(Id))
		if (UAudioComponent* Component = Entry->Get()) Component->Stop();
	LocalReloads.Remove(Id);
}

UBrVoiceSet* UBrAudioSubsystem::VoiceSet(int32 Index)
{
	const auto& Sets = UBrDeveloperSettings::Get()->VoiceSets;
	Index = FBrSoundEventRules::VoiceIndex(Index, Sets.Num());
	if (!Sets.IsValidIndex(Index))
	{
		const FSoftObjectPath Empty;
		if (!FailedVoiceSets.Contains(Empty))
		{
			FailedVoiceSets.Add(Empty);
			UE_LOG(LogBr, Warning, TEXT("Voice library is empty; no down lines available"));
		}
		return nullptr;
	}
	const FSoftObjectPath Path = Sets[Index].ToSoftObjectPath();
	if (FailedVoiceSets.Contains(Path)) return nullptr;
	UBrVoiceSet* Set = VoiceLibrary.FindRef(Path);
	if (!Set) Set = Sets[Index].LoadSynchronous();
	if (!Set || !Set->DownLines.ContainsByPredicate([](const auto& Line) { return !Line.IsNull(); }))
	{
		FailedVoiceSets.Add(Path);
		UE_LOG(LogBr, Warning, TEXT("Voice set missing or empty: %s; no down line"), *Path.ToString());
		return nullptr;
	}
	VoiceLibrary.Add(Path, Set);
	return Set;
}

void UBrAudioSubsystem::SpeakDown(ABrCharacter* Attacker)
{
	if (!IsValid(Attacker) || !Attacker->HasAuthority()) return;
	ABrPlayerState* PS = Attacker->GetPlayerState<ABrPlayerState>();
	UBrVoiceSet* Set = PS ? VoiceSet(PS->GetVoiceIndex()) : nullptr;
	if (!Set) return;
	TArray<TSoftObjectPtr<USoundBase>> Lines;
	for (const auto& Line : Set->DownLines)
		if (!Line.IsNull()) Lines.AddUnique(Line);
	const int32 Last = Lines.IndexOfByPredicate([PS](const auto& Line) { return Line.ToSoftObjectPath() == PS->LastDownLine; });
	const int32 Index = FBrSoundEventRules::Line(Lines.Num(), Last,
		FMath::RandHelper(FBrSoundEventRules::ChoiceCount(Lines.Num(), Last)));
	if (!Lines.IsValidIndex(Index)) return;
	PS->LastDownLine = Lines[Index].ToSoftObjectPath();
	Emit(Attacker, EBrSoundKind::VoiceLine, Lines[Index], Attacker->GetActorLocation(), nullptr, true);
}
