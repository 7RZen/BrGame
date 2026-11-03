#include "BrCharacterMovementComponent.h"
#include "BrPlayerController.h"
#include "BrCharacter.h"
#include "BrDeveloperSettings.h"
#include "BrAimingRules.h"
#include "BrInventoryComponent.h"
#include "BrLifeComponent.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	class FBrSavedMove : public FSavedMove_Character
	{
	public:
		using Super = FSavedMove_Character;
		bool bSprint = false;
		bool bForwardSprint = false;
		float SprintFacingYaw = 0;
		double SprintMoveWorldTime = 0;
		FBrSprintState StartSprint;
		FBrSprintState EndSprint;
		bool bProne = false;
		EBrPosture EndPosture = EBrPosture::Standing;
		EBrPosture StartPosture = EBrPosture::Standing;

		virtual void Clear() override
		{
			Super::Clear();
			bSprint = bProne = false;
			bForwardSprint = false;
			SprintFacingYaw = 0;
			SprintMoveWorldTime = 0;
			StartSprint = FBrSprintState();
			EndSprint = FBrSprintState();
			StartPosture = EndPosture = EBrPosture::Standing;
		}

		virtual uint8 GetCompressedFlags() const override
		{
			// Custom_2/3 report predicted actual posture, not a request or trusted server state.
			return Super::GetCompressedFlags() | (bSprint ? FLAG_Custom_0 : 0) | (bProne ? FLAG_Custom_1 : 0) |
				(static_cast<uint8>(EndPosture) << 6);
		}

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
		{
			const FBrSavedMove* Other = static_cast<const FBrSavedMove*>(NewMove.Get());
			const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
			FBrSprintState Combined = StartSprint;
			Combined.Advance(DeltaTime + Other->DeltaTime, bForwardSprint && bSprint, S->SprintStaminaDuration,
				S->SprintMinimumStart, S->SprintRegenDelay, S->SprintRegenDuration);
			// Combine linear drain/regen, but never span a gait/eligibility boundary.
			// Native combining reruns the pending move; CombineWith must rewind our state too.
			return bSprint == Other->bSprint && bForwardSprint == Other->bForwardSprint &&
				StartSprint.bActive == EndSprint.bActive && StartSprint.bActive == Other->StartSprint.bActive &&
				Combined.bActive == StartSprint.bActive &&
				(!bSprint || !bForwardSprint || StartSprint.bActive ||
					(StartSprint.Stamina >= S->SprintMinimumStart) == (Other->StartSprint.Stamina >= S->SprintMinimumStart)) &&
				bProne == Other->bProne && EndPosture == Other->EndPosture &&
				StartPosture == EndPosture && Other->StartPosture == Other->EndPosture &&
				Super::CanCombineWith(NewMove, Character, MaxDelta);
		}

		virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel,
			FNetworkPredictionData_Client_Character& ClientData) override
		{
			Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
			const UBrCharacterMovementComponent* Movement = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement());
			bSprint = Movement->WantsSprint();
			SprintFacingYaw = Character->GetControlRotation().Yaw;
			// World time at this move's simulation end, retained across timestamp resets
			// and combining (the newest move's end remains the combined move's end).
			SprintMoveWorldTime = Character->GetWorld()->GetTimeSeconds();
			StartSprint = EndSprint = Movement->GetSprintState();
			bForwardSprint = Movement->IsMovingOnGround() && Movement->GetPosture() == EBrPosture::Standing &&
				FBrSprintState::IsForward(NewAccel, SprintFacingYaw,
					UBrDeveloperSettings::Get()->SprintForwardHalfAngle);
			bProne = Movement->WantsProne();
			StartPosture = EndPosture = Movement->GetPosture();
		}

		virtual void PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode) override
		{
			Super::PostUpdate(Character, PostUpdateMode);
			EndPosture = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement())->GetPosture();
			EndSprint = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement())->GetSprintState();
			bForceNoCombine |= StartPosture != EndPosture || StartSprint.bActive != EndSprint.bActive;
			if (PostUpdateMode == PostUpdate_Replay)
				CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement())->FinishSprintReplay();
		}

		virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* Character,
			APlayerController* PC, const FVector& OldStartLocation) override
		{
			StartSprint = static_cast<const FBrSavedMove*>(OldMove)->StartSprint;
			CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement())->RestoreSprintStateForCombine(StartSprint);
			Super::CombineWith(OldMove, Character, PC, OldStartLocation);
		}

		virtual void PrepMoveFor(ACharacter* Character) override
		{
			Super::PrepMoveFor(Character);
			UBrCharacterMovementComponent* Movement = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement());
			Movement->RestoreSprintIntent(bSprint);
			Movement->PrepareSprintReplay(SprintFacingYaw, SprintMoveWorldTime);
			Movement->SetProne(bProne);
		}
	};

	class FBrClientPredictionData : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FBrClientPredictionData(const UCharacterMovementComponent& Movement)
			: FNetworkPredictionData_Client_Character(Movement) {}

		virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FBrSavedMove()); }
	};
}

void FBrNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(Move, Type);
	Sprint = static_cast<const FBrSavedMove&>(Move).EndSprint;
}

namespace
{
	void SerializeSprint(FArchive& Ar, FBrSprintState& State)
	{
		Ar << State.Stamina;
		Ar << State.RestTime;
		Ar.SerializeBits(&State.bActive, 1);
		if (!FMath::IsFinite(State.Stamina) || !FMath::IsFinite(State.RestTime) ||
			State.Stamina < 0 || State.RestTime < 0) Ar.SetError();
	}
}

bool FBrNetworkMoveData::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type)
{
	const bool bSuccess = FCharacterNetworkMoveData::Serialize(Movement, Ar, Map, Type);
	SerializeSprint(Ar, Sprint);
	return bSuccess && !Ar.IsError();
}

void FBrMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& Movement,
	const FClientAdjustment& Adjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement, Adjustment);
	const UBrCharacterMovementComponent& BrMovement = *CastChecked<UBrCharacterMovementComponent>(&Movement);
	Posture = BrMovement.GetServerCorrectionPosture();
	RequestedPosture = BrMovement.GetServerCorrectionRequest();
	Sprint = BrMovement.GetServerCorrectionSprint();
}

bool FBrMoveResponseDataContainer::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap)
{
	const bool bBaseSuccess = FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, PackageMap);
	SerializeSprint(Ar, Sprint);
	uint8 PostureBits = static_cast<uint8>(Posture);
	uint8 RequestBits = static_cast<uint8>(RequestedPosture);
	Ar.SerializeBits(&PostureBits, 2);
	Ar.SerializeBits(&RequestBits, 2);
	if (PostureBits > static_cast<uint8>(EBrPosture::Prone) || RequestBits > static_cast<uint8>(EBrPosture::Prone))
	{
		Ar.SetError();
		return false;
	}
	if (Ar.IsLoading())
	{
		Posture = static_cast<EBrPosture>(PostureBits);
		RequestedPosture = static_cast<EBrPosture>(RequestBits);
	}
	return bBaseSuccess && !Ar.IsError();
}

UBrCharacterMovementComponent::UBrCharacterMovementComponent()
{
	SetIsReplicatedByDefault(true);
	GetNavAgentPropertiesRef().bCanCrouch = true;
	SetMoveResponseDataContainer(MoveResponseData);
	SetNetworkMoveDataContainer(NetworkMoveData);
}

void UBrCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	Sprint.Stamina = FMath::Max(0.1f, UBrDeveloperSettings::Get()->SprintStaminaDuration);
}

void UBrCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UBrCharacterMovementComponent, ReplicatedPosture, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UBrCharacterMovementComponent, bReplicatedSprint, COND_SkipOwner);
}

FNetworkPredictionData_Client* UBrCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		// The base movement component owns and deletes prediction data.
		UBrCharacterMovementComponent* MutableThis = const_cast<UBrCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FBrClientPredictionData(*this);
	}
	return ClientPredictionData;
}

float UBrCharacterMovementComponent::GetCombatMoveTimeStamp() const
{
	return CharacterOwner && !CharacterOwner->HasAuthority() ?
		GetPredictionData_Client_Character()->CurrentTimeStamp : 0.f;
}

bool UBrCharacterMovementComponent::IsValidCombatMoveTimeStamp(float TimeStamp) const
{
	// CMC resets before adding the current frame delta, so allow one maximum frame above the period.
	return FMath::IsFinite(TimeStamp) && TimeStamp >= 0.f && TimeStamp <= MinTimeBetweenTimeStampResets + 1.f;
}

void UBrCharacterMovementComponent::ApplyAcceptedCombatAim(const FVector& Aim, float MoveTimeStamp)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority() || !CharacterOwner->GetController()) return;
	CombatAimRotation = FRotator(0, Aim.Rotation().Yaw, 0);
	CharacterOwner->GetController()->SetControlRotation(CombatAimRotation);
	CharacterOwner->SetActorRotation(CombatAimRotation);
	CharacterOwner->ForceNetUpdate();
	// The listen host has no incoming client moves to fence.
	if (!CharacterOwner->IsLocallyControlled()) CombatAimTimeStamp = MoveTimeStamp;
}

void UBrCharacterMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
	// Prediction reports are comparison-only. Never accept client stamina as authority.
	ClientReportedSprint = static_cast<const FBrNetworkMoveData&>(MoveData).Sprint;
	const bool bAfterAim = CombatAimTimeStamp.IsSet() && FBrAimingRules::IsMoveAfterCombatAim(
		MoveData.TimeStamp, CombatAimTimeStamp.GetValue(), MinTimeBetweenTimeStampResets);
	if (CombatAimTimeStamp.IsSet() && !bAfterAim)
	{
		// A reliable combat request can overtake an older unreliable/batched move. Preserve movement,
		// posture and native timestamp checks, but don't let its pre-shot control yaw undo the shot.
		FCharacterNetworkMoveData OrderedMove = MoveData;
		OrderedMove.ControlRotation = CombatAimRotation;
		Super::ServerMove_PerformMovement(OrderedMove);
	}
	else
	{
		Super::ServerMove_PerformMovement(MoveData);
	}
	// Only release the fence after CMC actually accepted a strictly newer move, not a forged/stale one.
	if (bAfterAim && GetPredictionData_Server_Character()->CurrentClientTimeStamp == MoveData.TimeStamp)
		CombatAimTimeStamp.Reset();
}

void UBrCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsToProne = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	// Prone is another low-capsule posture, encoded alongside the engine's crouch request.
	bWantsToCrouch = bWantsToCrouch || bWantsToProne;
	ClientReportedPosture = Flags >> 6;
}

bool UBrCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	const bool bRealSprint = bWantsToSprint;
	const bool bRealProne = bWantsToProne;
	const bool bRealCrouch = bWantsToCrouch;
	const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
	FinishSprintReplay(); // Also cover native early-outs / an empty replay list.
	bWantsToSprint = bRealSprint;
	bWantsToProne = bRealProne;
	bWantsToCrouch = bRealCrouch;
	return bReplayed;
}

void UBrCharacterMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime,
	const FVector& Accel, const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData,
	FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	// A posture-only disagreement must correct even with zero positional error,
	// including engine paths that otherwise tolerate error while falling.
	if (ClientReportedPosture != static_cast<uint8>(LocalPosture) ||
		FMath::Abs(ClientReportedSprint.Stamina - Sprint.Stamina) > 0.05f ||
		FMath::Abs(ClientReportedSprint.RestTime - Sprint.RestTime) > 0.05f ||
		ClientReportedSprint.bActive != Sprint.bActive)
	{
		GetPredictionData_Server_Character()->bForceClientUpdate = true;
	}
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation,
		ClientMovementBaseInterfaceData, ClientBaseBoneName, ClientMovementMode);
	const FClientAdjustment& Adjustment = GetPredictionData_Server_Character()->PendingAdjustment;
	if (Adjustment.TimeStamp == ClientTimeStamp)
	{
		// Snapshot with the pending adjustment, not the later state at send time.
		ServerCorrectionPosture = LocalPosture;
		ServerCorrectionRequest = GetRequestedPosture();
		ServerCorrectionSprint = Sprint;
	}
}

void UBrCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
#if !UE_BUILD_SHIPPING
	// Count every correction received, including one native CMC cannot apply (unresolved base,
	// expired saved move). Such a response must never produce a success-shaped smoke result.
	if (MoveResponse.IsCorrection())
		if (ABrCharacter* Character = Cast<ABrCharacter>(CharacterOwner))
			if (Character->bSprintSmokeRunning && Character->SprintSmokeElapsed > 2.5f && Character->SprintSmokeElapsed < 6.5f)
				++Character->SprintSmokeCorrections;
#endif
	const bool bKnownMove = HasValidData() &&
		GetPredictionData_Client_Character()->GetSavedMoveIndex(MoveResponse.ClientAdjustment.TimeStamp) != INDEX_NONE;
	Super::ClientHandleMoveResponse(MoveResponse);
	if (!HasValidData() || !bKnownMove || bClientIgnoreMovementCorrections)
	{
		return;
	}
	const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	// The native handler validates timestamps/bases and acknowledges only accepted responses.
	if (!ClientData->LastAckedMove.IsValid() ||
		ClientData->LastAckedMove->TimeStamp != MoveResponse.ClientAdjustment.TimeStamp)
	{
		return;
	}
	const FBrMoveResponseDataContainer& Response = static_cast<const FBrMoveResponseDataContainer&>(MoveResponse);
	if (MoveResponse.IsCorrection() && ClientData->bUpdatePosition)
	{
		// Native correction rewinds location to this same move; replay then integrates energy
		// from its authoritative end state, NOT each saved move's old predicted stamina.
		Sprint = Response.Sprint;
		ApplyAuthoritativePosture(Response.Posture);
		// Do not immediately predict the same rejected expansion during replay.
		// The server continues retrying the unchanged request; a new local request
		// or a later authoritative approval removes this prediction fence.
		if (Response.Posture != Response.RequestedPosture)
		{
			RejectedPostureRequest = Response.RequestedPosture;
		}
		else
		{
			RejectedPostureRequest.Reset();
		}
	}
}

float UBrCharacterMovementComponent::GetMaxSpeed() const
{
	const ABrCharacter* Character = Cast<ABrCharacter>(CharacterOwner);
	if (Character && !Character->CanMove())
	{
		return 0.0f;
	}
	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking && MovementMode != MOVE_Falling)
	{
		return Super::GetMaxSpeed();
	}
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	return FBrMovementSpeed::Calculate(GetPosture(), IsSprinting(),
		Character ? Character->GetWeaponRunSpeedMultiplier() : 1.0f,
		Settings->BaseRunSpeed, Settings->SprintSpeedMultiplier,
		Settings->CrouchSpeedMultiplier, Settings->ProneSpeedMultiplier);
}

EBrPosture UBrCharacterMovementComponent::GetPosture() const
{
	return CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy ? ReplicatedPosture : LocalPosture;
}

float UBrCharacterMovementComponent::GetPostureHalfHeight(EBrPosture Posture) const
{
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const UCapsuleComponent* DefaultCapsule = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent();
	if (Posture == EBrPosture::Standing)
	{
		return DefaultCapsule->GetUnscaledCapsuleHalfHeight();
	}
	const float Height = Posture == EBrPosture::Prone ? Settings->ProneCapsuleHalfHeight : Settings->CrouchCapsuleHalfHeight;
	return FMath::Clamp(Height, DefaultCapsule->GetUnscaledCapsuleRadius(), DefaultCapsule->GetUnscaledCapsuleHalfHeight());
}

void UBrCharacterMovementComponent::RequestPosture(EBrPosture Posture)
{
	SetSprinting(false);
	RejectedPostureRequest.Reset();
	bWantsToCrouch = Posture != EBrPosture::Standing;
	bWantsToProne = Posture == EBrPosture::Prone;
}

EBrPosture UBrCharacterMovementComponent::GetRequestedPosture() const
{
	return bWantsToProne ? EBrPosture::Prone : (bWantsToCrouch ? EBrPosture::Crouching : EBrPosture::Standing);
}

void UBrCharacterMovementComponent::Crouch(bool bClientSimulation)
{
	if (bClientSimulation)
	{
		ApplyAuthoritativePosture(ReplicatedPosture);
		return;
	}
	SetCrouchedHalfHeight(GetPostureHalfHeight(bWantsToProne ? EBrPosture::Prone : EBrPosture::Crouching));
	if (IsCrouching() && LocalPosture == GetRequestedPosture() &&
		FMath::IsNearlyEqual(CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), GetCrouchedHalfHeight()))
	{
		return;
	}
	Super::Crouch(bClientSimulation);
}

void UBrCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	if (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		const ABrCharacter* Character = CastChecked<ABrCharacter>(CharacterOwner);
		if (!Character->CanChangePosture())
		{
			// Enforce on the authority as well as the owning client; incoming moves cannot restore movement.
			DisableMovement();
			StopMovementImmediately();
			RefreshPosture();
			SetSprinting(false);
			return;
		}
		if (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy &&
			RejectedPostureRequest.IsSet() && RejectedPostureRequest.GetValue() == GetRequestedPosture())
		{
			RefreshPosture();
			UpdateSprint(DeltaSeconds);
			return;
		}
		if (IsCrouching() && bWantsToCrouch && CanCrouchInCurrentState())
		{
			const float DesiredHeight = GetPostureHalfHeight(bWantsToProne ? EBrPosture::Prone : EBrPosture::Crouching);
			if (!FMath::IsNearlyEqual(CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), DesiredHeight))
			{
				// Engine Crouch also checks clearance when growing from prone to crouch.
				Crouch(false);
			}
		}
	}
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	RefreshPosture();
	UpdateSprint(DeltaSeconds);
}

float UBrCharacterMovementComponent::GetSprintFraction() const
{
	return FMath::Clamp(Sprint.Stamina / FMath::Max(0.1f, UBrDeveloperSettings::Get()->SprintStaminaDuration), 0.f, 1.f);
}

bool UBrCharacterMovementComponent::IsSprinting() const
{
	return CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy ? bReplicatedSprint : Sprint.bActive;
}

void UBrCharacterMovementComponent::SetSprinting(bool bSprinting)
{
	if (bSprinting && CharacterOwner && CharacterOwner->IsLocallyControlled())
	{
		const ABrCharacter* Character = Cast<ABrCharacter>(CharacterOwner);
		if (Character && Character->GetInventory()->IsCombatHeld()) bSprinting = false;
	}
	bWantsToSprint = bSprinting;
	if (!bSprinting && Sprint.bActive)
	{
		Sprint.bActive = false;
		SprintStopped();
	}
	if (CharacterOwner && CharacterOwner->HasAuthority()) bReplicatedSprint = Sprint.bActive;
}

void UBrCharacterMovementComponent::PrepareSprintReplay(float FacingYaw, double MoveWorldTime)
{
	SprintReplayYaw = FacingYaw;
	SprintReplayWorldTime = MoveWorldTime;
}

void UBrCharacterMovementComponent::FinishSprintReplay()
{
	SprintReplayYaw.Reset();
}

void UBrCharacterMovementComponent::SprintStopped()
{
	// Replaying a historical stop may reconstruct its original deadline, but must
	// never move an existing combat recovery to "replay now + delay".
	const double StopTime = SprintReplayYaw.IsSet() ? SprintReplayWorldTime : GetWorld()->GetTimeSeconds();
	SprintFireReadyTime = FMath::Max(SprintFireReadyTime, StopTime + UBrDeveloperSettings::Get()->SprintToFireDelay);
	if (!SprintReplayYaw.IsSet() && CharacterOwner && CharacterOwner->HasAuthority())
		CastChecked<ABrCharacter>(CharacterOwner)->RequestAutoPickupRetry();
}

void UBrCharacterMovementComponent::CancelSprintForCombat()
{
	SetSprinting(false);
}

bool UBrCharacterMovementComponent::CanSprintFire() const
{
	return FBrSprintState::CanFire(IsSprinting(), GetWorld()->GetTimeSeconds(), SprintFireReadyTime,
		FMath::Clamp(UBrDeveloperSettings::Get()->SprintFireGrace, 0.f, 0.05f));
}

void UBrCharacterMovementComponent::UpdateSprint(float DeltaSeconds)
{
	if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) return;
	const ABrCharacter* Character = CastChecked<ABrCharacter>(CharacterOwner);
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const bool bWasActive = Sprint.bActive;
	if (Character->IsGameplayInputBlocked()) bWantsToSprint = false;
	// Acceleration is CMC's move acceleration on authority, not a separately supplied direction.
	const float Yaw = SprintReplayYaw.Get(CharacterOwner->GetControlRotation().Yaw);
	const bool bAllowed = bWantsToSprint && Character->CanMove() && !Character->IsGameplayInputBlocked() &&
		GetPosture() == EBrPosture::Standing && IsMovingOnGround() &&
		(!CharacterOwner->HasAuthority() || !Character->GetInventory()->IsCombatHeld()) && !Character->GetLife()->ReviveTarget &&
		FBrSprintState::IsForward(Acceleration, Yaw, S->SprintForwardHalfAngle);
	Sprint.Advance(DeltaSeconds, bAllowed, FMath::Max(0.1f, S->SprintStaminaDuration),
		S->SprintMinimumStart, S->SprintRegenDelay, S->SprintRegenDuration);
	if (bWasActive && !Sprint.bActive)
	{
		SprintStopped();
	}
	if (CharacterOwner->HasAuthority()) bReplicatedSprint = Sprint.bActive;
}

void UBrCharacterMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateAfterMovement(DeltaSeconds);
	RefreshPosture();
	if (CharacterOwner->HasAuthority())
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(CharacterOwner->GetController())) PC->ValidateUnstuckMovement();
}

void UBrCharacterMovementComponent::RefreshPosture()
{
	if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}
	if (!IsCrouching())
	{
		LocalPosture = EBrPosture::Standing;
	}
	else if (bWantsToCrouch && FMath::IsNearlyEqual(CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),
		GetPostureHalfHeight(bWantsToProne ? EBrPosture::Prone : EBrPosture::Crouching)))
	{
		LocalPosture = bWantsToProne ? EBrPosture::Prone : EBrPosture::Crouching;
	}
	// Keep actual posture when a requested expansion was blocked.
	if (CharacterOwner->HasAuthority())
	{
		if (ReplicatedPosture != LocalPosture)
			if (ABrPlayerController* PC = Cast<ABrPlayerController>(CharacterOwner->GetController()))
				PC->CancelUnstuck(TEXT("Unstuck cancelled: posture changed."));
		ReplicatedPosture = LocalPosture;
	}
}

void UBrCharacterMovementComponent::OnRep_Posture()
{
	ApplyReplicatedPosture();
}

void UBrCharacterMovementComponent::ApplyReplicatedPosture()
{
	if (!HasValidData() || CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		return;
	}
	ApplyAuthoritativePosture(ReplicatedPosture);
}

void UBrCharacterMovementComponent::ApplyAuthoritativePosture(EBrPosture Posture)
{
	if (!HasValidData())
	{
		return;
	}
	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float NewHeight = GetPostureHalfHeight(Posture);
	const float OldHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	CharacterOwner->SetIsCrouched(Posture != EBrPosture::Standing);
	if (LocalPosture == Posture && FMath::IsNearlyEqual(OldHeight, NewHeight))
	{
		return;
	}

	const FBrPostureAdjustment Adjustment = FBrPostureAdjustment::Calculate(
		GetPostureHalfHeight(EBrPosture::Standing), OldHeight, NewHeight, Capsule->GetShapeScale());
	LocalPosture = Posture;
	Capsule->SetCapsuleHalfHeight(NewHeight, true);
	bShrinkProxyCapsule = false;
	bForceNextFloorCheck = true;
	if (Posture == EBrPosture::Standing)
	{
		CharacterOwner->OnEndCrouch(NewHeight - OldHeight, -Adjustment.ScaledHeightDelta);
	}
	else
	{
		SetCrouchedHalfHeight(NewHeight);
		CharacterOwner->OnStartCrouch(Adjustment.HeightFromStanding,
			Adjustment.HeightFromStanding * Capsule->GetShapeScale());
	}
	if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Actor replication already moves the capsule center. Remove only this
		// transition's height delta from smoothing, never a standing->low delta.
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		ClientData->MeshTranslationOffset -= Adjustment.ScaledHeightDelta * -GetGravityDirection();
		ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
	}
}
