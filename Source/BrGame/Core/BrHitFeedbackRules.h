#pragma once
#include "CoreMinimal.h"

/** Presentation-only math. Never writes movement, aim or control rotation. */
struct FBrHitFeedbackRules
{
	static bool DamageEligible(float LostHealth, bool bAlive, bool bInvulnerable)
	{
		return FMath::IsFinite(LostHealth) && LostHealth > 0 && bAlive && !bInvulnerable;
	}
	static bool RemoteEligible(bool bVisible, bool bOwner, bool bAlive)
	{
		return bVisible && !bOwner && bAlive;
	}
	static float Envelope(float Age, float Duration)
	{
		return Duration > 0 ? FMath::Clamp(1.f - Age / Duration, 0.f, 1.f) : 0;
	}
	static float Shake(float Age, float Duration, float Amplitude, float Seed)
	{
		return FMath::Max(0.f, Amplitude) * FMath::Clamp(Seed, -1.f, 1.f) *
			Envelope(Age, Duration) * FMath::Cos(Age * 60.f);
	}
	/** Ground-plane projection: right maps to +X, forward to -Y, compressed by pitch. */
	static FVector2D ScreenDirection(FVector Delta, float Yaw, float Pitch)
	{
		const FVector Local = FRotator(0, -Yaw, 0).RotateVector(Delta);
		return FVector2D(Local.Y, -Local.X * FMath::Sin(FMath::DegreesToRadians(Pitch))).GetSafeNormal();
	}
	static FVector2D ScreenDirection(FVector Delta, const FRotator& Camera)
	{
		Delta.Z = 0;
		const FVector Local = Camera.UnrotateVector(Delta);
		return FVector2D(Local.Y, -Local.Z).GetSafeNormal();
	}
	/** Intersect the ray starting at the projected pawn (not screen centre) with an inset rectangle. */
	static FVector2D EdgePoint(FVector2D Pawn, FVector2D Direction, FVector2D Size, float Inset)
	{
		float T = TNumericLimits<float>::Max();
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			if (FMath::Abs(Direction[Axis]) > SMALL_NUMBER)
			{
				const float Edge = Direction[Axis] > 0 ? Size[Axis] - Inset : Inset;
				T = FMath::Min(T, FMath::Max(0.f, (Edge - Pawn[Axis]) / Direction[Axis]));
			}
		}
		return T == TNumericLimits<float>::Max() ? FVector2D(Size.X / 2, Inset) : Pawn + Direction * T;
	}
};
