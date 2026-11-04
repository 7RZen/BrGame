#pragma once

#include "CoreMinimal.h"
#include "BrMovement.h"

/** Presentation only. Never changes the capsule, movement state or mesh smoothing base. */
struct FBrPostureBlend
{
	FVector Weights = FVector(1, 0, 0);
	FVector Start = Weights;
	FVector Target = Weights;
	float Elapsed = 0;
	float Duration = 0;

	static FVector Select(EBrPosture Posture, bool bAlive)
	{
		if (!bAlive) return FVector(1, 0, 0);
		return Posture == EBrPosture::Prone ? FVector(0, 0, 1) :
			Posture == EBrPosture::Crouching ? FVector(0, 1, 0) : FVector(1, 0, 0);
	}

	void Advance(EBrPosture Posture, bool bAlive, float Delta, float CrouchSeconds, float ProneSeconds)
	{
		const FVector Next = Select(Posture, bAlive);
		if (!Next.Equals(Target))
		{
			Start = Weights;
			Duration = (Next.Z > 0 || Weights.Z > 0) ? ProneSeconds : CrouchSeconds;
			Elapsed = 0;
			Target = Next;
		}
		Elapsed += FMath::Max(0.f, Delta);
		const float T = Duration > UE_SMALL_NUMBER ? FMath::Clamp(Elapsed / Duration, 0.f, 1.f) : 1.f;
		Weights = FMath::Lerp(Start, Target, T * T * (3.f - 2.f * T));
	}
};
