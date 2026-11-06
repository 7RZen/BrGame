#include "BrGameMode.h"
#include "BrGameInstance.h"
#include "BrGameState.h"
#include "BrPlayerState.h"
#include "BrPlayerController.h"
#include "BrCharacter.h"
#include "BrSupinePose.h"
#include "BrCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "BrDeveloperSettings.h"
#include "BrInventoryComponent.h"
#include "BrCombatDefinitions.h"
#include "BrHealthComponent.h"
#include "BrArmorComponent.h"
#include "BrAudioSubsystem.h"
#include "BrVoiceSet.h"
#include "BrZoneDamageType.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "BrHitFeedbackComponent.h"
#include "BrFogComponent.h"
#include "BrVisibilitySubsystem.h"
#include "BrLifeComponent.h"
#include "BrCombatMath.h"
#include "BrPickup.h"
#include "BrPickupManager.h"
#include "BrPickupDefinition.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace
{
	int32 MatchSmokeRound = 0;
	bool bMatchSmokeFailed = false;
	FBrHitChanceRow SavedStanding;
	FBrHitChanceRow SavedCrouching;
	FBrHitChanceRow SavedProne;
	bool bSavedHitChance = false;

	void RestoreHitChance()
	{
		if (!bSavedHitChance) return;
		if (UBrDeveloperSettings* Settings = const_cast<UBrDeveloperSettings*>(UBrDeveloperSettings::Get()))
		{
			Settings->StandingHitChance = SavedStanding;
			Settings->CrouchingHitChance = SavedCrouching;
			Settings->ProneHitChance = SavedProne;
		}
		bSavedHitChance = false;
	}

	void RestoreFriendlyFire(ABrGameState* State, bool& bChanged, bool Original)
	{
		if (!bChanged) return;
		if (State) State->MatchSettings.bFriendlyFire = Original;
		bChanged = false;
	}

	void FailSmoke(const FString& Reason)
	{
		RestoreHitChance();
		bMatchSmokeFailed = true;
		UE_LOG(LogBr, Error, TEXT("E2E MATCH SMOKE FAIL: %s"), *Reason);
	}

	bool CheckSpawnClear(UWorld* World, const ABrCharacter* Character)
	{
		if (!World || !Character) return false;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(BrMatchSmokeSpawn), false, Character);
		// Leave a small margin above the ground-contacting capsule so the floor itself is not
		// mistaken for an obstruction; walls/props crossing the body remain detectable.
		return !World->OverlapAnyTestByObjectType(Character->GetActorLocation(), FQuat::Identity, Objects,
			FCollisionShape::MakeCapsule(38.0f, 78.0f), Query);
	}

	bool CheckAliveMeshPose(const ABrCharacter* Character)
	{
		return Character &&
			FMath::IsNearlyZero(Character->GetMesh()->GetRelativeRotation().Pitch, 2.f) &&
			FMath::IsNearlyZero(Character->GetBaseRotationOffset().Rotator().Pitch, 2.f) &&
			FMath::IsNearlyEqual(Character->GetBaseTranslationOffset().Z,
				-Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 0.1f);
	}

	bool ExercisePickups(UWorld* World, ABrCharacter* Character)
	{
		UBrDeveloperSettings* Settings = const_cast<UBrDeveloperSettings*>(UBrDeveloperSettings::Get());
		if (!World || !Character || !Settings) return false;
		const auto FindPickup = [Settings](FName Name) -> UBrPickupDefinition*
		{
			const TSoftObjectPtr<UBrPickupDefinition>* Entry = Settings->PickupRegistry.Find(Name);
			return Entry ? Entry->LoadSynchronous() : nullptr;
		};
		const auto Collect = [World, Character](UBrPickupDefinition* Definition)
		{
			ABrPickup* Pickup = ABrPickup::Spawn(World,
				FTransform(Character->GetActorLocation() + FVector(0, 0, 250)), Definition, FBrPickupPayload());
			return Pickup && Pickup->TryCollect(Character);
		};
		const TSoftObjectPtr<UBrWeaponDefinition>* RifleEntry = Settings->WeaponRegistry.Find(TEXT("AssaultRifle"));
		UBrWeaponDefinition* Rifle = RifleEntry ? RifleEntry->LoadSynchronous() : nullptr;
		if (!Rifle || !Collect(FindPickup(TEXT("AssaultRifle"))) || Character->GetInventory()->Weapon != Rifle)
			return false;

		Character->GetHealth()->SetHealth(Character->GetHealth()->MaxHealth - 5.0f);
		if (!Collect(FindPickup(TEXT("Health100"))) ||
			!FMath::IsNearlyEqual(Character->GetHealth()->Health, Character->GetHealth()->MaxHealth))
			return false;

		UBrInventoryComponent* Inventory = Character->GetInventory();
		Inventory->GiveGrenades(1);
		if (!Collect(FindPickup(TEXT("Grenade"))) || Inventory->Grenades != 2) return false;
		ABrPickup* AtCap = ABrPickup::Spawn(World,
			FTransform(Character->GetActorLocation() + FVector(0, 0, 250)), FindPickup(TEXT("Grenade")), FBrPickupPayload());
		const bool bAtCapRejected = AtCap && !AtCap->TryCollect(Character) && Inventory->Grenades == 2;
		if (AtCap) AtCap->Destroy();
		return bAtCapRejected;
	}
}
#endif

void ABrPlayerController::ClientPostureSmoke_Implementation(EBrPosture Posture)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || !IsLocalController()) return;
	if (ABrCharacter* BrPawn = Cast<ABrCharacter>(GetPawn()))
		BrPawn->GetBrMovement()->RequestPosture(Posture);
#endif
}

void ABrPlayerController::ClientArmorSmokeProbe_Implementation(int32 Stage, int32 TargetId, bool bOwner)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || !IsLocalController() ||
		HasAuthority() || Stage < 1 || Stage > 3) return;
	GetWorldTimerManager().ClearTimer(ArmorSmokeProbeTimer);
	ArmorSmokeProbeStage = Stage;
	ArmorSmokeProbeTargetId = TargetId;
	bArmorSmokeProbeOwner = bOwner;
	ArmorSmokeProbeDeadline = GetWorld()->GetTimeSeconds() + 5;
	ArmorSmokeStableSince = -1;
	if (Stage == 1)
	{
		ArmorSmokeSoundBaseline = ArmorSmokeBreakSounds;
		bArmorSmokeBaselineCaptured = false;
	}
	GetWorldTimerManager().SetTimer(ArmorSmokeProbeTimer, this, &ThisClass::ServiceArmorSmokeProbe, 0.1f, true);
	ServiceArmorSmokeProbe();
#endif
}

void ABrPlayerController::ServerArmorSmokeResult_Implementation(int32 Stage, int32 TargetId, bool bPass)
{
#if !UE_BUILD_SHIPPING
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	// This acknowledgement cannot grant armor, apply damage or choose spectator targets.
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || !HasAuthority() || IsLocalController() ||
		!Mode || !Mode->IsAdmitted(this) || Stage != ArmorSmokeExpectedStage ||
		TargetId != ArmorSmokeExpectedTargetId) return;
	bArmorSmokePassed = bPass;
	if (!bPass) UE_LOG(LogBr, Error, TEXT("E2E ARMOR PROBE FAIL: remote assertion Player=%s Stage=%d"), *GetName(), Stage);
#endif
}

void ABrPlayerController::ClientSoundSmokeVoice_Implementation(int32 Index)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || HasAuthority() || !IsLocalController()) return;
	// Production validated preference RPC, without writing the user's local save.
	ServerSetVoiceIndex(Index);
#endif
}

void ABrPlayerController::ClientSoundSmokeProbe_Implementation(int32 Stage, FVector_NetQuantize Origin,
	const TSoftObjectPtr<USoundBase>& ReloadSound, bool bCheck)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || HasAuthority() || !IsLocalController() ||
		Stage < 1 || Stage > 8) return;
	if (!bCheck)
	{
		SoundProbeStage = Stage;
		SoundProbeOrigin = Origin;
		SoundProbeReloadSound = ReloadSound;
		SoundProbeReloadId = 0;
		SoundProbeReload = SoundProbeStop = SoundProbeDry = SoundProbePain = SoundProbeArmor = SoundProbeVoice = 0;
		bSoundProbeBad = bSoundProbeVoiceClass = false;
		ServerSoundSmokeResult(Stage, 0); // Armed: server cannot emit before this acknowledgement.
		return;
	}
	if (Stage != SoundProbeStage) return;
	bool bPass = !bSoundProbeBad;
	bPass &= SoundProbeReload == (Stage <= 3 ? 1 : 0) && SoundProbeStop == (Stage <= 3 ? 1 : 0);
	bPass &= SoundProbeDry == (Stage == 4 ? 2 : 0);
	bPass &= SoundProbePain == (Stage == 5 ? 1 : 0) && SoundProbeArmor == (Stage == 5 ? 1 : 0);
	bPass &= SoundProbeVoice == (Stage == 8 ? 1 : 0) && (Stage != 8 || bSoundProbeVoiceClass);
	if (!bPass)
	{
		UE_LOG(LogBr, Error, TEXT("E2E SOUND PROBE FAIL Stage=%d Reload=%d Stop=%d Dry=%d Pain=%d PainArmor=%d Voice=%d Class=%d Bad=%d"),
			Stage, SoundProbeReload, SoundProbeStop, SoundProbeDry, SoundProbePain, SoundProbeArmor,
			SoundProbeVoice, bSoundProbeVoiceClass, bSoundProbeBad);
	}
	else if (Stage <= 3)
	{
		UE_LOG(LogBr, Display, TEXT("E2E SOUND PROBE PASS Reload=1 Stop=1 Stage=%d Sound=%s Handle=%u"),
			Stage, *ReloadSound.ToString(), SoundProbeReloadId);
	}
	else if (Stage == 4)
	{
		UE_LOG(LogBr, Display, TEXT("E2E SOUND PROBE PASS DryFire=2 Attempts=2 Throttled=1"));
	}
	else if (Stage == 5)
	{
		UE_LOG(LogBr, Display, TEXT("E2E SOUND PROBE PASS Pain=1 PainArmor=1 NonDowning=1"));
	}
	else if (Stage == 6)
	{
		UE_LOG(LogBr, Display, TEXT("E2E SOUND PROBE PASS Zone=1 Pain=0 PainArmor=0"));
	}
	else if (Stage == 7)
	{
		UE_LOG(LogBr, Display, TEXT("E2E SOUND PROBE PASS OutOfRange=1 Pain=0 PainArmor=0"));
	}
	else
	{
		UE_LOG(LogBr, Display, TEXT("E2E SOUND PROBE PASS VoiceLine=1 VoiceIndex=1 VoiceClass=1 AttackerOrigin=1"));
	}
	ServerSoundSmokeResult(Stage, bPass ? 2 : 3);
	SoundProbeStage = 0;
#endif
}

void ABrPlayerController::ServerSoundSmokeResult_Implementation(int32 Stage, int32 Ack)
{
#if !UE_BUILD_SHIPPING
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || IsLocalController() ||
		!Mode || !Mode->IsAdmitted(this) || Stage != SoundSmokeExpectedStage || Ack < 0 || Ack > 3) return;
	// Acknowledgements only report assertions; never mutate gameplay or select the next fixture.
	if (Ack == 3)
		UE_LOG(LogBr, Error, TEXT("E2E SOUND PROBE FAIL: remote assertion Stage=%d"), Stage);
	SoundSmokeAck = Ack;
#endif
}

#if !UE_BUILD_SHIPPING
void ABrPlayerController::BeginSoundSmokeProbe(int32 Stage, const FVector& Origin, TSoftObjectPtr<USoundBase> ReloadSound)
{
	if (!HasAuthority() || IsLocalController() || !FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	SoundSmokeExpectedStage = Stage;
	SoundSmokeAck = -1;
	SoundProbeOrigin = Origin;
	SoundProbeReloadSound = ReloadSound;
	ClientSoundSmokeProbe(Stage, Origin, ReloadSound, false);
}

void ABrPlayerController::CheckSoundSmokeProbe(int32 Stage)
{
	ClientSoundSmokeProbe(Stage, SoundProbeOrigin, SoundProbeReloadSound, true);
}

void ABrPlayerController::TrackSoundSmokeEvent(EBrSoundKind Kind, TSoftObjectPtr<USoundBase> Sound, const FVector& Location)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || SoundProbeStage == 0) return;
	if (Kind == EBrSoundKind::DryFire) ++SoundProbeDry;
	else if (Kind == EBrSoundKind::Pain) ++SoundProbePain;
	else if (Kind == EBrSoundKind::PainArmor) ++SoundProbeArmor;
	else if (Kind == EBrSoundKind::VoiceLine) ++SoundProbeVoice;
	else return;
	bSoundProbeBad |= !Location.Equals(SoundProbeOrigin, 2);
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	if (Kind == EBrSoundKind::DryFire) bSoundProbeBad |= Sound != S->DryFireSound;
	if (Kind == EBrSoundKind::Pain) bSoundProbeBad |= Sound != S->PainSound;
	if (Kind == EBrSoundKind::PainArmor) bSoundProbeBad |= Sound != S->PainArmorSound;
	if (Kind == EBrSoundKind::VoiceLine)
	{
		UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>();
		UBrVoiceSet* Set = Audio ? Audio->VoiceSet(1) : nullptr;
		USoundBase* Line = Sound.LoadSynchronous();
		USoundClass* Class = S->VoiceSoundClass.LoadSynchronous();
		bSoundProbeBad |= !Set || !Set->DownLines.Contains(Sound) || !Line;
		// Same class resolver used by production UAudioComponent::SoundClassOverride.
		// -nosound validates routing/assets, not audible playback or a live component.
		bSoundProbeVoiceClass = Class && Audio && Audio->SoundClass(Kind) == Class && Line && Line->SoundClassObject == Class;
	}
}

bool ABrGameMode::ServiceSoundSmoke(ABrPlayerController* Host, ABrPlayerController* Observer)
{
	if (SoundSmokeStage == 9) return true;
	ABrCharacter* Source = Host ? Cast<ABrCharacter>(Host->GetPawn()) : nullptr;
	ABrCharacter* Receiver = Observer ? Cast<ABrCharacter>(Observer->GetPawn()) : nullptr;
	UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>();
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const float Now = GetWorld()->GetTimeSeconds();
	if (!Source || !Receiver || !Audio || !Observer || Observer->IsLocalController())
	{
		FailSmoke(TEXT("E2E SOUND PROBE FAIL: missing source/remote listener"));
		return false;
	}
	if (SoundSmokeStage == 0)
	{
		ABrCharacter* Attacker = HitSmokeVictim.Get();
		ABrPlayerController* PC = Attacker ? Cast<ABrPlayerController>(Attacker->GetController()) : nullptr;
		if (!PC || PC->IsLocalController() || PC == Observer || !Audio->VoiceSet(1) ||
			!Source->GetArmor()->GetPlates().IsEmpty())
		{
			FailSmoke(TEXT("E2E SOUND PROBE FAIL: remote attacker/voice set 1/empty host armor required"));
			return false;
		}
		SoundSmokeAttacker = Attacker;
		SoundSmokeAttackerPC = PC;
		SoundSmokeVoiceIndex = PC->GetPlayerState<ABrPlayerState>()->GetVoiceIndex();
		SoundSmokeVoiceRpcBaseline = PC->GetSoundSmokeVoiceRpcCount();
		SoundSmokeGear = Source->GetInventory()->CaptureState();
		SoundSmokeHealth = Source->GetHealth()->Health;
		SoundSmokeHostLocation = Source->GetActorLocation();
		SoundSmokeObserverLocation = Receiver->GetActorLocation();
		SoundSmokeAttackerLocation = Attacker->GetActorLocation();
		SoundSmokeHostTeam = Source->GetPlayerState<ABrPlayerState>()->Team;
		SoundSmokeObserverTeam = Receiver->GetPlayerState<ABrPlayerState>()->Team;
		SoundSmokeAttackerTeam = Attacker->GetPlayerState<ABrPlayerState>()->Team;
		SoundSmokeReviveRule = GetGameState<ABrGameState>()->MatchSettings.ReviveRule;
		SoundSmokeHostMode = Source->GetBrMovement()->MovementMode;
		SoundSmokeObserverMode = Receiver->GetBrMovement()->MovementMode;
		SoundSmokeAttackerMode = Attacker->GetBrMovement()->MovementMode;
		SoundSmokeHostCustomMode = Source->GetBrMovement()->CustomMovementMode;
		SoundSmokeObserverCustomMode = Receiver->GetBrMovement()->CustomMovementMode;
		SoundSmokeAttackerCustomMode = Attacker->GetBrMovement()->CustomMovementMode;
		Source->GetPlayerState<ABrPlayerState>()->Team = 11001;
		Receiver->GetPlayerState<ABrPlayerState>()->Team = 11002;
		Attacker->GetPlayerState<ABrPlayerState>()->Team = 11003;
		for (ABrCharacter* Pawn : {Source, Receiver, Attacker})
		{
			Pawn->GetBrMovement()->DisableMovement();
			Pawn->GetBrMovement()->Velocity = FVector::ZeroVector;
		}
		Source->SetActorLocation(FVector(-6000, -6000, 100), false, nullptr, ETeleportType::TeleportPhysics);
		Receiver->SetActorLocation(Source->GetActorLocation() + FVector(300, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		Attacker->SetActorLocation(Source->GetActorLocation() + FVector(100, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		for (ABrCharacter* Pawn : {Source, Receiver, Attacker}) Pawn->ForceNetUpdate();
		PC->ClientSoundSmokeVoice(1);
		SoundSmokeStage = 1;
		SoundSmokePhase = 0;
		SoundSmokeNext = Now + 1;
		SoundSmokeDeadline = Now + 7;
		return false;
	}
	ABrCharacter* Attacker = SoundSmokeAttacker.Get();
	ABrPlayerController* AttackerPC = SoundSmokeAttackerPC.Get();
	if (!Attacker || !AttackerPC || Now > SoundSmokeDeadline || Observer->GetSoundSmokeAck() == 3)
	{
		FailSmoke(FString::Printf(TEXT("E2E SOUND PROBE FAIL: bounded fixture Stage=%d Phase=%d Ack=%d"),
			SoundSmokeStage, SoundSmokePhase, Observer->GetSoundSmokeAck()));
		return false;
	}
	if (Now < SoundSmokeNext) return false;
	UBrInventoryComponent* Inv = Source->GetInventory();
	TSoftObjectPtr<USoundBase> ReloadSound;
	if (SoundSmokeStage <= 3)
	{
		const FName Names[] = {TEXT("Pistol"), TEXT("AssaultRifle"), TEXT("MachineGun")};
		const auto* Entry = S->WeaponRegistry.Find(Names[SoundSmokeStage - 1]);
		UBrWeaponDefinition* Weapon = Entry ? Entry->LoadSynchronous() : nullptr;
		if (!Weapon || Weapon->ReloadSound.IsNull() || !Weapon->ReloadSound.LoadSynchronous())
		{
			FailSmoke(TEXT("E2E SOUND PROBE FAIL: per-weapon reload asset missing"));
			return false;
		}
		ReloadSound = Weapon->ReloadSound;
		if (SoundSmokePhase == 0)
		{
			FBrInventoryState Gear = SoundSmokeGear;
			Gear.Weapon = Weapon;
			Gear.Loaded = 0;
			Gear.Ammo.Reset();
			Inv->RestoreState(Gear);
			Inv->GiveAmmo(10);
		}
	}
	if (SoundSmokePhase == 0)
	{
		if (AttackerPC->GetPlayerState<ABrPlayerState>()->GetVoiceIndex() != 1 ||
			AttackerPC->GetSoundSmokeVoiceRpcCount() <= SoundSmokeVoiceRpcBaseline) return false;
		const FVector Origin = SoundSmokeStage == 8 ? Attacker->GetActorLocation() : Source->GetActorLocation();
		Observer->BeginSoundSmokeProbe(SoundSmokeStage, Origin, ReloadSound);
		SoundSmokePhase = 1;
		SoundSmokeDeadline = Now + 7;
		return false;
	}
	if (SoundSmokePhase == 1)
	{
		if (Observer->GetSoundSmokeAck() != 0) return false;
		if (SoundSmokeStage <= 3)
		{
			Inv->RequestReload();
			if (!Inv->bReloading) { FailSmoke(TEXT("E2E SOUND PROBE FAIL: authoritative reload rejected")); return false; }
			SoundSmokeNext = Now + 0.1f;
		}
		else if (SoundSmokeStage == 4)
		{
			FBrInventoryState Gear = Inv->CaptureState();
			Gear.Loaded = 0; Gear.Ammo.Reset();
			Inv->RestoreState(Gear);
			for (int32 Attempt = 0; Attempt < 20; ++Attempt)
				Inv->ServerFire_Implementation(Source->GetActorForwardVector(), Source->GetBrMovement()->GetCombatMoveTimeStamp());
			SoundSmokeNext = Now + 1.f / Inv->Weapon->FireRate + 0.1f;
		}
		else if (SoundSmokeStage >= 5 && SoundSmokeStage <= 7)
		{
			Source->GetArmor()->Clear();
			const bool bZone = SoundSmokeStage == 6;
			const FBrDamageEvent Event(CaptureDamageSource(AttackerPC),
				bZone ? UBrZoneDamageType::StaticClass() : UBrBulletDamageType::StaticClass(), Attacker->GetActorLocation());
			const float Before = Source->GetHealth()->Health;
			const float Applied = Source->TakeDamage(bZone ? 1 : 1 / S->StandingDamageMultiplier, Event, AttackerPC, Attacker);
			if (Applied <= 0 || Source->GetHealth()->Health >= Before || !Source->GetLife()->IsAlive())
			{ FailSmoke(TEXT("E2E SOUND PROBE FAIL: non-downing damage rejected")); return false; }
			SoundSmokeNext = Now + FMath::Max(0.6f, S->PainSoundMinInterval + 0.1f);
		}
		else if (SoundSmokeStage == 8)
		{
			GetGameState<ABrGameState>()->MatchSettings.ReviveRule = EBrReviveRule::Anyone;
			const FBrDamageEvent Event(CaptureDamageSource(AttackerPC), UBrBulletDamageType::StaticClass(), Attacker->GetActorLocation());
			const int32 Downs = AttackerPC->GetPlayerState<ABrPlayerState>()->DownsInflicted;
			Source->TakeDamage(Source->GetHealth()->MaxHealth * 2 / S->StandingDamageMultiplier, Event, AttackerPC, Attacker);
			UBrVoiceSet* Set = Audio->VoiceSet(AttackerPC->GetPlayerState<ABrPlayerState>()->GetVoiceIndex());
			const FSoftObjectPath Line = AttackerPC->GetPlayerState<ABrPlayerState>()->LastDownLine;
			if (Source->GetLife()->State != EBrLifeState::Downed ||
				AttackerPC->GetPlayerState<ABrPlayerState>()->DownsInflicted != Downs + 1 || !Set ||
				!Set->DownLines.Contains(TSoftObjectPtr<USoundBase>(Line)))
			{ FailSmoke(TEXT("E2E SOUND PROBE FAIL: CreditDown did not use RPC-selected voice")); return false; }
			UE_LOG(LogBr, Display, TEXT("E2E SOUND PROBE PASS Authority=1 VoiceIndex=1 RPC=1 OpponentDown=1 Line=%s"), *Line.ToString());
			Attacker->GetLife()->StartInteract(); // Real revive keeps the existing round/life checks intact.
			SoundSmokeNext = Now + 2;
		}
		SoundSmokePhase = 2;
		return false;
	}
	if (SoundSmokePhase == 2)
	{
		if (SoundSmokeStage <= 3)
		{
			if (Observer->GetSoundSmokeAck() != 1) return false;
			Inv->RestoreState(SoundSmokeGear); // Real cancellation path, original received handle must stop.
		}
		else if (SoundSmokeStage == 4)
		{
			for (int32 Attempt = 0; Attempt < 20; ++Attempt)
				Inv->ServerFire_Implementation(Source->GetActorForwardVector(), Source->GetBrMovement()->GetCombatMoveTimeStamp());
		}
		else if (SoundSmokeStage == 5 || SoundSmokeStage == 7)
		{
			Source->GetArmor()->AddPlate();
			const float Before = Source->GetHealth()->Health;
			const FBrDamageEvent Event(CaptureDamageSource(AttackerPC), UBrBulletDamageType::StaticClass(), Attacker->GetActorLocation());
			const float Applied = Source->TakeDamage(1 / S->StandingDamageMultiplier, Event, AttackerPC, Attacker);
			if (Applied <= 0 || Source->GetHealth()->Health != Before)
			{ FailSmoke(TEXT("E2E SOUND PROBE FAIL: full armor absorption rejected")); return false; }
			Source->GetArmor()->Clear();
		}
		if (SoundSmokeStage == 8 && !Source->GetLife()->IsAlive()) return false;
		SoundSmokeNext = Now + 1;
		SoundSmokePhase = 3;
		return false;
	}
	if (SoundSmokePhase == 3)
	{
		Observer->CheckSoundSmokeProbe(SoundSmokeStage);
		SoundSmokePhase = 4;
		return false;
	}
	if (SoundSmokePhase == 4 && Observer->GetSoundSmokeAck() == 2)
	{
		if (SoundSmokeStage == 8)
		{
			Attacker->GetLife()->StopInteract();
			GetGameState<ABrGameState>()->MatchSettings.ReviveRule = SoundSmokeReviveRule;
			Source->GetHealth()->SetHealth(SoundSmokeHealth);
			Source->GetLife()->InvulnerabilityEndTime = 0;
			Inv->RestoreState(SoundSmokeGear);
			Source->GetPlayerState<ABrPlayerState>()->Team = SoundSmokeHostTeam;
			Receiver->GetPlayerState<ABrPlayerState>()->Team = SoundSmokeObserverTeam;
			Attacker->GetPlayerState<ABrPlayerState>()->Team = SoundSmokeAttackerTeam;
			Source->SetActorLocation(SoundSmokeHostLocation, false, nullptr, ETeleportType::TeleportPhysics);
			Receiver->SetActorLocation(SoundSmokeObserverLocation, false, nullptr, ETeleportType::TeleportPhysics);
			Attacker->SetActorLocation(SoundSmokeAttackerLocation, false, nullptr, ETeleportType::TeleportPhysics);
			Source->GetBrMovement()->SetMovementMode(static_cast<EMovementMode>(SoundSmokeHostMode), SoundSmokeHostCustomMode);
			Receiver->GetBrMovement()->SetMovementMode(static_cast<EMovementMode>(SoundSmokeObserverMode), SoundSmokeObserverCustomMode);
			Attacker->GetBrMovement()->SetMovementMode(static_cast<EMovementMode>(SoundSmokeAttackerMode), SoundSmokeAttackerCustomMode);
			AttackerPC->ClientSoundSmokeVoice(SoundSmokeVoiceIndex);
			for (ABrCharacter* Pawn : {Source, Receiver, Attacker}) Pawn->ForceNetUpdate();
			RecordPlayer(Host); // Replace the transient downed round snapshot with the restored living state.
			SoundSmokeStage = 9;
			return true;
		}
		++SoundSmokeStage;
		if (SoundSmokeStage == 7)
			Receiver->SetActorLocation(Source->GetActorLocation() + FVector(S->PainHearingRange + 500, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		else
			Receiver->SetActorLocation(Source->GetActorLocation() + FVector(300, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		Receiver->ForceNetUpdate();
		SoundSmokePhase = 0;
		SoundSmokeNext = Now + 1;
		SoundSmokeDeadline = Now + 7;
	}
	return false;
}

void ABrPlayerController::BeginArmorSmokeProbe(int32 Stage, ABrCharacter* Target)
{
	if (!HasAuthority() || IsLocalController() || !Target ||
		!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	const ABrPlayerState* PS = Target->GetPlayerState<ABrPlayerState>();
	if (!PS) return;
	ArmorSmokeExpectedStage = Stage;
	ArmorSmokeExpectedTargetId = PS->GetPlayerId();
	bArmorSmokePassed = false;
	ClientArmorSmokeProbe(Stage, PS->GetPlayerId(), GetPawn() == Target);
}

void ABrPlayerController::ServiceArmorSmokeProbe()
{
	// A one-shot actor RPC parameter can be null before a fog channel opens.
	// Resolve the public identity afresh, just as flow retries its replicated view target.
	ABrCharacter* Target = nullptr;
	for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
	{
		const ABrPlayerState* PS = It->GetPlayerState<ABrPlayerState>();
		if (PS && PS->GetPlayerId() == ArmorSmokeProbeTargetId && !It->IsActorBeingDestroyed())
		{
			Target = *It;
			break;
		}
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const int32 Stage = ArmorSmokeProbeStage;
	const bool bOwner = bArmorSmokeProbeOwner;
	const TArray<float> Expected = Stage == 2 ? TArray<float>{17} : TArray<float>{7, 20};
	bool bGood = Target && Target->GetPlayerState<ABrPlayerState>();
	bool bLeaked = false;
	if (bGood)
	{
		if (Stage == 1 && !bArmorSmokeBaselineCaptured)
		{
			ArmorSmokeHitBaseline = Target->GetHitFeedback()->OwnerEvents;
			ArmorSmokeHealth = Target->GetHealth()->Health;
			bArmorSmokeBaselineCaptured = true;
		}
		const TArray<float>& Plates = Target->GetArmor()->GetPlates();
		// The server-only PlayerState backing store must never become another public route.
		bLeaked = !Target->GetPlayerState<ABrPlayerState>()->GetArmorPlates().IsEmpty();
		if (Stage == 3)
		{
			const TArray<float>* Mirror = GetSpectatorArmor(Target);
			bGood = !bOwner && IsFlowSpectating() && Mirror && *Mirror == Expected && Plates.IsEmpty();
		}
		else if (bOwner)
		{
			bGood = GetPawn() == Target && !IsFlowSpectating() && Plates == Expected;
			if (Stage == 2)
				bGood &= FMath::IsNearlyEqual(Target->GetHealth()->Health, ArmorSmokeHealth) &&
					Target->GetHitFeedback()->OwnerEvents - ArmorSmokeHitBaseline == 1 &&
					ArmorSmokeBreakSounds - ArmorSmokeSoundBaseline == 1;
		}
		else
		{
			bLeaked |= !Plates.IsEmpty() || !SpectatorArmor.Plates.IsEmpty();
			bGood = GetPawn() != Target && !IsFlowSpectating() && UBrFogComponent::LocallyVisible(Target) &&
				!bLeaked && ArmorSmokeBreakSounds == ArmorSmokeSoundBaseline;
		}
		bGood &= !bLeaked;
	}
	if (bGood && ArmorSmokeStableSince < 0) ArmorSmokeStableSince = Now;
	if (!bGood) ArmorSmokeStableSince = -1;
	// Observe a real, visible actor for a full second: a missing fog channel is not a privacy PASS.
	const bool bPass = bGood && Now - ArmorSmokeStableSince >= 1;
	if (!bPass && !bLeaked && Now < ArmorSmokeProbeDeadline) return;
	GetWorldTimerManager().ClearTimer(ArmorSmokeProbeTimer);
	if (!bPass)
	{
		UE_LOG(LogBr, Error, TEXT("E2E ARMOR PROBE FAIL Owner=%d Stage=%d Target=%s Leak=%d Health=%.1f HitEvents=%u BreakSounds=%u"),
			bOwner, Stage, *GetNameSafe(Target), bLeaked, Target ? Target->GetHealth()->Health : -1.f,
			Target ? Target->GetHitFeedback()->OwnerEvents - ArmorSmokeHitBaseline : 0,
			ArmorSmokeBreakSounds - ArmorSmokeSoundBaseline);
	}
	else if (Stage == 3)
	{
		UE_LOG(LogBr, Display, TEXT("E2E ARMOR PROBE PASS Spectator=1 Mirror=1 Plates=7,20 ComponentPrivate=1"));
	}
	else if (bOwner)
	{
		UE_LOG(LogBr, Display, TEXT("E2E ARMOR PROBE PASS Owner=1 Stage=%d Plates=%s Health=%.1f HitEvents=%u BreakSounds=%u"),
			Stage, Stage == 1 ? TEXT("7,20") : TEXT("17"), Target->GetHealth()->Health,
			Target->GetHitFeedback()->OwnerEvents - ArmorSmokeHitBaseline, ArmorSmokeBreakSounds - ArmorSmokeSoundBaseline);
	}
	else
	{
		UE_LOG(LogBr, Display, TEXT("E2E ARMOR PROBE PASS Owner=0 Private=1 Stage=%d Visible=1 BreakSounds=0"), Stage);
	}
	ServerArmorSmokeResult(Stage, ArmorSmokeProbeTargetId, bPass);
}

bool ABrGameMode::ServiceArmorSmoke(ABrPlayerController* Host, ABrPlayerController* Observer)
{
	if (!HasAuthority() || !FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return false;
	if (ArmorSmokeStage >= 4) return true;
	const float Now = GetWorld()->GetTimeSeconds();
	ABrCharacter* Source = Host ? Cast<ABrCharacter>(Host->GetPawn()) : nullptr;
	ABrCharacter* ObserverPawn = Observer ? Cast<ABrCharacter>(Observer->GetPawn()) : nullptr;
	if (ArmorSmokeStage == 0)
	{
		ABrCharacter* Victim = HitSmokeVictim.Get();
		ABrPlayerController* VictimPC = Victim ? Cast<ABrPlayerController>(Victim->GetController()) : nullptr;
		const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
		if (!Source || !ObserverPawn || !Victim || !VictimPC || VictimPC->IsLocalController() ||
			!FMath::IsNearlyEqual(S->ArmorPlateValue, 20.f) || S->MaxArmorPlates != 2)
		{
			FailSmoke(TEXT("E2E ARMOR PROBE FAIL: fixture needs two 20-value plates and distinct remote pawns."));
			return false;
		}
		ArmorSmokeVictim = Victim;
		ArmorSmokeVictimController = VictimPC;
		ArmorSmokeVictimLocation = Victim->GetActorLocation();
		ArmorSmokeObserverLocation = ObserverPawn->GetActorLocation();
		ArmorSmokeVictimMode = Victim->GetBrMovement()->MovementMode;
		ArmorSmokeObserverMode = ObserverPawn->GetBrMovement()->MovementMode;
		ArmorSmokeVictimCustomMode = Victim->GetBrMovement()->CustomMovementMode;
		ArmorSmokeObserverCustomMode = ObserverPawn->GetBrMovement()->CustomMovementMode;
		ArmorSmokeVictimTeam = Victim->GetPlayerState<ABrPlayerState>()->Team;
		ArmorSmokeObserverTeam = ObserverPawn->GetPlayerState<ABrPlayerState>()->Team;
		Victim->GetPlayerState<ABrPlayerState>()->Team = 10001;
		ObserverPawn->GetPlayerState<ABrPlayerState>()->Team = 10002;
		Victim->GetBrMovement()->DisableMovement();
		ObserverPawn->GetBrMovement()->DisableMovement();
		// Same known visible lane as the existing hit fixture; restore both real spawns afterward.
		Victim->SetActorLocation(FVector(-6000, -6000, 100), false, nullptr, ETeleportType::TeleportPhysics);
		ObserverPawn->SetActorLocation(Victim->GetActorLocation() + FVector(200, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		Victim->GetArmor()->Clear();
		const bool bFirst = Victim->GetArmor()->AddPlate();
		Victim->GetArmor()->Absorb(13, false); // Partial plate, no health/feedback/sound event.
		const bool bSecond = Victim->GetArmor()->AddPlate();
		if (!bFirst || !bSecond || Victim->GetArmor()->GetPlates() != TArray<float>({7, 20}))
		{
			FailSmoke(TEXT("E2E ARMOR PROBE FAIL: authority grant did not produce 7,20."));
			return false;
		}
		ArmorSmokeHealth = Victim->GetHealth()->Health;
		Victim->ForceNetUpdate();
		ObserverPawn->ForceNetUpdate();
		GetWorld()->GetSubsystem<UBrVisibilitySubsystem>()->RequestDiscovery(Victim, Observer);
		ArmorSmokeStage = 1;
		ArmorSmokeDeadline = Now + 2;
		return false;
	}
	ABrCharacter* Victim = ArmorSmokeVictim.Get();
	ABrPlayerController* VictimPC = ArmorSmokeVictimController.Get();
	if (!Source || !ObserverPawn || !Victim || !VictimPC)
	{
		FailSmoke(TEXT("E2E ARMOR PROBE FAIL: fixture pawn/controller disappeared."));
		return false;
	}
	if (ArmorSmokeStage == 1 && Now >= ArmorSmokeDeadline)
	{
		VictimPC->BeginArmorSmokeProbe(1, Victim);
		Observer->BeginArmorSmokeProbe(1, Victim);
		ArmorSmokeStage = 2;
		ArmorSmokeDeadline = Now + 7;
	}
	else if (ArmorSmokeStage == 2 && VictimPC->HasArmorSmokePassed(1) && Observer->HasArmorSmokePassed(1))
	{
		const float Multiplier = UBrDeveloperSettings::Get()->StandingDamageMultiplier;
		if (Victim->GetPosture() != EBrPosture::Standing || !FMath::IsFinite(Multiplier) || Multiplier <= 0)
		{
			FailSmoke(TEXT("E2E ARMOR PROBE FAIL: bullet fixture requires standing damage multiplier > 0."));
			return false;
		}
		const FVector Origin = Victim->GetActorLocation() + FVector(1000, -500, 0);
		const FBrDamageEvent Event(CaptureDamageSource(Host), UBrBulletDamageType::StaticClass(), Origin);
		const float Applied = Victim->TakeDamage(10.f / Multiplier, Event, Host, Source);
		if (!FMath::IsNearlyEqual(Applied, 10.f) || !FMath::IsNearlyEqual(Victim->GetHealth()->Health, ArmorSmokeHealth) ||
			Victim->GetArmor()->GetPlates() != TArray<float>({17}))
		{
			FailSmoke(TEXT("E2E ARMOR PROBE FAIL: bullet absorption/health/most-damaged carry-over mismatch."));
			return false;
		}
		VictimPC->BeginArmorSmokeProbe(2, Victim);
		Observer->BeginArmorSmokeProbe(2, Victim);
		ArmorSmokeStage = 3;
		ArmorSmokeDeadline = Now + 7;
	}
	else if (ArmorSmokeStage == 3 && VictimPC->HasArmorSmokePassed(2) && Observer->HasArmorSmokePassed(2))
	{
		// PASS requires the owner's actual received hit-feedback and break-sound RPC counters too.
		UE_LOG(LogBr, Display, TEXT("E2E ARMOR PROBE PASS Authority=1 Absorbed=10 HealthUnchanged=1 MostDamagedFirst=1 HitFeedback=1 BreakSoundRoute=1 Private=1"));
		Victim->GetArmor()->Clear(); // Isolate every pre-existing exact health-delta assertion.
		Victim->GetPlayerState<ABrPlayerState>()->Team = ArmorSmokeVictimTeam;
		ObserverPawn->GetPlayerState<ABrPlayerState>()->Team = ArmorSmokeObserverTeam;
		Victim->SetActorLocation(ArmorSmokeVictimLocation, false, nullptr, ETeleportType::TeleportPhysics);
		ObserverPawn->SetActorLocation(ArmorSmokeObserverLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Victim->GetBrMovement()->SetMovementMode(static_cast<EMovementMode>(ArmorSmokeVictimMode), ArmorSmokeVictimCustomMode);
		ObserverPawn->GetBrMovement()->SetMovementMode(static_cast<EMovementMode>(ArmorSmokeObserverMode), ArmorSmokeObserverCustomMode);
		Victim->ForceNetUpdate();
		ObserverPawn->ForceNetUpdate();
		ArmorSmokeStage = 4;
		return true;
	}
	else if (Now >= ArmorSmokeDeadline && ArmorSmokeStage >= 2)
		FailSmoke(TEXT("E2E ARMOR PROBE FAIL: bounded remote replication/feedback acknowledgement timed out."));
	return false;
}

void ABrGameMode::ServiceArmorSpectatorSmoke(ABrPlayerController* Host)
{
	if (!HasAuthority() || !FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || MatchSmokeStage != 6) return;
	ABrPlayerController* Viewer = ArmorSmokeVictimController.Get();
	ABrCharacter* Viewed = Host ? Cast<ABrCharacter>(Host->GetPawn()) : nullptr;
	const float Now = GetWorld()->GetTimeSeconds();
	if (ArmorSmokeStage == 4 && Viewer && Viewed)
	{
		// All remote contestants were eliminated by the existing final match fixture.
		Viewer->ServiceSpectator();
		Viewer->BeginArmorSmokeProbe(3, Viewed);
		ArmorSmokeStage = 5;
		ArmorSmokeDeadline = Now + 7;
	}
	else if (ArmorSmokeStage == 5 && Viewer && Viewer->HasArmorSmokePassed(3))
	{
		const TArray<float>* Mirror = Viewed ? Viewer->GetSpectatorArmor(Viewed) : nullptr;
		if (!Mirror || *Mirror != TArray<float>({7, 20}))
		{
			FailSmoke(TEXT("E2E ARMOR PROBE FAIL: authoritative spectator mirror mismatch."));
			return;
		}
		UE_LOG(LogBr, Display, TEXT("E2E ARMOR PROBE PASS Authority=1 Spectator=1 Mirror=1"));
		Viewed->GetArmor()->Clear();
		ArmorSmokeStage = 6;
	}
	else if (ArmorSmokeStage == 5 && Now >= ArmorSmokeDeadline)
		FailSmoke(TEXT("E2E ARMOR PROBE FAIL: bounded spectator mirror acknowledgement timed out."));
}

void ABrPlayerController::ReportMatchSmokeReady(ABrCharacter* ObserverPawn)
{
	if (HasAuthority() || !IsLocalController() || !ObserverPawn || ObserverPawn != GetPawn() ||
		IsMatchSmokeReady() || !FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	MatchSmokeReadyPawn = ObserverPawn;
	ServerMatchSmokeReady(ObserverPawn);
}
#endif

void ABrPlayerController::ServerMatchSmokeReady_Implementation(ABrCharacter* ObserverPawn)
{
#if !UE_BUILD_SHIPPING
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || IsLocalController() ||
		!Mode || Mode->IsLobbyMode() || !IsValid(ObserverPawn) || ObserverPawn != GetPawn() ||
		ObserverPawn->GetWorld() != GetWorld()) return;
	MatchSmokeReadyPawn = ObserverPawn;
	UE_LOG(LogBr, Display, TEXT("E2E MESH OBSERVER READY: %s"), *GetNameSafe(ObserverPawn));
#endif
}

#if !UE_BUILD_SHIPPING
void ABrCharacter::ServiceMeshPoseSmoke(float DeltaSeconds)
{
	// Each machine samples its own mesh, never the authoritative pawn via an RPC.
	// Previous-frame presentation includes movement smoothing; do not reapply it here.
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	if (IsLocallyControlled())
	{
		// Admission precedes client world BeginPlay (sometimes by seconds). Do
		// not let the fixture down/revive entirely before this machine can tick.
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(GetController())) PC->ReportMatchSmokeReady(this);
		return;
	}
	if (bMeshPoseSmokeFailed) return;
	if (GetLife()->IsAlive() && GetPosture() != EBrPosture::Standing)
	{
		if (VisualSmokePosture != GetPosture())
		{
			VisualSmokePosture = GetPosture();
			VisualSmokeSeconds = 0;
		}
		VisualSmokeSeconds += DeltaSeconds;
		bool& Passed = GetPosture() == EBrPosture::Prone ? bVisualPronePassed : bVisualCrouchPassed;
		if (!Passed && VisualSmokeSeconds >= 0.9f)
		{
			const float HeadHeight = GetMesh()->GetBoneLocation(TEXT("head")).Z -
				(GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			const bool Prone = GetPosture() == EBrPosture::Prone;
			if (HeadHeight < 5 || HeadHeight > (Prone ? 45.f : 135.f))
			{
				bMeshPoseSmokeFailed = true;
				UE_LOG(LogBr, Error, TEXT("E2E MESH POSE FAIL: Posture=%d HeadHeight=%.2f Authority=%d"), int32(GetPosture()), HeadHeight, HasAuthority());
				return;
			}
			Passed = true;
			UE_LOG(LogBr, Display, TEXT("E2E MESH POSE %s PASS: Pawn=%s Authority=%d NonOwner=1 HeadHeight=%.2f"),
				Prone ? TEXT("PRONE") : TEXT("CROUCH"), *GetName(), HasAuthority(), HeadHeight);
		}
	}
	else
	{
		VisualSmokePosture = EBrPosture::Standing;
		VisualSmokeSeconds = 0;
	}
	const bool bDowned = GetLife()->State == EBrLifeState::Downed;
	if (!bDowned && (!bMeshPoseSmokeDownPassed || !GetLife()->IsAlive() || bMeshPoseSmokeRevivePassed)) return;
	if (bDowned != bMeshPoseSmokeDowned)
	{
		bMeshPoseSmokeDowned = bDowned;
		MeshPoseSmokeSeconds = 0;
		MeshPoseSmokeSamples = 0;
		if (bDowned) bMeshPoseSmokeDownPassed = bMeshPoseSmokeRevivePassed = false;
	}
	MeshPoseSmokeSeconds += DeltaSeconds;
	++MeshPoseSmokeSamples;
	// Allow a short replication/smoothing settling interval, then check every tick.
	if (MeshPoseSmokeSeconds < 0.2f) return;
	const FQuat ExpectedRotation = (bDowned ? BrSupinePose::Rotation() : FRotator(0, -90, 0)).Quaternion();
	const float Pitch = GetMesh()->GetRelativeRotation().Pitch;
	const float ExpectedZ = -GetCapsuleComponent()->GetScaledCapsuleHalfHeight() +
		(bDowned ? BrSupinePose::GroundLift(GetMesh()->GetSkeletalMeshAsset()) : 0.f);
	const FVector BodyAxis = GetMesh()->GetBoneLocation(TEXT("head")) - GetMesh()->GetBoneLocation(TEXT("pelvis"));
	const bool bSupine = !bDowned || (GetMesh()->GetComponentQuat().RotateVector(FVector::RightVector).Z > 0.98 &&
		FVector::DotProduct(BodyAxis.GetSafeNormal2D(), GetActorForwardVector()) > 0.98 && FMath::Abs(BodyAxis.Z) < 10);
	if (GetMesh()->GetRelativeRotation().Quaternion().AngularDistance(ExpectedRotation) > FMath::DegreesToRadians(2.f) ||
		GetBaseRotationOffset().AngularDistance(ExpectedRotation) > FMath::DegreesToRadians(2.f) ||
		!bSupine ||
		!FMath::IsNearlyEqual(GetBaseTranslationOffset().Z, ExpectedZ, 0.1f))
	{
		bMeshPoseSmokeFailed = true;
		UE_LOG(LogBr, Error, TEXT("E2E MESH POSE FAIL: Pawn=%s Authority=%d Downed=%d Pitch=%.2f BasePitch=%.2f BaseZ=%.2f ExpectedZ=%.2f"),
			*GetName(), HasAuthority(), bDowned, Pitch, GetBaseRotationOffset().Rotator().Pitch,
			GetBaseTranslationOffset().Z, ExpectedZ);
		return;
	}
	if (MeshPoseSmokeSeconds >= 0.8f && MeshPoseSmokeSamples >= 10 &&
		(bDowned ? !bMeshPoseSmokeDownPassed : !bMeshPoseSmokeRevivePassed))
	{
		if (bDowned) bMeshPoseSmokeDownPassed = true;
		else bMeshPoseSmokeRevivePassed = true;
		UE_LOG(LogBr, Display, TEXT("E2E MESH POSE %s PASS: Pawn=%s Authority=%d NonOwner=1 Seconds=%.2f Samples=%d Pitch=%.2f"),
			bDowned ? TEXT("DOWNED") : TEXT("REVIVED"), *GetName(), HasAuthority(),
			MeshPoseSmokeSeconds, MeshPoseSmokeSamples, Pitch);
	}
}
#endif

void ABrGameMode::ServiceMatchSmoke()
{
#if !UE_BUILD_SHIPPING
	if (!HasAuthority() || bMatchSmokeFailed || !FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	UWorld* World = GetWorld();
	ABrGameState* GS = GetGameState<ABrGameState>();
	ABrPlayerController* Host = Cast<ABrPlayerController>(World ? World->GetFirstPlayerController() : nullptr);
	if (!World || !GS || !Host || !Host->IsLocalController()) return;

	if (IsLobbyMode())
	{
		if (MatchSmokeRound == 2)
		{
			UE_LOG(LogBr, Display, TEXT("E2E MATCH SMOKE PASS: two rounds, clean reset, %s"),
				GS->MatchSettings.Mode == EBrMode::Grouped ? TEXT("grouped") : TEXT("all-vs-all"));
			if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>()) GI->bSmokeFinished = true;
			MatchSmokeRound = 3;
			return;
		}
		if (GS->PlayerArray.Num() < 3) return;
		if (MatchSmokeRound == 0)
		{
			MatchSmokeRound = 1;
			UE_LOG(LogBr, Display, TEXT("E2E MATCH SMOKE: starting first round with %d players"), GS->PlayerArray.Num());
			StartRound(Host);
		}
		else if (MatchSmokeRound == 1)
		{
			for (APlayerState* Entry : GS->PlayerArray)
			{
				const ABrPlayerState* PS = Cast<ABrPlayerState>(Entry);
				if (PS && (PS->DeathCount != 0 || PS->VoluntaryRespawnCount != 0 || PS->bEliminated))
				{
					FailSmoke(FString::Printf(TEXT("Round ledger was not reset for %s"), *PS->GetPlayerName()));
					return;
				}
			}
			UE_LOG(LogBr, Display, TEXT("E2E SECOND ROUND CLEAN: deaths, penalties and elimination reset"));
			MatchSmokeRound = 2;
			StartRound(Host);
		}
		return;
	}

	ServiceArmorSpectatorSmoke(Host); // Existing result-phase spectating continues before lobby travel.
	if (GS->bResults) return;
	const float Now = World->GetTimeSeconds();
	if (MatchSmokeStage == 0)
	{
		// A log handshake lets the runner join its third remote player during the
		// live match. Continue on admission, or fail after a bounded deadline.
		if (MatchSmokeDeadline == 0)
		{
			MatchSmokeDeadline = Now + 25.0f;
			UE_LOG(LogBr, Display, TEXT("E2E LATE JOIN WINDOW: active match; waiting for third remote client"));
			return;
		}
		if (GS->PlayerArray.Num() < 4)
		{
			if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Late join did not produce host plus three remote contestants."));
			return;
		}
		for (APlayerState* Entry : GS->PlayerArray)
			if (Entry && Entry->GetPlayerName() == TEXT("MatchLateJoin"))
			{
				const ABrPlayerController* Observer = Cast<ABrPlayerController>(Entry->GetPlayerController());
				if (!Observer || !Observer->IsMatchSmokeReady())
				{
					if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Non-owning mesh observer did not reach client BeginPlay."));
					return;
				}
			}
		for (APlayerState* Entry : GS->PlayerArray)
			if (Entry && Entry->GetPlayerName() == TEXT("MatchLateJoin"))
			{
				ABrPlayerController* Observer = Cast<ABrPlayerController>(Entry->GetPlayerController());
				if (!ServiceHitSmoke(Host, Observer)) return;
				if (!ServiceAudioSmoke(Host, Observer)) return;
				if (!ServiceSprintSmoke(Observer)) return;
				if (!ServiceArmorSmoke(Host, Observer)) return;
				if (!ServiceSoundSmoke(Host, Observer)) return;
			}
		for (APlayerState* Entry : GS->PlayerArray)
		{
			const ABrPlayerState* PS = Cast<ABrPlayerState>(Entry);
			const ABrCharacter* Pawn = PS ? Cast<ABrCharacter>(PS->GetPawn()) : nullptr;
			if (!Pawn || !CheckSpawnClear(World, Pawn) || !CheckAliveMeshPose(Pawn))
			{
				FailSmoke(FString::Printf(TEXT("Missing or obstructed round spawn for %s"), *GetNameSafe(PS)));
				return;
			}
			if (PS->GetPlayerName() == TEXT("MatchLateJoin"))
			{
				double NearestEnemySquared = TNumericLimits<double>::Max();
				for (APlayerState* OtherEntry : GS->PlayerArray)
				{
					const ABrPlayerState* OtherPS = Cast<ABrPlayerState>(OtherEntry);
					const ABrCharacter* OtherPawn = OtherPS ? Cast<ABrCharacter>(OtherPS->GetPawn()) : nullptr;
					if (OtherPawn && OtherPawn != Pawn && IsEnemy(Pawn, OtherPawn))
						NearestEnemySquared = FMath::Min(NearestEnemySquared,
							FVector::DistSquared2D(Pawn->GetActorLocation(), OtherPawn->GetActorLocation()));
				}
				const float RequiredDistance = UBrDeveloperSettings::Get()->MinSpawnDistanceFromEnemies;
				if (NearestEnemySquared < FMath::Square(RequiredDistance))
				{
					FailSmoke(FString::Printf(TEXT("Late join spawn too close to enemy: %.0f cm (required %.0f)"),
						FMath::Sqrt(NearestEnemySquared), RequiredDistance));
					return;
				}
				UE_LOG(LogBr, Display, TEXT("E2E LATE JOIN SPAWN PASS: MinEnemyDistance=%.0f"),
					FMath::Sqrt(NearestEnemySquared));
			}
		}
		UE_LOG(LogBr, Display, TEXT("E2E SPAWNS PASS: Players=%d geometry-clear=1"), GS->PlayerArray.Num());
		if (!ExercisePickups(World, Cast<ABrCharacter>(Host->GetPawn())))
		{
			FailSmoke(TEXT("Weapon/health/grenade pickup rule check failed."));
			return;
		}
		UE_LOG(LogBr, Display, TEXT("E2E PICKUPS PASS: weapon swap, health cap and grenade cap"));
		// Collection was tested above. Isolate subsequent exact gear-retention
		// assertions from random live map pickups at respawn destinations.
		// Each new round still creates/tests its normal pickup population.
		if (PickupManager) PickupManager->ClearRoundPickups();

		ABrCharacter* Shooter = Cast<ABrCharacter>(Host->GetPawn());
		if (!Shooter)
		{
			FailSmoke(TEXT("Host shooter pawn is missing."));
			return;
		}
		ABrCharacter* Target = nullptr;
		for (APlayerState* Entry : GS->PlayerArray)
			if (ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
				if (!PS->bHost && PS->GetPlayerName() != TEXT("MatchLateJoin"))
				{
					ABrCharacter* Candidate = Cast<ABrCharacter>(PS->GetPawn());
					if (Candidate && IsEnemy(Shooter, Candidate)) { Target = Candidate; break; }
				}
		if (!Target || !Shooter->GetInventory()->Weapon)
		{
			FailSmoke(TEXT("Could not resolve shooter, target or weapon."));
			return;
		}

		bool bLineFound = false;
		FVector Destination = Target->GetActorLocation();
		for (int32 I = 0; I < 24 && !bLineFound; ++I)
		{
			const float Angle = 2.0f * UE_PI * I / 24.0f;
			const FVector Offset(FMath::Cos(Angle) * 500.0f, FMath::Sin(Angle) * 500.0f, 0);
			Destination = Shooter->GetActorLocation() + Offset;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(BrMatchSmokeLOS), false, Shooter);
			Query.AddIgnoredActor(Target);
			FHitResult Hit;
			bLineFound = !World->LineTraceSingleByChannel(Hit, Shooter->GetActorLocation(), Destination,
				ECC_Visibility, Query);
		}
		if (!bLineFound)
		{
			FailSmoke(TEXT("No unobstructed short-range shot lane found."));
			return;
		}
		Target->SetActorLocation(Destination, false, nullptr, ETeleportType::TeleportPhysics);
		Target->ForceNetUpdate();
		const FVector Aim = (Destination - Shooter->GetActorLocation()).GetSafeNormal2D();
		Host->SetControlRotation(Aim.Rotation());
		Shooter->SetActorRotation(Aim.Rotation());
		UBrDeveloperSettings* Settings = const_cast<UBrDeveloperSettings*>(UBrDeveloperSettings::Get());
		MatchSmokeTarget = Target;
		MatchSmokeTargetHealth = Target->GetHealth()->Health;
		MatchSmokeExpectedDamage = Shooter->GetInventory()->Weapon->Damage *
			(Target->GetPosture() == EBrPosture::Prone ? Settings->ProneDamageMultiplier :
				Target->GetPosture() == EBrPosture::Crouching ? Settings->CrouchingDamageMultiplier :
				Settings->StandingDamageMultiplier);
		SavedStanding = Settings->StandingHitChance;
		SavedCrouching = Settings->CrouchingHitChance;
		SavedProne = Settings->ProneHitChance;
		bSavedHitChance = true;
		for (FBrHitChanceRow* Row : {&Settings->StandingHitChance, &Settings->CrouchingHitChance, &Settings->ProneHitChance})
			Row->Standing = Row->Crouching = Row->Prone = 100.0f;
		Shooter->GetInventory()->StopFire();
		Shooter->GetInventory()->StartFire(); // Real authoritative bullet path; only hit chance is forced.
		Shooter->GetInventory()->StopFire();  // Keep the test to one deterministic bullet, even for automatic weapons.
		MatchSmokeDeadline = Now + 4.0f;
		MatchSmokeStage = 1;
		return;
	}
	if (MatchSmokeStage == 1)
	{
		ABrCharacter* Target = MatchSmokeTarget.Get();
		if (Target && FMath::IsNearlyEqual(MatchSmokeTargetHealth - Target->GetHealth()->Health,
			MatchSmokeExpectedDamage, 0.1f))
		{
			RestoreHitChance();
			UE_LOG(LogBr, Display, TEXT("E2E SHOT PASS: Damage=%.1f HitChanceForced=100 Target=%s"),
				MatchSmokeExpectedDamage, *GetNameSafe(Target));
			ABrCharacter* Reviver = nullptr;
			for (APlayerState* Entry : GS->PlayerArray)
				if (ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
					if (ABrCharacter* Candidate = Cast<ABrCharacter>(PS->GetPawn()))
						if (Candidate != Target && Candidate->GetLife()->IsAlive() &&
							(GS->MatchSettings.Mode != EBrMode::Grouped || AreTeammates(Candidate, Target)))
						{
							Reviver = Candidate;
							break;
						}
			if (!Reviver)
			{
				FailSmoke(TEXT("Could not find a living teammate/helper for revive."));
				return;
			}
			if (GS->MatchSettings.Mode == EBrMode::Grouped)
			{
				MatchSmokeOriginalReviveRule = GS->MatchSettings.ReviveRule;
				GS->MatchSettings.ReviveRule = EBrReviveRule::TeammatesOnly;
				bMatchSmokeReviveRuleChanged = true;
			}
			MatchSmokeReviver = Reviver;
			Reviver->SetActorLocation(Target->GetActorLocation() + FVector(100, 0, 0),
				false, nullptr, ETeleportType::TeleportPhysics);
			// Exercise both low postures, then a late crouch callback while downed.
			Target->GetBrMovement()->RequestPosture(
				MatchSmokeRound == 1 ? EBrPosture::Crouching : EBrPosture::Prone);
			Target->GetBrMovement()->Crouch(false);
			if (!Target->bIsCrouched)
			{
				FailSmoke(TEXT("Could not crouch/prone before down."));
				return;
			}
			Target->GetLife()->Down();
			const float HeightAdjust = Target->GetClass()->GetDefaultObject<ABrCharacter>()->
				GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() -
				Target->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
			Target->OnStartCrouch(HeightAdjust, HeightAdjust * Target->GetCapsuleComponent()->GetShapeScale());
			// The late-join observer was spawned outside enemy fog range. Moving it
			// into this lane now also exercises initial Downed OnRep on relevancy entry.
			for (APlayerState* Entry : GS->PlayerArray)
				if (Entry && Entry->GetPlayerName() == TEXT("MatchLateJoin"))
					if (ABrCharacter* Observer = Cast<ABrCharacter>(Entry->GetPawn()))
					{
						Observer->SetActorLocation(Reviver->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
						Observer->ForceNetUpdate();
					}
			Reviver->GetLife()->StartInteract();
			MatchSmokeDeadline = Now + 5.0f;
			MatchSmokeStage = 2;
			return;
		}
		if (Target && Target->GetHealth()->Health < MatchSmokeTargetHealth &&
			!FMath::IsNearlyEqual(MatchSmokeTargetHealth - Target->GetHealth()->Health, MatchSmokeExpectedDamage, 0.1f))
		{
			FailSmoke(FString::Printf(TEXT("Shot damage mismatch: expected %.1f, actual %.1f"),
				MatchSmokeExpectedDamage, MatchSmokeTargetHealth - Target->GetHealth()->Health));
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Real server-side shot did not reduce target health within four seconds."));
		return;
	}
	if (MatchSmokeStage == 2)
	{
		ABrCharacter* Target = MatchSmokeTarget.Get();
		ABrCharacter* Reviver = MatchSmokeReviver.Get();
		if (Target && Target->GetLife()->State == EBrLifeState::Downed)
		{
			// Exercise OnEndCrouch after Down, including replicated posture updates.
			Target->GetBrMovement()->RequestPosture(EBrPosture::Standing);
			Target->GetBrMovement()->UnCrouch(false);
			Target->ForceNetUpdate();
		}
		if (Target && Reviver && Target->GetLife()->IsAlive())
		{
			MatchSmokeRevivedHealth = Target->GetHealth()->Health;
			const float Expected = Target->GetHealth()->MaxHealth *
				FMath::Clamp(UBrDeveloperSettings::Get()->RevivedHealthFraction, 0.01f, 1.0f);
			if (!FMath::IsNearlyEqual(MatchSmokeRevivedHealth, Expected, 0.1f))
			{
				FailSmoke(FString::Printf(TEXT("Revive health mismatch: expected %.1f, actual %.1f"), Expected, MatchSmokeRevivedHealth));
				return;
			}
			UE_LOG(LogBr, Display, TEXT("E2E REVIVE PASS: Health=%.1f TeammatesOnly=%d"),
				MatchSmokeRevivedHealth, GS->MatchSettings.Mode == EBrMode::Grouped);
			if (bMatchSmokeReviveRuleChanged)
			{
				GS->MatchSettings.ReviveRule = MatchSmokeOriginalReviveRule;
				bMatchSmokeReviveRuleChanged = false;
			}
			// Keep Alive replicated long enough for non-owning machines to sample
			// the restored pose over multiple smoothing ticks before the next down.
			MatchSmokeDeadline = Now + 1.5f;
			MatchSmokeStage = 20;
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Teammate revive did not complete before timeout."));
		return;
	}
	if (MatchSmokeStage == 20)
	{
		if (Now < MatchSmokeDeadline) return;
		ABrCharacter* Target = MatchSmokeTarget.Get();
		if (Target && Target->GetLife()->IsAlive())
		{
			Target->GetBrMovement()->RequestPosture(EBrPosture::Crouching);
			CastChecked<ABrPlayerController>(Target->GetController())->ClientPostureSmoke(EBrPosture::Crouching);
			Target->GetBrMovement()->Crouch(false);
			Target->ForceNetUpdate();
			MatchSmokeStage = 21;
			MatchSmokeDeadline = Now + 1.8f;
			return;
		}
		FailSmoke(TEXT("Revived pawn disappeared during mesh observation."));
		return;
	}
	if (MatchSmokeStage == 21 || MatchSmokeStage == 22 || MatchSmokeStage == 23)
	{
		if (Now < MatchSmokeDeadline) return;
		ABrCharacter* Target = MatchSmokeTarget.Get();
		if (Target && Target->GetLife()->IsAlive())
		{
			if (!CheckAliveMeshPose(Target))
			{
				FailSmoke(TEXT("Post-revive crouch/prone mesh offset mismatch."));
				return;
			}
			if (MatchSmokeStage < 23)
			{
				const EBrPosture Posture = MatchSmokeStage == 21 ? EBrPosture::Prone : EBrPosture::Standing;
				Target->GetBrMovement()->RequestPosture(Posture);
				CastChecked<ABrPlayerController>(Target->GetController())->ClientPostureSmoke(Posture);
				if (Posture == EBrPosture::Standing) Target->GetBrMovement()->UnCrouch(false);
				else Target->GetBrMovement()->Crouch(false);
				Target->ForceNetUpdate();
				++MatchSmokeStage;
				MatchSmokeDeadline = Now + 1.8f;
				return;
			}
			MatchSmokeTargetController = Target->GetController();
			MatchSmokeGearWeapon = Target->GetInventory()->Weapon;
			MatchSmokeGearLoaded = Target->GetInventory()->Loaded;
			MatchSmokeGearReserve = Target->GetInventory()->GetReserve();
			MatchSmokeGearGrenades = Target->GetInventory()->Grenades;
			Target->GetLife()->Down();
			Target->GetLife()->StartRespawnHold();
			MatchSmokeDeadline = Now + 5.0f;
			MatchSmokeStage = 3;
			return;
		}
		FailSmoke(TEXT("Revived pawn disappeared during mesh observation."));
		return;
	}
	if (MatchSmokeStage == 3)
	{
		AController* Controller = MatchSmokeTargetController.Get();
		ABrCharacter* OldTarget = MatchSmokeTarget.Get();
		ABrCharacter* Respawned = Controller ? Cast<ABrCharacter>(Controller->GetPawn()) : nullptr;
		const ABrPlayerState* PS = Controller ? Controller->GetPlayerState<ABrPlayerState>() : nullptr;
		if (Respawned && Respawned != OldTarget && Respawned->GetLife()->IsAlive() && PS)
		{
			const bool bGearKept = Respawned->GetInventory()->Weapon == MatchSmokeGearWeapon.Get() &&
				Respawned->GetInventory()->Loaded == MatchSmokeGearLoaded &&
				Respawned->GetInventory()->GetReserve() == MatchSmokeGearReserve &&
				Respawned->GetInventory()->Grenades == MatchSmokeGearGrenades;
			const float ExpectedMax = UBrDeveloperSettings::Get()->BaseMaxHealth *
				(1.0f - UBrDeveloperSettings::Get()->MaxHealthPenaltyPerRespawn);
			if (!CheckAliveMeshPose(Respawned) || PS->VoluntaryRespawnCount != 1 ||
				!FMath::IsNearlyEqual(Respawned->GetHealth()->MaxHealth, ExpectedMax, 0.1f) ||
				!bGearKept || !Respawned->GetLife()->IsInvulnerable())
			{
				FailSmoke(TEXT("Voluntary respawn penalty, gear retention or invulnerability assertion failed."));
				return;
			}
			UE_LOG(LogBr, Display, TEXT("E2E RESPAWN PASS: MaxHealth=%.1f GearKept=1 Invulnerable=1"),
				Respawned->GetHealth()->MaxHealth);
			ABrCharacter* Shooter = Cast<ABrCharacter>(Host->GetPawn());
			ABrCharacter* Near = nullptr;
			ABrCharacter* Outside = nullptr;
			for (APlayerState* Entry : GS->PlayerArray)
				if (ABrPlayerState* OtherPS = Cast<ABrPlayerState>(Entry))
					if (!OtherPS->bHost)
						if (ABrCharacter* Candidate = Cast<ABrCharacter>(OtherPS->GetPawn()))
							if (Candidate != Respawned)
							{
								if (!Near) Near = Candidate;
								else if (!Outside) Outside = Candidate;
							}
			if (!Shooter || !Near || !Outside)
			{
				FailSmoke(TEXT("Could not establish near/outside grenade damage targets."));
				return;
			}
			bool bClearLane = false;
			FVector Aim = FVector::ForwardVector;
			for (int32 I = 0; I < 24 && !bClearLane; ++I)
			{
				const float Angle = 2.0f * UE_PI * I / 24.0f;
				Aim = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0);
				FCollisionQueryParams Query(SCENE_QUERY_STAT(BrMatchSmokeGrenadeLane), false);
				for (APlayerState* OtherEntry : GS->PlayerArray)
					if (OtherEntry && OtherEntry->GetPawn()) Query.AddIgnoredActor(OtherEntry->GetPawn());
				FHitResult Hit;
				bClearLane = !World->LineTraceSingleByChannel(Hit, Shooter->GetActorLocation(),
					Shooter->GetActorLocation() + Aim * 1000.0f, ECC_Visibility, Query);
			}
			if (!bClearLane)
			{
				FailSmoke(TEXT("No unobstructed grenade test lane found."));
				return;
			}
			Near->SetActorLocation(Shooter->GetActorLocation() + Aim * 300.0f,
				false, nullptr, ETeleportType::TeleportPhysics);
			Outside->SetActorLocation(Shooter->GetActorLocation() + Aim * 650.0f,
				false, nullptr, ETeleportType::TeleportPhysics);
			Near->GetHealth()->SetHealth(Near->GetHealth()->MaxHealth);
			Outside->GetHealth()->SetHealth(Outside->GetHealth()->MaxHealth);
			if (GS->MatchSettings.Mode == EBrMode::Grouped)
			{
				MatchSmokeOriginalFriendlyFire = GS->MatchSettings.bFriendlyFire;
				GS->MatchSettings.bFriendlyFire = true;
				bMatchSmokeFriendlyFireChanged = true;
			}
			const UBrThrowableDefinition* Grenade = UBrDeveloperSettings::Get()->HandGrenade.LoadSynchronous();
			if (!Grenade || Shooter->GetInventory()->Grenades <= 0)
			{
				FailSmoke(TEXT("Hand grenade definition or inventory is missing."));
				return;
			}
			Host->SetControlRotation(Aim.Rotation());
			Shooter->SetActorRotation(Aim.Rotation());
			MatchSmokeGrenadeTarget = Near;
			MatchSmokeGrenadeOutside = Outside;
			MatchSmokeGrenadeTargetHealth = Near->GetHealth()->Health;
			MatchSmokeGrenadeOutsideHealth = Outside->GetHealth()->Health;
			MatchSmokeExpectedGrenadeDamage = Grenade->Damage * UBrDeveloperSettings::Get()->StandingDamageMultiplier;
			const int32 GrenadesBefore = Shooter->GetInventory()->Grenades;
			Shooter->GetInventory()->RequestGrenade();
			if (Shooter->GetInventory()->Grenades != GrenadesBefore - 1)
			{
				FailSmoke(TEXT("Real grenade throw did not consume exactly one grenade."));
				return;
			}
			MatchSmokeDeadline = Now + 4.0f;
			MatchSmokeStage = 4;
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Voluntary respawn did not complete before timeout."));
		return;
	}
	if (MatchSmokeStage == 4)
	{
		ABrCharacter* Near = MatchSmokeGrenadeTarget.Get();
		ABrCharacter* Outside = MatchSmokeGrenadeOutside.Get();
		if (Near && Outside && FMath::IsNearlyEqual(MatchSmokeGrenadeTargetHealth - Near->GetHealth()->Health,
			MatchSmokeExpectedGrenadeDamage, 0.1f))
		{
			if (!FMath::IsNearlyEqual(MatchSmokeGrenadeOutsideHealth, Outside->GetHealth()->Health, 0.1f))
			{
				FailSmoke(TEXT("Grenade damaged a player outside the configured blast radius."));
				return;
			}
			RestoreFriendlyFire(GS, bMatchSmokeFriendlyFireChanged, MatchSmokeOriginalFriendlyFire);
			UE_LOG(LogBr, Display, TEXT("E2E GRENADE PASS: Impact=1 Damage=%.1f OutsideRadiusUnaffected=1"),
				MatchSmokeExpectedGrenadeDamage);
			AController* TimeoutController = Outside->GetController();
			ABrPlayerState* TimeoutPS = TimeoutController ? TimeoutController->GetPlayerState<ABrPlayerState>() : nullptr;
			if (!TimeoutController || !TimeoutPS)
			{
				FailSmoke(TEXT("Could not resolve timeout-respawn fixture controller."));
				return;
			}
			MatchSmokeTarget = Outside;
			MatchSmokeTargetController = TimeoutController;
			MatchSmokeExpectedMaxHealth = Outside->GetHealth()->MaxHealth;
			MatchSmokeGearWeapon = Outside->GetInventory()->Weapon;
			MatchSmokeGearLoaded = Outside->GetInventory()->Loaded;
			MatchSmokeGearReserve = Outside->GetInventory()->GetReserve();
			MatchSmokeGearGrenades = Outside->GetInventory()->Grenades;
			MatchSmokeTimeoutDeaths = TimeoutPS->DeathCount;
			Outside->GetLife()->Down(); // No hold input: this must resolve by the configured timeout.
			MatchSmokeDeadline = Now + UBrDeveloperSettings::Get()->DownedDuration + 5.0f;
			MatchSmokeStage = 5;
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Thrown grenade did not impact and apply configured damage before timeout."));
		return;
	}
	if (MatchSmokeStage == 5)
	{
		AController* Controller = MatchSmokeTargetController.Get();
		ABrCharacter* OldTarget = MatchSmokeTarget.Get();
		ABrCharacter* Respawned = Controller ? Cast<ABrCharacter>(Controller->GetPawn()) : nullptr;
		const ABrPlayerState* PS = Controller ? Controller->GetPlayerState<ABrPlayerState>() : nullptr;
		if (Respawned && Respawned != OldTarget && Respawned->GetLife()->IsAlive() && PS)
		{
			const bool bGearKept = Respawned->GetInventory()->Weapon == MatchSmokeGearWeapon.Get() &&
				Respawned->GetInventory()->Loaded == MatchSmokeGearLoaded &&
				Respawned->GetInventory()->GetReserve() == MatchSmokeGearReserve &&
				Respawned->GetInventory()->Grenades == MatchSmokeGearGrenades;
			const int32 ExpectedDeaths = MatchSmokeTimeoutDeaths +
				(UBrDeveloperSettings::Get()->bTimeoutRespawnCountsAsDeath ? 1 : 0);
			if (!CheckAliveMeshPose(Respawned) ||
				!FMath::IsNearlyEqual(Respawned->GetHealth()->MaxHealth, MatchSmokeExpectedMaxHealth, 0.1f) ||
				!bGearKept || PS->DeathCount != ExpectedDeaths || !Respawned->GetLife()->IsInvulnerable())
			{
				FailSmoke(TEXT("Timeout respawn penalty, death accounting, gear retention or protection assertion failed."));
				return;
			}
			UE_LOG(LogBr, Display, TEXT("E2E TIMEOUT RESPAWN PASS: MaxHealth=%.1f Deaths=%d GearKept=1 Invulnerable=1"),
				Respawned->GetHealth()->MaxHealth, PS->DeathCount);
			ABrCharacter* Viewed = Cast<ABrCharacter>(Host->GetPawn());
			if (!Viewed)
			{
				FailSmoke(TEXT("E2E ARMOR PROBE FAIL: spectator fixture has no host pawn."));
				return;
			}
			Viewed->GetArmor()->Clear();
			const bool bFirst = Viewed->GetArmor()->AddPlate();
			Viewed->GetArmor()->Absorb(13, false);
			const bool bSecond = Viewed->GetArmor()->AddPlate();
			if (!bFirst || !bSecond || Viewed->GetArmor()->GetPlates() != TArray<float>({7, 20}))
			{
				FailSmoke(TEXT("E2E ARMOR PROBE FAIL: spectator fixture grant failed."));
				return;
			}
			for (APlayerState* Entry : GS->PlayerArray)
				if (ABrPlayerState* OtherPS = Cast<ABrPlayerState>(Entry))
					if (!OtherPS->bHost)
						if (ABrCharacter* Pawn = Cast<ABrCharacter>(OtherPS->GetPawn())) Pawn->GetLife()->Eliminate();
			MatchSmokeStage = 6;
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Downed timeout did not respawn the player before the bounded deadline."));
		return;
	}
#endif
}
