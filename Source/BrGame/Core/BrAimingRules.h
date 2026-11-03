#pragma once

#include "CoreMinimal.h"

/** Pure, shared input/projection rules. Sensitivity is degrees per raw mouse X unit, not per second. */
struct FBrAimingRules
{
	/** Same half-period timestamp-reset distinction as CMC, including shots around the reset boundary. */
	static bool IsMoveAfterCombatAim(float MoveTimeStamp, float AimTimeStamp, float ResetPeriod)
	{
		float Delta = MoveTimeStamp - AimTimeStamp;
		if (Delta < -ResetPeriod * 0.5f) Delta += ResetPeriod;
		else if (Delta > ResetPeriod * 0.5f) Delta -= ResetPeriod;
		return Delta > 0.f;
	}
	static float Sensitivity(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.02f, 2.0f) : 0.2f;
	}
	static float TurnYaw(float Yaw, float DeltaX, float MouseSensitivity)
	{
		return FMath::IsFinite(DeltaX) ?
			FRotator::NormalizeAxis(Yaw + DeltaX * Sensitivity(MouseSensitivity)) : Yaw;
	}
	static FVector Movement(float Yaw, FVector2D Axis)
	{
		Axis = Axis.GetClampedToMaxSize(1);
		const FRotationMatrix Rotation(FRotator(0, Yaw, 0));
		return Rotation.GetUnitAxis(EAxis::X) * Axis.Y + Rotation.GetUnitAxis(EAxis::Y) * Axis.X;
	}
	/** Horizontal displacement: its projection onto camera-up is distance * sin(pitch). */
	static float ForwardOffset(float Width, float AspectRatio, float Pitch, float ScreenFraction)
	{
		return FMath::Max(1.f, Width) / FMath::Max(0.1f, AspectRatio) *
			FMath::Clamp(ScreenFraction, 0.f, 0.4f) /
			FMath::Sin(FMath::DegreesToRadians(FMath::Clamp(Pitch, 30.f, 90.f)));
	}
};
