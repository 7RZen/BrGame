#pragma once

#include "CoreMinimal.h"
#include "BrMovement.generated.h"

UENUM(BlueprintType)
enum class EBrPosture : uint8
{
	Standing,
	Crouching,
	Prone
};

struct FBrMovementSpeed
{
	static float Calculate(EBrPosture Posture, bool bSprinting, float WeaponRunMultiplier,
		float BaseRunSpeed, float SprintMultiplier, float CrouchMultiplier, float ProneMultiplier)
	{
		switch (Posture)
		{
		case EBrPosture::Crouching: return BaseRunSpeed * CrouchMultiplier;
		case EBrPosture::Prone: return BaseRunSpeed * ProneMultiplier;
		default: return BaseRunSpeed * WeaponRunMultiplier * (bSprinting ? SprintMultiplier : 1.f);
		}
	}
};

/** Deterministic move-time rules shared by prediction, authority and tests. Units are seconds of sprint. */
struct FBrSprintState
{
	float Stamina = 5;
	float RestTime = 0;
	bool bActive = false;

	static bool IsForward(const FVector& Acceleration, float FacingYaw, float HalfAngle)
	{
		return !Acceleration.IsNearlyZero() && FVector::DotProduct(Acceleration.GetSafeNormal2D(),
			FRotator(0, FacingYaw, 0).Vector()) >= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngle, 0.f, 89.f)));
	}

	void Advance(float Delta, bool bAllowed, float Capacity, float MinimumStart, float RegenDelay, float RegenDuration)
	{
		bActive = bAllowed && Stamina > UE_KINDA_SMALL_NUMBER &&
			(bActive || Stamina + UE_KINDA_SMALL_NUMBER >= FMath::Min(Capacity, MinimumStart));
		if (bActive)
		{
			Stamina = FMath::Max(0.f, Stamina - Delta);
			RestTime = 0;
			// This move used its remaining sprint energy. The next move must pass the minimum-start gate.
			if (Stamina <= UE_KINDA_SMALL_NUMBER) bActive = false;
		}
		else
		{
			const float RegenDelta = FMath::Max(0.f, RestTime + Delta - RegenDelay) -
				FMath::Max(0.f, RestTime - RegenDelay);
			RestTime = FMath::Min(RegenDelay, RestTime + Delta);
			Stamina = FMath::Min(Capacity, Stamina + RegenDelta * Capacity / FMath::Max(0.01f, RegenDuration));
		}
	}

	static bool CanFire(bool bSprinting, double Now, double ReadyTime, float Grace)
	{
		return !bSprinting && Now + Grace >= ReadyTime;
	}
};

struct FBrPostureAdjustment
{
	float HeightFromStanding;
	float ScaledHeightDelta;

	static FBrPostureAdjustment Calculate(float StandingHeight, float PreviousHeight, float NewHeight, float Scale)
	{
		return {StandingHeight - NewHeight, (PreviousHeight - NewHeight) * Scale};
	}
};

/** Move-time state: no world-clock deadlines, so CMC timestamp resets need no rebasing. */
struct FBrSlideState
{
	bool bActive = false;
	float Remaining = 0;
	float Cooldown = 0;
	float InitialSpeed = 0;
	float FinalSpeed = 0;
	float Duration = 0;
	float StartMoveTime = 0; // diagnostic native timestamp, never a permission/deadline
	FVector Direction = FVector::ZeroVector;

	float Speed(float TimeAhead = 0) const
	{
		return FMath::Lerp(FinalSpeed, InitialSpeed,
			FMath::Clamp((Remaining - TimeAhead) / FMath::Max(.01f, Duration), 0.f, 1.f));
	}
	static bool Eligible(float Speed, float RunSpeed, const FVector& Input, float Yaw, float Cone,
		float Rest, EBrPosture Posture, bool Alive, bool Grounded, bool Blocked)
	{
		return Alive && Grounded && !Blocked && Posture != EBrPosture::Prone && Rest <= 0 &&
			Speed + UE_KINDA_SMALL_NUMBER >= RunSpeed &&
			FBrSprintState::IsForward(Input, Yaw, Cone);
	}
};
