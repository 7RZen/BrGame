#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrCharacter.h"
#include "BrHitFeedbackComponent.h"
#include "BrLifeComponent.h"
#include "BrHealthComponent.h"
#include "BrCharacterMovementComponent.h"
#include "BrCombatDefinitions.h"
#include "BrVisibilitySubsystem.h"
#include "BrFogComponent.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void ABrPlayerController::ClientHitSmokeProbe_Implementation(int32 Stage, bool bVictim, FVector_NetQuantize Origin)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")))
	{
		return;
	}
	uint32 Count = 0;
	ABrCharacter* OwnCharacter = Cast<ABrCharacter>(GetPawn());
	if (bVictim && OwnCharacter)
	{
		Count = OwnCharacter->GetHitFeedback()->OwnerEvents;
	}
	else
	{
		for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
		{
			Count += It->GetHitFeedback()->RemoteEvents;
		}
	}
	if (Stage == 0)
	{
		HitSmokeBaseline = Count;
		return;
	}
	const uint32 Expected = bVictim ? Stage : (Stage == 1 ? 0 : 1);
	bool bGood = Count - HitSmokeBaseline == Expected;
	if (bVictim)
	{
		const TArray<FBrHitIndicator>* Indicators = OwnCharacter
			                                            ? &OwnCharacter->GetHitFeedback()->GetIndicators()
			                                            : nullptr;
		bGood &= Indicators && Indicators->Num() == 1 && (*Indicators)[0].Origin.Equals(Origin, 2);
	}
	if (!bGood)
	{
		UE_LOG(LogBr, Error, TEXT("E2E HIT PROBE FAIL Stage=%d Victim=%d Count=%u Baseline=%u Expected=%u"), Stage,
		       bVictim, Count, HitSmokeBaseline, Expected);
	}
	else
	{
		UE_LOG(LogBr, Display, TEXT("E2E HIT PROBE PASS Stage=%d Victim=%d Events=%u Origin=%s"), Stage, bVictim,
		       Expected, *FVector(Origin).ToString());
	}
#endif
}

#if !UE_BUILD_SHIPPING
bool ABrGameMode::ServiceHitSmoke(ABrPlayerController* Host, ABrPlayerController* Observer)
{
	if (HitSmokeStage == 5)
	{
		return true;
	}
	ABrCharacter* ObserverPawn = Observer ? Cast<ABrCharacter>(Observer->GetPawn()) : nullptr;
	ABrCharacter* Source = Host ? Cast<ABrCharacter>(Host->GetPawn()) : nullptr;
	UBrVisibilitySubsystem* Visibility = GetWorld()->GetSubsystem<UBrVisibilitySubsystem>();
	if (!ObserverPawn || !Source || !Visibility)
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (HitSmokeStage == 0)
	{
		for (APlayerState* Entry : GetGameState<ABrGameState>()->PlayerArray)
		{
			if (Entry && Entry->GetOwner() != Host && Entry->GetOwner() != Observer)
			{
				if (ABrCharacter* C = Cast<ABrCharacter>(Entry->GetPawn()))
				{
					HitSmokeVictim = C;
					break;
				}
			}
		}
		ABrCharacter* Victim = HitSmokeVictim.Get();
		if (!Victim)
		{
			return false;
		}
		HitSmokeVictimLocation = Victim->GetActorLocation();
		HitSmokeObserverLocation = ObserverPawn->GetActorLocation();
		HitSmokeVictimTeam = Victim->GetPlayerState<ABrPlayerState>()->Team;
		HitSmokeObserverTeam = ObserverPawn->GetPlayerState<ABrPlayerState>()->Team;
		HitSmokeHealth = Victim->GetHealth()->Health;
		HitSmokeVictimMode = static_cast<uint8>(Victim->GetBrMovement()->MovementMode);
		HitSmokeObserverMode = static_cast<uint8>(ObserverPawn->GetBrMovement()->MovementMode);
		Victim->GetBrMovement()->DisableMovement();
		ObserverPawn->GetBrMovement()->DisableMovement();
		Victim->GetPlayerState<ABrPlayerState>()->Team = 10001;
		ObserverPawn->GetPlayerState<ABrPlayerState>()->Team = 10002;
		Victim->SetActorLocation(FVector(-6000, -6000, 100), false, nullptr, ETeleportType::TeleportPhysics);
		ObserverPawn->SetActorLocation(FVector(6000, 6000, 100), false, nullptr, ETeleportType::TeleportPhysics);
		Victim->ForceNetUpdate();
		ObserverPawn->ForceNetUpdate();
		// Native authoritative event below represents a captured bullet origin, not current shooter transform.
		HitSmokeOrigin = Victim->GetActorLocation() + FVector(1000, -500, 0);
		CastChecked<ABrPlayerController>(Victim->GetController())->ClientHitSmokeProbe(0, true, HitSmokeOrigin);
		Observer->ClientHitSmokeProbe(0, false, HitSmokeOrigin);
		HitSmokeStage = 1;
		HitSmokeDeadline = Now + 2;
		return false;
	}
	if (Now < HitSmokeDeadline)
	{
		return false;
	}
	ABrCharacter* Victim = HitSmokeVictim.Get();
	if (!Victim)
	{
		return false;
	}
	ABrPlayerController* VictimPC = CastChecked<ABrPlayerController>(Victim->GetController());
	if (HitSmokeStage == 1 || HitSmokeStage == 3)
	{
		const bool bVisible = Visibility->Relevant(Victim, Observer);
		if (bVisible != (HitSmokeStage == 3))
		{
			UE_LOG(LogBr, Error, TEXT("E2E HIT PROBE FAIL: server visibility Stage=%d Visible=%d"), HitSmokeStage,
			       bVisible);
			return false;
		}
		const FBrDamageEvent Event(CaptureDamageSource(Host), UBrBulletDamageType::StaticClass(), HitSmokeOrigin);
		const float Before = Victim->GetHealth()->Health;
		Victim->TakeDamage(5, Event, Host, Source);
		if (Victim->GetHealth()->Health >= Before)
		{
			UE_LOG(LogBr, Error, TEXT("E2E HIT PROBE FAIL: no actual damage"));
			return false;
		}
		++HitSmokeStage;
		HitSmokeDeadline = Now + 0.6f;
	}
	else if (HitSmokeStage == 2)
	{
		VictimPC->ClientHitSmokeProbe(1, true, HitSmokeOrigin);
		Observer->ClientHitSmokeProbe(1, false, HitSmokeOrigin);
		ObserverPawn->SetActorLocation(Victim->GetActorLocation() + FVector(200, 0, 0), false, nullptr,
		                               ETeleportType::TeleportPhysics);
		ObserverPawn->ForceNetUpdate();
		HitSmokeStage = 3;
		HitSmokeDeadline = Now + 2;
	}
	else if (HitSmokeStage == 4)
	{
		VictimPC->ClientHitSmokeProbe(2, true, HitSmokeOrigin);
		Observer->ClientHitSmokeProbe(2, false, HitSmokeOrigin);
		Victim->GetHealth()->SetHealth(HitSmokeHealth);
		Victim->GetPlayerState<ABrPlayerState>()->Team = HitSmokeVictimTeam;
		ObserverPawn->GetPlayerState<ABrPlayerState>()->Team = HitSmokeObserverTeam;
		Victim->SetActorLocation(HitSmokeVictimLocation, false, nullptr, ETeleportType::TeleportPhysics);
		ObserverPawn->SetActorLocation(HitSmokeObserverLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Victim->GetBrMovement()->SetMovementMode(static_cast<EMovementMode>(HitSmokeVictimMode));
		ObserverPawn->GetBrMovement()->SetMovementMode(static_cast<EMovementMode>(HitSmokeObserverMode));
		Victim->ForceNetUpdate();
		ObserverPawn->ForceNetUpdate();
		HitSmokeStage = 5;
		UE_LOG(LogBr, Display, TEXT("E2E HIT FIXTURE COMPLETE: anonymous owner origin; hidden then visible observer"));
	}
	return false;
}
#endif
