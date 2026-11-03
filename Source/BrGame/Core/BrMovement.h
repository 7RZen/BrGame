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
	static float Calculate(EBrPosture Posture, bool bWalking, float WeaponRunMultiplier,
		float BaseRunSpeed, float WalkMultiplier, float CrouchMultiplier, float ProneMultiplier)
	{
		switch (Posture)
		{
		case EBrPosture::Crouching: return BaseRunSpeed * CrouchMultiplier;
		case EBrPosture::Prone: return BaseRunSpeed * ProneMultiplier;
		default: return BaseRunSpeed * (bWalking ? WalkMultiplier : WeaponRunMultiplier);
		}
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
