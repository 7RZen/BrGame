#include "BrLifeComponent.h"
#include "BrAudioSubsystem.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrDeveloperSettings.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrPlayerState.h"
#include "BrPlayerController.h"
#include "BrHealthComponent.h"
#include "BrLog.h"
#include "BrPlayerState.h"
#include "BrPickup.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UBrLifeComponent::UBrLifeComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

ABrCharacter* UBrLifeComponent::Character() const { return CastChecked<ABrCharacter>(GetOwner()); }

void UBrLifeComponent::BeginPlay()
{
	Super::BeginPlay();
	Character()->GetHealth()->OnHealthDepleted.AddDynamic(this, &UBrLifeComponent::HealthDepleted);
	if ((IsInvulnerable() || State == EBrLifeState::Downed) && GetOwner()->HasAuthority())
	{
		EnsureServiceTimer();
	}
	OnRep_State();
}

void UBrLifeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetOwner()->HasAuthority())
	{
		CancelRevive();
	}
	GetWorld()->GetTimerManager().ClearTimer(ServiceTimer);
	Character()->GetHealth()->OnHealthDepleted.RemoveDynamic(this, &UBrLifeComponent::HealthDepleted);
	Super::EndPlay(Reason);
}

void UBrLifeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBrLifeComponent, State);
	DOREPLIFETIME(UBrLifeComponent, DownedEndTime);
	DOREPLIFETIME(UBrLifeComponent, InvulnerabilityEndTime);
	DOREPLIFETIME(UBrLifeComponent, RespawnHoldEndTime);
	DOREPLIFETIME(UBrLifeComponent, Reviver);
	DOREPLIFETIME(UBrLifeComponent, ReviveTarget);
	DOREPLIFETIME(UBrLifeComponent, ReviveStartTime);
	DOREPLIFETIME(UBrLifeComponent, ReviveEndTime);
}

float UBrLifeComponent::ServerTime() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

bool UBrLifeComponent::IsInvulnerable() const { return IsAlive() && ServerTime() < InvulnerabilityEndTime; }

void UBrLifeComponent::EnsureServiceTimer()
{
	if (!GetWorld()->GetTimerManager().IsTimerActive(ServiceTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(ServiceTimer, this, &UBrLifeComponent::Service, 0.05f, true);
	}
}

void UBrLifeComponent::HealthDepleted()
{
	if (GetOwner()->HasAuthority() && IsAlive())
	{
		Down();
	}
}

void UBrLifeComponent::SetPendingDowner(const FString& Key)
{
	if (GetOwner()->HasAuthority()) PendingDownerKey = Key;
}

void UBrLifeComponent::Down()
{
	if (!GetOwner()->HasAuthority() || !IsAlive())
	{
		return;
	}
	if (ABrPlayerController* PC = Cast<ABrPlayerController>(Character()->GetController()))
	{
		PC->CancelUnstuck(TEXT("Unstuck cancelled: downed."));
	}
	CancelRevive();
	GearAtDown = Character()->GetInventory()->CaptureState();
	State = EBrLifeState::Downed;
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
		Mode->CommitQuestDown(Character()->GetController(), PendingDownerKey);
	PendingDownerKey.Reset();
	// Do not replay on initial OnRep_State, relevancy entry or restored downed saves.
	if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
	{
		Audio->Emit(Character(), EBrSoundKind::Downed, UBrDeveloperSettings::Get()->DownedScream,
		            Character()->GetActorLocation());
	}
	InvulnerabilityEndTime = 0;
	DownedEndTime = ServerTime() + FMath::Max(0.1f, UBrDeveloperSettings::Get()->DownedDuration);
	// Set state first to avoid recursive HealthDepleted transitions.
	Character()->GetHealth()->SetHealth(0);
	OnRep_State();
	EnsureServiceTimer();
	GetOwner()->ForceNetUpdate();
	UE_LOG(LogBr, Display, TEXT("Life Downed: %s End=%.2f"), *GetOwner()->GetName(), DownedEndTime);
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
	{
		Mode->RecordPlayer(Character()->GetController());
	}
}

void UBrLifeComponent::RestoreDowned(float Deadline)
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}
	State = EBrLifeState::Downed;
	DownedEndTime = Deadline;
	InvulnerabilityEndTime = RespawnHoldEndTime = 0;
	GearAtDown = Character()->GetInventory()->CaptureState();
	Character()->GetHealth()->SetHealth(0);
	if (HasBegunPlay())
	{
		OnRep_State();
		EnsureServiceTimer();
	}
}

void UBrLifeComponent::OnRep_State()
{
	Character()->ApplyLifePresentation();
	if (bAudioPresentationInitialized && State == EBrLifeState::Downed &&
		AudioPresentedState != EBrLifeState::Downed && Character()->IsLocallyControlled())
	{
		if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
		{
			Audio->Play(EBrSoundKind::Downed, UBrDeveloperSettings::Get()->DownedScream,
			            Character()->GetActorLocation());
		}
	}
	AudioPresentedState = State;
	bAudioPresentationInitialized = HasBegunPlay();
}

bool UBrLifeComponent::CanRevive(const ABrCharacter* Target) const
{
	if (!IsValid(Target) || !Character()->GetController())
	{
		return false;
	}
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	// Client-side prompt is provisional; only the authority calls the future team policy.
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (GS && GS->bResults)
	{
		return false;
	}
	const ABrPlayerState* A = Character()->GetPlayerState<ABrPlayerState>();
	const ABrPlayerState* B = Target->GetPlayerState<ABrPlayerState>();
	const bool bTeammates = Mode
		                        ? Mode->AreTeammates(Character(), Target)
		                        : GS && GS->MatchSettings.Mode == EBrMode::Grouped && A && B && A->Team >= 0 && A->Team
		                        == B->Team;
	return FBrLifeRules::CanRevive(State, Target->GetLife()->State,
	                               FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation()),
	                               Settings->ReviveRadius, GS ? GS->MatchSettings.ReviveRule : Settings->ReviveRule,
	                               bTeammates, Target == GetOwner());
}

ABrCharacter* UBrLifeComponent::FindReviveTarget() const
{
	ABrCharacter* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
	{
		ABrCharacter* Target = *It;
		if (!CanRevive(Target) || ServerTime() >= Target->GetLife()->DownedEndTime ||
			(Target->GetLife()->Reviver && Target->GetLife()->Reviver != GetOwner()))
		{
			continue;
		}
		const double Distance = FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation());
		if (Distance < BestDistance)
		{
			Best = Target;
			BestDistance = Distance;
		}
	}
	return Best;
}

void UBrLifeComponent::StartInteract()
{
	Character()->GetBrMovement()->SetSprinting(false);
	ServerStartRevive();
}

void UBrLifeComponent::StopInteract() { ServerStopRevive(); }
void UBrLifeComponent::StartRespawnHold() { ServerRespawnHold(true); }
void UBrLifeComponent::StopRespawnHold() { ServerRespawnHold(false); }

void UBrLifeComponent::ServerStartRevive_Implementation()
{
	Character()->GetBrMovement()->SetSprinting(false);
	if (!IsAlive() || ReviveTarget)
	{
		return;
	}
	ABrCharacter* Target = FindReviveTarget(); // Intent only; never accept a client-selected target.
	if (!Target)
	{
		if (ABrPickup* Pickup = ABrPickup::FindInteractPickup(Character()))
		{
			Pickup->TryCollect(Character());
		}
		return;
	}
	if (!CanRevive(Target) || ServerTime() >= Target->GetLife()->DownedEndTime)
	{
		return;
	}
	UBrLifeComponent* Other = Target->GetLife();
	if (Other->Reviver || ReviveTarget)
	{
		return; // Never restart a deadline or steal an occupied target.
	}
	const float Now = ServerTime();
	ReviveTarget = Target;
	ReviveStartTime = Other->ReviveStartTime = Now;
	ReviveEndTime = Other->ReviveEndTime = Now + FMath::Max(0.1f, UBrDeveloperSettings::Get()->ReviveDuration);
	Other->Reviver = Character();
	Other->EnsureServiceTimer();
	GetOwner()->ForceNetUpdate();
	Target->ForceNetUpdate();
	UE_LOG(LogBr, Display, TEXT("Revive started: %s -> %s"), *GetOwner()->GetName(), *Target->GetName());
}

void UBrLifeComponent::ServerStopRevive_Implementation() { CancelRevive(); }

void UBrLifeComponent::CancelRevive()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}
	ABrCharacter* OtherCharacter = ReviveTarget ? ReviveTarget.Get() : Reviver.Get();
	if (IsValid(OtherCharacter))
	{
		UBrLifeComponent* Other = OtherCharacter->GetLife();
		if (Other->Reviver == GetOwner() || Other->ReviveTarget == GetOwner())
		{
			Other->Reviver = nullptr;
			Other->ReviveTarget = nullptr;
			Other->ReviveStartTime = Other->ReviveEndTime = 0;
			OtherCharacter->ForceNetUpdate();
		}
	}
	Reviver = ReviveTarget = nullptr;
	ReviveStartTime = ReviveEndTime = 0;
	GetOwner()->ForceNetUpdate();
}

void UBrLifeComponent::DamageReceived()
{
	if (GetOwner()->HasAuthority())
	{
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(Character()->GetController()))
		{
			PC->CancelUnstuck(TEXT("Unstuck cancelled: damage."));
		}
	}
	if (GetOwner()->HasAuthority() && UBrDeveloperSettings::Get()->bCancelReviveOnDamage)
	{
		CancelRevive();
	}
}

void UBrLifeComponent::ServerRespawnHold_Implementation(bool bHeld)
{
	if (!bHeld)
	{
		RespawnHoldEndTime = 0;
		GetOwner()->ForceNetUpdate();
		return;
	}
	const ABrPlayerState* PS = Character()->GetPlayerState<ABrPlayerState>();
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	if (!Mode || Mode->AreRespawnsDisabled())
	{
		return;
	}
	if (State != EBrLifeState::Downed || !PS || RespawnHoldEndTime > 0 ||
		FBrLifeRules::Outcome(true, PS->DeathCount, UBrDeveloperSettings::Get()->MaxDeaths) ==
		EBrDownedOutcome::Refused)
	{
		return;
	}
	RespawnHoldEndTime = ServerTime() + FMath::Max(0.1f, UBrDeveloperSettings::Get()->RespawnHoldTime);
	EnsureServiceTimer();
	GetOwner()->ForceNetUpdate();
}

void UBrLifeComponent::Service()
{
	if (const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>())
	{
		if (GS->bResults)
		{
			CancelRevive();
			GetWorld()->GetTimerManager().ClearTimer(ServiceTimer);
			return;
		}
	}
	const float Now = ServerTime();
	if (InvulnerabilityEndTime > 0 && Now >= InvulnerabilityEndTime)
	{
		InvulnerabilityEndTime = 0;
	}
	if (State == EBrLifeState::Downed)
	{
		if (Reviver && (!IsValid(Reviver) || !Reviver->GetLife()->CanRevive(Character())))
		{
			CancelRevive();
		}
		// A revive completed before the dying deadline wins even if this service frame was late.
		if (Reviver && Now >= ReviveEndTime && ReviveEndTime <= DownedEndTime)
		{
			FinishRevive();
			return;
		}
		if (Now >= DownedEndTime)
		{
			ResolveDowned(false);
			return;
		}
		if (RespawnHoldEndTime > 0 && Now >= RespawnHoldEndTime) { ResolveDowned(true); }
	}
	else if (InvulnerabilityEndTime <= 0)
	{
		GetWorld()->GetTimerManager().ClearTimer(ServiceTimer);
	}
}

void UBrLifeComponent::FinishRevive()
{
	if (State != EBrLifeState::Downed || !Reviver || !Reviver->GetLife()->CanRevive(Character()))
	{
		CancelRevive();
		return;
	}
	CancelRevive();
	DownedEndTime = RespawnHoldEndTime = 0;
	State = EBrLifeState::Alive;
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
		Mode->ClearQuestDown(Character()->GetController());
	Character()->GetHealth()->SetHealth(Character()->GetHealth()->MaxHealth *
		FMath::Clamp(UBrDeveloperSettings::Get()->RevivedHealthFraction, 0.01f, 1.0f));
	OnRep_State();
	GetWorld()->GetTimerManager().ClearTimer(ServiceTimer);
	GetOwner()->ForceNetUpdate();
	UE_LOG(LogBr, Display, TEXT("Life Revived: %s Health=%.1f"), *GetOwner()->GetName(),
	       Character()->GetHealth()->Health);
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
	{
		Mode->RecordPlayer(Character()->GetController());
	}
}

void UBrLifeComponent::ResolveDowned(bool bVoluntary)
{
	ABrPlayerState* PS = Character()->GetPlayerState<ABrPlayerState>();
	ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	if (!PS || !Mode || !Character()->GetController())
	{
		UE_LOG(LogBr, Error, TEXT("Life outcome missing PlayerState/GameMode/controller: %s"), *GetOwner()->GetName());
		Eliminate();
		return;
	}
	const EBrDownedOutcome Outcome = FBrLifeRules::Outcome(bVoluntary, PS->DeathCount,
	                                                       Mode->AreRespawnsDisabled()
		                                                       ? 0
		                                                       : UBrDeveloperSettings::Get()->MaxDeaths);
	if (Outcome == EBrDownedOutcome::Refused)
	{
		RespawnHoldEndTime = 0;
		return;
	}
	if (Outcome == EBrDownedOutcome::Eliminated)
	{
		Eliminate();
		return;
	}
	CancelRevive();
	if (!Mode->RespawnPlayer(Character(), GearAtDown, bVoluntary))
	{
		if (State != EBrLifeState::Downed)
		{
			return; // Exhausted spawn budget may have eliminated us.
		}
		// Bounded by the GameMode's round-local budget; never charge a failed respawn.
		RespawnHoldEndTime = 0;
		DownedEndTime = ServerTime() + 1;
	}
}

void UBrLifeComponent::Eliminate()
{
	if (!GetOwner()->HasAuthority() || State == EBrLifeState::Eliminated)
	{
		return;
	}
	CancelRevive();
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
		Mode->ResolveQuestLife(Character()->GetController(), true);
	State = EBrLifeState::Eliminated;
	DownedEndTime = RespawnHoldEndTime = InvulnerabilityEndTime = 0;
	if (ABrPlayerState* PS = Character()->GetPlayerState<ABrPlayerState>())
	{
		PS->bEliminated = true;
		PS->ForceNetUpdate();
	}
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
	{
		Mode->LeaveCorpse(Character());
		Mode->OnPlayerEliminated(Character()->GetPlayerState<ABrPlayerState>());
	}
	else UE_LOG(LogBr, Error, TEXT("Elimination cannot leave corpse without BrGameMode."));
	OnRep_State();
	GetWorld()->GetTimerManager().ClearTimer(ServiceTimer);
	GetOwner()->ForceNetUpdate();
	UE_LOG(LogBr, Display, TEXT("Life Eliminated: %s"), *GetOwner()->GetName());
}

void UBrLifeComponent::GrantSpawnProtection()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}
	InvulnerabilityEndTime = ServerTime() + FMath::Max(
		0.0f, UBrDeveloperSettings::Get()->RespawnInvulnerabilityDuration);
	if (HasBegunPlay())
	{
		EnsureServiceTimer();
	}
}

void UBrLifeComponent::Fired()
{
	if (GetOwner()->HasAuthority())
	{
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(Character()->GetController()))
		{
			PC->CancelUnstuck(TEXT("Unstuck cancelled: firing/throwing."));
		}
	}
	if (GetOwner()->HasAuthority() && UBrDeveloperSettings::Get()->bCancelInvulnerabilityOnFire)
	{
		InvulnerabilityEndTime = 0;
		GetOwner()->ForceNetUpdate();
	}
}
