#include "BrCharacterMovementComponent.h"
#include "BrPlayerController.h"
#include "BrCharacter.h"
#include "BrDeveloperSettings.h"
#include "BrAimingRules.h"
#include "BrInventoryComponent.h"
#include "BrLifeComponent.h"
#include "BrHealthComponent.h"
#include "BrAudioSubsystem.h"
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
		FBrSlideState StartSlide;
		FBrSlideState EndSlide;
		bool bSlidePressed = false;
		bool bInputBlocked = false;
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
			StartSlide = EndSlide = FBrSlideState();
			bSlidePressed = bInputBlocked = false;
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
			return !bSlidePressed && !Other->bSlidePressed && !StartSlide.bActive && !EndSlide.bActive &&
				!Other->StartSlide.bActive && StartSlide.Cooldown <= 0 && Other->StartSlide.Cooldown <= 0 &&
				bInputBlocked == Other->bInputBlocked &&
				bSprint == Other->bSprint && bForwardSprint == Other->bForwardSprint &&
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
			bSlidePressed = Movement->WantsSlide();
			bInputBlocked = CastChecked<ABrCharacter>(Character)->IsGameplayInputBlocked();
			StartSlide = EndSlide = Movement->GetSlideState();
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
			const FRotator RecordedControlRotation = SavedControlRotation;
			Super::PostUpdate(Character, PostUpdateMode);
			// Native PostUpdate samples the live controller even on replay. A resent move
			// must retain the rotation originally recorded for this input, matching the
			// historical yaw used by sprint/slide prediction, not a later mouse turn.
			if (PostUpdateMode == PostUpdate_Replay)
				SavedControlRotation = RecordedControlRotation;
			EndPosture = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement())->GetPosture();
			EndSprint = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement())->GetSprintState();
			EndSlide = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement())->GetSlideState();
			bForceNoCombine |= bSlidePressed || StartSlide.bActive || EndSlide.bActive || StartSlide.Cooldown > 0;
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
			Movement->RestoreSlideRequest(bSlidePressed);
			Movement->RestoreInputBlock(bInputBlocked);
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
	Slide = static_cast<const FBrSavedMove&>(Move).EndSlide;
	bSlidePressed = static_cast<const FBrSavedMove&>(Move).bSlidePressed;
	bInputBlocked = static_cast<const FBrSavedMove&>(Move).bInputBlocked;
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
	void SerializeSlide(FArchive& Ar, FBrSlideState& State)
	{
		Ar.SerializeBits(&State.bActive, 1);
		Ar << State.Remaining << State.Cooldown << State.InitialSpeed << State.FinalSpeed
			<< State.Duration << State.StartMoveTime << State.Direction;
		if (!FMath::IsFinite(State.Remaining) || !FMath::IsFinite(State.Cooldown) ||
			!FMath::IsFinite(State.InitialSpeed) || !FMath::IsFinite(State.FinalSpeed) ||
			!FMath::IsFinite(State.Duration) || !FMath::IsFinite(State.StartMoveTime) ||
			State.Direction.ContainsNaN() || State.Remaining < 0 || State.Cooldown < 0 ||
			State.InitialSpeed < 0 || State.FinalSpeed < 0 || State.Duration < 0) Ar.SetError();
	}
}

bool FBrNetworkMoveData::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type)
{
	const bool bSuccess = FCharacterNetworkMoveData::Serialize(Movement, Ar, Map, Type);
	SerializeSprint(Ar, Sprint);
	SerializeSlide(Ar, Slide);
	Ar.SerializeBits(&bSlidePressed, 1);
	Ar.SerializeBits(&bInputBlocked, 1);
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
	Slide = BrMovement.GetServerCorrectionSlide();
}

bool FBrMoveResponseDataContainer::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap)
{
	const bool bBaseSuccess = FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, PackageMap);
	SerializeSprint(Ar, Sprint);
	SerializeSlide(Ar, Slide);
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
	DOREPLIFETIME_CONDITION(UBrCharacterMovementComponent, bReplicatedSlide, COND_SkipOwner);
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
	const auto& BrMove = static_cast<const FBrNetworkMoveData&>(MoveData);
	ClientReportedSlide = BrMove.Slide;
	bSlidePressed = BrMove.bSlidePressed;
	bMoveInputBlocked = BrMove.bInputBlocked;
	// Eligibility must use the ORIGINAL move yaw, even if a newer combat aim fences actor facing.
	MoveInputYaw = MoveData.ControlRotation.Yaw;
	SlideMoveTimeStamp = MoveData.TimeStamp;
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
	MoveInputYaw.Reset();
	bSlidePressed = false;
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
	const bool bRealSlide = bSlidePressed;
	const bool bRealBlocked = bMoveInputBlocked;
	const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
	FinishSprintReplay(); // Also cover native early-outs / an empty replay list.
	bWantsToSprint = bRealSprint;
	bWantsToProne = bRealProne;
	bWantsToCrouch = bRealCrouch;
	bSlidePressed = bRealSlide;
	bMoveInputBlocked = bRealBlocked;
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
		ClientReportedSprint.bActive != Sprint.bActive ||
		ClientReportedSlide.bActive != Slide.bActive ||
		FMath::Abs(ClientReportedSlide.Remaining - Slide.Remaining) > .02f ||
		FMath::Abs(ClientReportedSlide.Cooldown - Slide.Cooldown) > .02f ||
		(Slide.bActive && (!ClientReportedSlide.Direction.Equals(Slide.Direction, .005f) ||
			FMath::Abs(ClientReportedSlide.InitialSpeed - Slide.InitialSpeed) > .5f)))
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
		ServerCorrectionSlide = Slide;
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
	if (MoveResponse.IsCorrection())
		if (ABrCharacter* Character = Cast<ABrCharacter>(CharacterOwner))
			if (Character->bSprintSmokeRunning && Character->SprintSmokeElapsed > 11.75f && Character->SprintSmokeElapsed < 13.f)
				++Character->SlideSmokeCorrections;
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
		Slide = Response.Slide;
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
	if (IsSliding()) return Slide.Speed();
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
	if (Posture == EBrPosture::Prone) CancelSlide();
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
			CancelSlide();
			bSlidePressed = false;
			return;
		}
		UpdateSlideBeforeMovement(DeltaSeconds);
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
	MoveInputYaw.Reset();
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
#if !UE_BUILD_SHIPPING
	const float PreviousEnergy = Sprint.Stamina;
#endif
	if (Character->IsGameplayInputBlocked()) bWantsToSprint = false;
	// Acceleration is CMC's move acceleration on authority, not a separately supplied direction.
	const float Yaw = SprintReplayYaw.Get(CharacterOwner->GetControlRotation().Yaw);
	const bool bAllowed = bWantsToSprint && Character->CanMove() && !Character->IsGameplayInputBlocked() &&
		!IsSliding() && !bMoveInputBlocked &&
		GetPosture() == EBrPosture::Standing && IsMovingOnGround() &&
		(!CharacterOwner->HasAuthority() || !Character->GetInventory()->IsCombatHeld()) && !Character->GetLife()->ReviveTarget &&
		FBrSprintState::IsForward(Acceleration, Yaw, S->SprintForwardHalfAngle);
	Sprint.Advance(DeltaSeconds, bAllowed, FMath::Max(0.1f, S->SprintStaminaDuration),
		S->SprintMinimumStart, S->SprintRegenDelay, S->SprintRegenDuration);
#if !UE_BUILD_SHIPPING
	if (CharacterOwner->HasAuthority())
		if (auto* Pawn = Cast<ABrCharacter>(CharacterOwner))
			if (Pawn->bSprintSmokeRunning && !Pawn->bSprintSmokeExhausted && Sprint.Stamina < PreviousEnergy)
			{
				Pawn->SprintSmokeDrainSeconds += PreviousEnergy - Sprint.Stamina;
				Pawn->bSprintSmokeExhausted = Sprint.Stamina <= UE_KINDA_SMALL_NUMBER;
			}
#endif
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
	if (Slide.bActive && (!IsMovingOnGround() || Slide.Remaining <= UE_KINDA_SMALL_NUMBER ||
		Velocity.Size2D() < Slide.Speed() * FMath::Clamp(UBrDeveloperSettings::Get()->SlideWallSpeedFraction, 0.f, 1.f)))
		CancelSlide();
	if (CharacterOwner->HasAuthority())
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(CharacterOwner->GetController())) PC->ValidateUnstuckMovement();
}

bool UBrCharacterMovementComponent::IsSliding() const
{
	return CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy ? bReplicatedSlide : Slide.bActive;
}

void UBrCharacterMovementComponent::CancelSlide()
{
	if (!Slide.bActive) return;
	Slide.bActive = false;
	Slide.Remaining = 0;
	Slide.Cooldown = FMath::Max(0.f, UBrDeveloperSettings::Get()->SlideCooldown);
	if (CharacterOwner && CharacterOwner->HasAuthority()) bReplicatedSlide = false;
	// Crouch intent/capsule persists; prone/down/menu may independently override it.
}

void UBrCharacterMovementComponent::UpdateSlideBeforeMovement(float DeltaSeconds)
{
	const auto* Character = CastChecked<ABrCharacter>(CharacterOwner);
	const auto* S = UBrDeveloperSettings::Get();
	if (CharacterOwner->IsLocallyControlled() && !SprintReplayYaw.IsSet())
		bMoveInputBlocked = Character->IsGameplayInputBlocked();
	const auto* PC = Cast<ABrPlayerController>(CharacterOwner->GetController());
	const bool Blocked = bMoveInputBlocked || (PC && PC->IsGameplayInputBlocked());
	if (!Slide.bActive) Slide.Cooldown = FMath::Max(0.f, Slide.Cooldown - DeltaSeconds);
	if (Slide.bActive && (bWantsToProne || Blocked || !Character->CanMove() || !IsMovingOnGround()))
		CancelSlide();
	if (bSlidePressed && !Slide.bActive && FBrSlideState::Eligible(Velocity.Size2D(),
		FBrMovementSpeed::Calculate(EBrPosture::Standing, false, Character->GetWeaponRunSpeedMultiplier(),
			S->BaseRunSpeed, S->SprintSpeedMultiplier, S->CrouchSpeedMultiplier, S->ProneSpeedMultiplier),
		Acceleration, MoveInputYaw.Get(SprintReplayYaw.Get(CharacterOwner->GetControlRotation().Yaw)),
		S->SprintForwardHalfAngle, Slide.Cooldown, GetPosture(),
		Character->CanMove() && Character->GetHealth()->Health > 0, IsMovingOnGround(),
		Blocked || bWantsToProne || Character->GetLife()->ReviveTarget != nullptr))
	{
		const FVector Direction = Velocity.GetSafeNormal2D();
		const float Speed = Velocity.Size2D();
		bWantsToCrouch = true;
		Crouch(false);
		RefreshPosture();
		if (GetPosture() == EBrPosture::Crouching)
		{
			Slide.bActive = true;
			Slide.Direction = Direction;
			Slide.InitialSpeed = Speed * FMath::Clamp(S->SlideSpeedMultiplier, 1.f, 2.f);
			Slide.FinalSpeed = S->BaseRunSpeed * S->CrouchSpeedMultiplier;
			Slide.Duration = Slide.Remaining = FMath::Clamp(S->SlideDuration, .05f, 2.f);
			Slide.StartMoveTime = CharacterOwner->HasAuthority() ? SlideMoveTimeStamp : GetCombatMoveTimeStamp();
			SetSprinting(false);
			// Slide replaces sprint recovery: new shots/throws are immediately allowed.
			SprintFireReadyTime = 0;
			if (CharacterOwner->HasAuthority()) bReplicatedSlide = true;
			if (!SprintReplayYaw.IsSet())
				if (auto* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
				{
					if (CharacterOwner->HasAuthority()) Audio->Emit(CharacterOwner, EBrSoundKind::Slide, S->SlideSound, CharacterOwner->GetActorLocation());
					if (CharacterOwner->IsLocallyControlled()) Audio->Play(EBrSoundKind::Slide, S->SlideSound, CharacterOwner->GetActorLocation());
				}
		}
	}
	bSlidePressed = false; // edge-triggered, never queued until eligibility changes
	if (Slide.bActive)
	{
		bWantsToCrouch = true;
		bWantsToSprint = false;
	}
}

void UBrCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (!Slide.bActive || !IsMovingOnGround())
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}
	// Native walking owns collision, steps, floor and falling. Replace only horizontal
	// acceleration/friction; integrate the linear curve exactly over each physics substep.
	const float ActiveDelta = FMath::Min(DeltaTime, Slide.Remaining);
	const float Distance = (Slide.Speed() + Slide.Speed(ActiveDelta)) * .5f * ActiveDelta +
		Slide.FinalSpeed * FMath::Max(0.f, DeltaTime - ActiveDelta);
	Velocity = Slide.Direction * (Distance / FMath::Max(UE_SMALL_NUMBER, DeltaTime));
	Slide.Remaining = FMath::Max(0.f, Slide.Remaining - DeltaTime);
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
