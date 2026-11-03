#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

struct FBrMenuRules
{
	static bool CanBind(FName Name, FKey Key, const TMap<FName, FKey>& Bindings, FString& Error)
	{
		if (!Key.IsValid() || Key.IsGamepadKey() || Key == EKeys::Tilde || Key == EKeys::Period ||
			(Key == EKeys::Escape && Name != TEXT("Br.Menu")) ||
			(Key.IsAxis1D() && (Name != TEXT("Zoom") || Key != EKeys::MouseWheelAxis)) ||
			Key.IsAxis2D() || Key.IsAxis3D())
		{
			Error = TEXT("Use a keyboard/mouse key; Escape is reserved for Menu, console keys are reserved. Wheel axis is Zoom only.");
			return false;
		}
		for (const auto& Pair : Bindings)
			if (Pair.Key != Name && Pair.Value == Key)
			{
				Error = TEXT("Already bound to ") + Pair.Key.ToString() + TEXT(". Choose another key (no swap).");
				return false;
			}
		return true;
	}
};

struct FBrUnstuckRules
{
	/** Pawn-profile sweep ignoring only components overlapping the capsule at origin. */
	static bool PathClear(const class UWorld* World, const class AActor* Player,
		const FVector& Origin, const FVector& Candidate, float Radius, float HalfHeight);
	static bool Eligible(bool bActiveMatch, bool bAdmitted, bool bAlive, bool bSuddenDeath)
	{
		return bActiveMatch && bAdmitted && bAlive && !bSuddenDeath;
	}
	static bool Cancelled(bool bEligible, bool bDamaged, bool bFired, bool bPostureChanged,
		double DistanceSquared, float Tolerance, bool bZoneChanged)
	{
		return !bEligible || bDamaged || bFired || bPostureChanged || bZoneChanged ||
			!FMath::IsFinite(DistanceSquared) || DistanceSquared > FMath::Square(Tolerance);
	}
	static bool ZoneAllowed(bool bOriginInside, bool bDestinationInside) { return bOriginInside == bDestinationInside; }
	static float Remaining(float Deadline, float Now) { return FMath::Max(0.f, Deadline - Now); }
	static float CompletionCooldown(bool bSuccess, float Previous, float Now, float Duration)
	{
		return bSuccess ? Now + Duration : Previous;
	}
};
