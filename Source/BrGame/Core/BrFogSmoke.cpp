#include "BrGameMode.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrCharacter.h"
#include "BrGameState.h"
#include "BrDeveloperSettings.h"
#include "BrVisibilitySubsystem.h"
#include "BrDevCheatPolicy.h"
#include "BrLog.h"
#include "BrLifeComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/ActorChannel.h"
#include "Engine/PackageMapClient.h"

void ABrGameMode::ServiceFogSmoke()
{
#if !UE_BUILD_SHIPPING
	ABrPlayerController* Host = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!Host || !FBrDevCheatPolicy::CanExecute(HasAuthority(), Host->IsLocalController(), GetNetMode())) return;
	ABrGameState* GS = GetGameState<ABrGameState>();
	ABrCharacter* HostPawn = Cast<ABrCharacter>(Host->GetPawn());
	const bool bGrouped = GS->MatchSettings.Mode == EBrMode::Grouped;
	ABrPlayerController* Remote = nullptr;
	for (APlayerState* Entry : GS->PlayerArray)
		if (ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
			if (!PS->bHost && (!bGrouped || UBrVisibilitySubsystem::Friendly(PS, Host->GetPlayerState<ABrPlayerState>())))
			{ Remote = Cast<ABrPlayerController>(PS->GetOwner()); break; }
	ABrCharacter* RemotePawn = Remote ? Cast<ABrCharacter>(Remote->GetPawn()) : nullptr;
	if (!HostPawn || !RemotePawn) return;
	const float Elapsed = GetWorld()->GetTimeSeconds() - MatchBeganTime;
	if (Elapsed < 3 + FogSmokeStage * 4 || FogSmokeStage >= 6) return;
	if (FogSmokeStage == 0)
	{
		// Freeze only this development smoke's pawns: positions must not drift under gravity.
		HostPawn->GetCharacterMovement()->DisableMovement();
		RemotePawn->GetCharacterMovement()->DisableMovement();
		HostPawn->GetCharacterMovement()->SetComponentTickEnabled(false);
	}

	if (FogSmokeStage % 2 == 0)
	{
		const bool bNear = FogSmokeStage == 2;
		const FVector RemoteLocation = RemotePawn->GetActorLocation();
		FVector Destination = RemoteLocation + FVector(UBrDeveloperSettings::Get()->VisionRadius * 3, 0, 0);
		if (bNear)
		{
			bool bFound = false;
			for (int32 I = 0; I < 16; ++I)
			{
				const double Angle = I * 2 * UE_PI / 16;
				const FVector Candidate = RemoteLocation + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 200;
				if (UBrVisibilitySubsystem::LOS(GetWorld(), RemotePawn, Candidate, HostPawn))
				{
					Destination = Candidate;
					bFound = true;
					break;
				}
			}
			if (!bFound) { UE_LOG(LogBr, Error, TEXT("Fog smoke could not find clear LOS near client.")); return; }
		}
		Host->ServerBrCheat(TEXT("Teleport"), FName(*FString::SanitizeFloat(Destination.X)), Destination.Y);
		UE_LOG(LogBr, Display, TEXT("Fog smoke positioned: Stage=%d Near=%d Distance=%.0f Grouped=%d"),
			FogSmokeStage, bNear, FVector::Dist2D(HostPawn->GetActorLocation(), RemoteLocation), bGrouped);
	}
	else Remote->ClientFogProbe(FogSmokeStage, bGrouped || FogSmokeStage == 3);
	++FogSmokeStage;
#endif
}

void ABrGameMode::ServiceFogSpectatorSmoke()
{
#if !UE_BUILD_SHIPPING
	ABrPlayerController* Host = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!Host || !FBrDevCheatPolicy::CanExecute(HasAuthority(), Host->IsLocalController(), GetNetMode())) return;
	ABrGameState* GS = GetGameState<ABrGameState>();
	ABrPlayerController* Spectator = nullptr;
	ABrPlayerController* Enemy = nullptr;
	for (APlayerState* Entry : GS->PlayerArray)
	{
		if (Entry->GetPlayerName() == TEXT("FogSpectator")) Spectator = Cast<ABrPlayerController>(Entry->GetOwner());
		if (Entry->GetPlayerName() == TEXT("FogEnemy")) Enemy = Cast<ABrPlayerController>(Entry->GetOwner());
	}
	if (!Spectator || !Enemy || !Host->GetPawn() || !Enemy->GetPawn()) return;
	const float Elapsed = GetWorld()->GetTimeSeconds() - MatchBeganTime;
	if (Elapsed < 3 + FogSmokeStage * 4 || FogSmokeStage > 8) return;
	if (GS->bResults || !UBrVisibilitySubsystem::Active(GetWorld()))
	{
		UE_LOG(LogBr, Error, TEXT("FOG SPECTATOR FAIL: fog inactive/results before completion."));
		return;
	}
	if (FogSmokeStage == 0)
	{
		ABrCharacter* Pawn = Cast<ABrCharacter>(Spectator->GetPawn());
		if (!Pawn) return;
		const FVector Anchor = Host->GetPawn()->GetActorLocation();
		const float Separation = UBrDeveloperSettings::Get()->VisionRadius * 3;
		int32 Index = 0;
		for (ABrPlayerController* PC : {Host, Enemy, Spectator})
		{
			ABrCharacter* Character = CastChecked<ABrCharacter>(PC->GetPawn());
			Character->GetCharacterMovement()->DisableMovement();
			Character->GetCharacterMovement()->SetComponentTickEnabled(false);
			Character->SetActorLocation(Anchor + FVector(Index++ * Separation, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
			Character->ForceNetUpdate();
		}
		UE_LOG(LogBr, Display, TEXT("FOG SPECTATOR fixture: three opposing contestants separated, Results=0"));
	}
	else if (FogSmokeStage == 1)
	{
		Spectator->ClientFogSpectatorProbe(0, INDEX_NONE, Host->PlayerState->GetPlayerId(), TEXT("preflight"));
	}
	else if (FogSmokeStage == 2)
	{
		CastChecked<ABrCharacter>(Spectator->GetPawn())->GetLife()->Eliminate();
		Spectator->ServiceSpectator();
	}
	else
	{
		ABrPlayerController* Expected = FogSmokeStage % 2 ? Host : Enemy;
		ABrPlayerController* Withheld = FogSmokeStage % 2 ? Enemy : Host;
		if (Spectator->GetFlowSpectatorPawn() != Expected->GetPawn())
		{
			UE_LOG(LogBr, Error, TEXT("FOG SPECTATOR FAIL: server target switch Stage=%d"), FogSmokeStage);
			return;
		}
		UNetDriver* Driver = GetWorld()->GetNetDriver();
		const FString Guid = Driver->GetNetGuidCache()->GetNetGUID(Expected->GetPawn()).ToString();
		UActorChannel* Channel = Spectator->GetNetConnection()->FindActorChannelRef(Expected->GetPawn());
		UE_LOG(LogBr, Display, TEXT("FOG SPECTATOR route Stage=%d Target=%s NetGUID=%s Channel=%d Closing=%d Results=0"),
			FogSmokeStage, *Expected->GetPawn()->GetName(), *Guid, Channel ? Channel->ChIndex : -1, Channel && Channel->Closing);
		Spectator->ClientFogSpectatorProbe(FogSmokeStage, Expected->PlayerState->GetPlayerId(), Withheld->PlayerState->GetPlayerId(), Guid);
	}
	++FogSmokeStage;
#endif
}
