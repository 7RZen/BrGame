#include "BrGameMode.h"
#include "BrAudioSubsystem.h"
#include "BrCharacter.h"
#include "BrLifeComponent.h"
#include "BrCharacterMovementComponent.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrDeveloperSettings.h"
#include "BrInventoryComponent.h"
#include "BrCombatDefinitions.h"
#include "BrVisibilitySubsystem.h"
#include "BrLog.h"
#include "Engine/World.h"

#if !UE_BUILD_SHIPPING
bool ABrGameMode::ServiceAudioSmoke(ABrPlayerController* Host, ABrPlayerController* Listener)
{
	if (AudioSmokeStage == 7) return true;
	if (!Host || !Listener) return false;
	ABrCharacter* Source = Cast<ABrCharacter>(Host->GetPawn());
	ABrCharacter* Receiver = Cast<ABrCharacter>(Listener->GetPawn());
	UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>();
	UBrVisibilitySubsystem* Fog = GetWorld()->GetSubsystem<UBrVisibilitySubsystem>();
	UBrDeveloperSettings* S = const_cast<UBrDeveloperSettings*>(UBrDeveloperSettings::Get());
	if (!Source || !Receiver || !Audio || !Fog || !Source->GetInventory()->Weapon) return false;
	const float Now = GetWorld()->GetTimeSeconds();
	if (AudioSmokeStage == 0)
	{
		AudioSmokeOriginalVision = S->VisionRadius;
		AudioSmokeOriginalTeam = Listener->GetPlayerState<ABrPlayerState>()->Team;
		AudioSmokeHostLocation = Source->GetActorLocation();
		AudioSmokeListenerLocation = Receiver->GetActorLocation();
		AudioSmokeReceiverMovementMode = static_cast<uint8>(Receiver->GetBrMovement()->MovementMode);
		AudioSmokeReceiverCustomMode = Receiver->GetBrMovement()->CustomMovementMode;
		// Isolate this temporary fixture in grouped mode too. Cache is still the real server LOS cache.
		Listener->GetPlayerState<ABrPlayerState>()->Team = 10000;
		S->VisionRadius = 500;
		Source->GetBrMovement()->SetComponentTickEnabled(false);
		Receiver->GetBrMovement()->SetComponentTickEnabled(false);
		// A network owner's ServerMove can still run physics with component Tick
		// disabled. At edge starts the +30/+60m probe is outside the map and falls,
		// invalidating hearing distance. Freeze movement mode for this fixture too.
		Receiver->GetBrMovement()->DisableMovement();
		Source->GetBrMovement()->Velocity = FVector::ZeroVector;
		Receiver->SetActorLocation(AudioSmokeHostLocation + FVector(3000, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		Receiver->ForceNetUpdate();
		Listener->ClientAudioSmokeProbe(0, AudioSmokeHostLocation);
		AudioSmokeDeadline = Now + 2;
		AudioSmokeStage = 1;
		return false;
	}
	if (Now < AudioSmokeDeadline) return false;
	if (AudioSmokeStage == 1)
	{
		if (Fog->Relevant(Source, Listener))
		{
			UE_LOG(LogBr, Error, TEXT("E2E AUDIO PROBE FAIL: source not fog-hidden"));
			return false;
		}
		// Real firing path: prediction + multicast for visible listeners, one anonymous RPC for this hidden listener.
		UE_LOG(LogBr, Display, TEXT("E2E AUDIO SHOT FIXTURE: Local=%d Menu=%d Blocked=%d Life=%d Loaded=%d Reload=%d Source=%s Receiver=%s"),
			Source->IsLocallyControlled(), Host->IsMenuOpen(), Source->IsGameplayInputBlocked(),
			static_cast<int32>(Source->GetLife()->State), Source->GetInventory()->Loaded,
			Source->GetInventory()->bReloading, *Source->GetActorLocation().ToString(), *Receiver->GetActorLocation().ToString());
		Source->GetInventory()->StartFire();
		Source->GetInventory()->StopFire();
		UE_LOG(LogBr, Display, TEXT("E2E AUDIO SHOT RESULT: Loaded=%d Listener=%s Spectating=%d Rotation=%s"),
			Source->GetInventory()->Loaded, *GetNameSafe(UBrAudioSubsystem::ListenerPawn(Listener)),
			Listener->IsFlowSpectating(), *Source->GetControlRotation().ToString());
		AudioSmokeStage = 2;
	}
	else if (AudioSmokeStage == 2)
	{
		Listener->ClientAudioSmokeProbe(1, AudioSmokeHostLocation);
		Receiver->SetActorLocation(AudioSmokeHostLocation + FVector(6000, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		Receiver->ForceNetUpdate();
		AudioSmokeStage = 3;
	}
	else if (AudioSmokeStage == 3)
	{
		Source->GetInventory()->StartFire();
		Source->GetInventory()->StopFire();
		AudioSmokeStage = 4;
	}
	else if (AudioSmokeStage == 4)
	{
		Listener->ClientAudioSmokeProbe(2, AudioSmokeHostLocation);
		Receiver->SetActorLocation(AudioSmokeHostLocation + FVector(1000, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		Receiver->ForceNetUpdate();
		// Frozen displacement with authoritative velocity stimulates the actual cadence timer deterministically.
		Source->GetBrMovement()->Velocity = FVector(600, 0, 0);
		AudioSmokeStage = 5;
	}
	else if (AudioSmokeStage == 5)
	{
		Source->GetBrMovement()->Velocity = FVector::ZeroVector;
		Source->GetBrMovement()->SetComponentTickEnabled(true);
		Source->GetBrMovement()->RequestPosture(EBrPosture::Prone);
		AudioSmokeStage = 51;
	}
	else if (AudioSmokeStage == 51)
	{
		Listener->ClientAudioSmokeProbe(3, AudioSmokeHostLocation);
		if (Source->GetPosture() != EBrPosture::Prone)
		{
			UE_LOG(LogBr, Error, TEXT("E2E AUDIO PROBE FAIL: fixture did not enter prone"));
			return false;
		}
		Source->GetBrMovement()->SetComponentTickEnabled(false);
		Source->GetBrMovement()->Velocity = FVector(600, 0, 0);
		AudioSmokeStage = 6;
	}
	else if (AudioSmokeStage == 6)
	{
		Listener->ClientAudioSmokeProbe(4, AudioSmokeHostLocation);
		Audio->Emit(Source, EBrSoundKind::Downed, S->DownedScream, AudioSmokeHostLocation);
		AudioSmokeStage = 8;
	}
	else if (AudioSmokeStage == 8)
	{
		Listener->ClientAudioSmokeProbe(5, AudioSmokeHostLocation);
		S->VisionRadius = AudioSmokeOriginalVision;
		Listener->GetPlayerState<ABrPlayerState>()->Team = AudioSmokeOriginalTeam;
		Source->GetBrMovement()->RequestPosture(EBrPosture::Standing);
		Source->GetBrMovement()->UnCrouch(false);
		Source->GetBrMovement()->Velocity = FVector::ZeroVector;
		Source->SetActorLocation(AudioSmokeHostLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Receiver->SetActorLocation(AudioSmokeListenerLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Source->GetBrMovement()->SetComponentTickEnabled(true);
		Receiver->GetBrMovement()->SetComponentTickEnabled(true);
		Receiver->GetBrMovement()->SetMovementMode(static_cast<EMovementMode>(AudioSmokeReceiverMovementMode),
			AudioSmokeReceiverCustomMode);
		Source->ForceNetUpdate();
		Receiver->ForceNetUpdate();
		AudioSmokeStage = 7;
		UE_LOG(LogBr, Display, TEXT("E2E AUDIO ROUTING COMPLETE: real hidden shot, beyond-range shot, timer steps, prone silence, downed event"));
		return true;
	}
	AudioSmokeDeadline = Now + 2;
	return false;
}
#endif
