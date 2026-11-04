#pragma once
#include "CoreMinimal.h"

class AActor;
class UWorld;

/** Shared surface policy. A fog tag alone does not distinguish floors from raised caps. */
struct FBrPlayableGroundRules
{
	static bool StableSupport(bool bGroundObjectType, bool bSimulatingPhysics, double SpeedSquared)
	{
		return bGroundObjectType && !bSimulatingPhysics && FMath::IsFinite(SpeedSquared) &&
			SpeedSquared >= 0 && SpeedSquared <= 1; // at rest (<= 1 cm/s), regardless of authoring mobility
	}
	static bool AcceptSurface(float NormalZ, bool bStaticGround, bool bGroundTagged, bool bRequireTaggedStaticFloor)
	{
		return FMath::IsFinite(NormalZ) && NormalZ >= 0.7f &&
			(!bRequireTaggedStaticFloor || (bStaticGround && bGroundTagged));
	}
	static bool SupportsSurfaceFootprint(const FVector& WorldExtent, const FVector& ScaledLocalExtent,
		const FVector& LocalNormal, float RequiredHalfWidth)
	{
		if (LocalNormal.ContainsNaN() || LocalNormal.IsNearlyZero()) return false;
		const FVector Normal = LocalNormal.GetAbs();
		// Exclude the local axis facing out of the hit surface, not necessarily
		// local Z: a broad floor mesh can be authored on its side.
		const FVector2D PlaneExtent = Normal.X >= Normal.Y && Normal.X >= Normal.Z ?
			FVector2D(ScaledLocalExtent.Y, ScaledLocalExtent.Z) :
			(Normal.Y >= Normal.Z ? FVector2D(ScaledLocalExtent.X, ScaledLocalExtent.Z) :
				FVector2D(ScaledLocalExtent.X, ScaledLocalExtent.Y));
		return SupportsFootprint(FVector2D(WorldExtent.X, WorldExtent.Y), RequiredHalfWidth) &&
			SupportsFootprint(PlaneExtent, RequiredHalfWidth);
	}
	/** Crates need a broad floor supporting their scatter footprint, not a small tagged prop. */
	static bool SupportsFootprint(const FVector2D& HalfExtent, float RequiredHalfWidth)
	{
		return FMath::IsFinite(RequiredHalfWidth) && RequiredHalfWidth >= 0 &&
			!HalfExtent.ContainsNaN() && HalfExtent.X >= RequiredHalfWidth && HalfExtent.Y >= RequiredHalfWidth;
	}
};

struct FBrGroundQuery
{
	float Radius = 42;
	float HalfHeight = 96;
	float TraceUp = 1000;
	float TraceDown = 2000;
	bool bRequireTaggedStaticFloor = false;
	bool bUsePawnProfile = false;
	/** Zero keeps the legacy zone policy. Crates/scatter require a broad supporting floor. */
	float MinFloorHalfWidth = 0;
};

/** Zone fallback and crate placement share trace/slope/capsule verification; no nav dependency. */
struct BRGAME_API FBrPlayableGround
{
	static bool Find(UWorld* World, const FVector& Candidate, FVector& Floor,
		const FBrGroundQuery& Settings, const AActor* Ignore = nullptr);
};
