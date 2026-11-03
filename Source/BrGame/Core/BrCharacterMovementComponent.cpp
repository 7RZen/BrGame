#include "BrCharacterMovementComponent.h"
#include "BrPlayerController.h"
#include "BrCharacter.h"
#include "BrDeveloperSettings.h"
#include "BrAimingRules.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	class FBrSavedMove : public FSavedMove_Character
	{
	public:
		using Super = FSavedMove_Character;
		bool bWalk = false;
		bool bProne = false;
		EBrPosture EndPosture = EBrPosture::Standing;
		EBrPosture StartPosture = EBrPosture::Standing;

		virtual void Clear() override
		{
			Super::Clear();
			bWalk = bProne = false;
			StartPosture = EndPosture = EBrPosture::Standing;
		}

		virtual uint8 GetCompressedFlags() const override
		{
			// Custom_2/3 report predicted actual posture, not a request or trusted server state.
			return Super::GetCompressedFlags() | (bWalk ? FLAG_Custom_0 : 0) | (bProne ? FLAG_Custom_1 : 0) |
				(static_cast<uint8>(EndPosture) << 6);
		}

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
		{
			const FBrSavedMove* Other = static_cast<const FBrSavedMove*>(NewMove.Get());
			return bWalk == Other->bWalk && bProne == Other->bProne && EndPosture == Other->EndPosture &&
				StartPosture == EndPosture && Other->StartPosture == Other->EndPosture &&
				Super::CanCombineWith(NewMove, Character, MaxDelta);
		}

		virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel,
			FNetworkPredictionData_Client_Character& ClientData) override
		{
			Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
			const UBrCharacterMovementComponent* Movement = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement());
			bWalk = Movement->IsWalking();
			bProne = Movement->WantsProne();
			StartPosture = EndPosture = Movement->GetPosture();
		}

		virtual void PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode) override
		{
			Super::PostUpdate(Character, PostUpdateMode);
			EndPosture = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement())->GetPosture();
			bForceNoCombine |= StartPosture != EndPosture;
		}

		virtual void PrepMoveFor(ACharacter* Character) override
		{
			Super::PrepMoveFor(Character);
			UBrCharacterMovementComponent* Movement = CastChecked<UBrCharacterMovementComponent>(Character->GetCharacterMovement());
			Movement->SetWalking(bWalk);
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

void FBrMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& Movement,
	const FClientAdjustment& Adjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement, Adjustment);
	const UBrCharacterMovementComponent& BrMovement = *CastChecked<UBrCharacterMovementComponent>(&Movement);
	Posture = BrMovement.GetServerCorrectionPosture();
	RequestedPosture = BrMovement.GetServerCorrectionRequest();
}

bool FBrMoveResponseDataContainer::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap)
{
	const bool bBaseSuccess = FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, PackageMap);
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
}

void UBrCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UBrCharacterMovementComponent, ReplicatedPosture, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UBrCharacterMovementComponent, bWantsToWalk, COND_SkipOwner);
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
	bWantsToWalk = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsToProne = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	// Prone is another low-capsule posture, encoded alongside the engine's crouch request.
	bWantsToCrouch = bWantsToCrouch || bWantsToProne;
	ClientReportedPosture = Flags >> 6;
}

bool UBrCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	const bool bRealWalk = bWantsToWalk;
	const bool bRealProne = bWantsToProne;
	const bool bRealCrouch = bWantsToCrouch;
	const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
	bWantsToWalk = bRealWalk;
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
	if (ClientReportedPosture != static_cast<uint8>(LocalPosture))
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
	}
}

void UBrCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
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
	return FBrMovementSpeed::Calculate(GetPosture(), bWantsToWalk,
		Character ? Character->GetWeaponRunSpeedMultiplier() : 1.0f,
		Settings->BaseRunSpeed, Settings->WalkSpeedMultiplier,
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
			return;
		}
		if (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy &&
			RejectedPostureRequest.IsSet() && RejectedPostureRequest.GetValue() == GetRequestedPosture())
		{
			RefreshPosture();
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
