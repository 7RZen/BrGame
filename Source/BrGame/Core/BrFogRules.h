#pragma once
#include "CoreMinimal.h"

/** Pure policy shared by replication, local rendering and tests. */
struct FBrFogRules
{
	template<typename T>
	static T Viewer(bool bSpectating, T Own, T Target) { return bSpectating ? Target : Own; }
	static int64 Group(bool bGrouped, int32 Team, int32 Player)
	{
		return bGrouped && Team >= 0 ? int64(Team) : -int64(Player) - 1;
	}
	static bool Visible(bool bFriendly, double DistanceSquared, double Radius, bool bLOS,
		double Now, double LastSeen, double Grace)
	{
		if (bFriendly) return true;
		// Never retain vision through a wall. Grace only softens the distance boundary.
		return bLOS && (DistanceSquared <= FMath::Square(Radius) ||
			(DistanceSquared <= FMath::Square(Radius * 1.1) && Now - LastSeen <= Grace));
	}
	static bool Grenade(bool bPositionVisible) { return bPositionVisible; }
	/** World-space half extents: flat slabs cannot expose the floor via stencil.
	 * BrFogGround component/actor tags additionally exclude slopes/authored ground. */
	static bool GroundLike(const FVector& Extent)
	{
		return Extent.Z <= 25 ||
			(Extent.Z <= 100 && Extent.Z <= 0.1 * FMath::Min(Extent.X, Extent.Y));
	}
	/** Stencil 1 is friends; 2..17 encode static obstacle last-seen fade.
	 * Zero means not explored by this group. No dynamic gameplay actor uses this. */
	static int32 ObstacleStencil(bool bKnown, double Age, double Delay)
	{
		return bKnown ? 2 + FMath::RoundToInt(15 * FMath::Clamp(Age / FMath::Max(Delay, 0.01), 0.0, 1.0)) : 0;
	}
	static FVector2D UV(const FVector& Position, const FBox& Bounds)
	{
		const FVector Size = Bounds.GetSize();
		return FVector2D((Position.X - Bounds.Min.X) / Size.X, (Position.Y - Bounds.Min.Y) / Size.Y);
	}
};
