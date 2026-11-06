#pragma once
#include "CoreMinimal.h"
#include "BrFogRules.h"

/** All distances are world centimetres; map offsets are screen-right/screen-down. */
struct FBrMinimapRules
{
	/** Bound icons/strokes and reserve at least half the radius for positioning. */
	static float MarkerPixels(float MapSize, float ReferenceSize, float Scale)
	{
		return FMath::Min(FMath::Clamp(ReferenceSize, 2.f, 16.f) * FMath::Max(0.f, Scale),
			FMath::Max(0.f, MapSize) / 12.f);
	}
	static double MarkerInset(float MapSize, float MarkerSize)
	{
		return FMath::Clamp(1.0 - 6.0 * MarkerSize / FMath::Max(MapSize, UE_SMALL_NUMBER), 0.01, 1.0);
	}
	static FVector2D InsetOffset(const FVector2D& Offset, bool bCircular, double Inset)
	{
		Inset = FMath::Clamp(Inset, 0.01, 1.0);
		return Inside(Offset / Inset, bCircular) ? Offset : Edge(Offset, bCircular) * Inset;
	}

	static FVector2D Offset(const FVector& World, const FVector& Center, double Yaw, double Radius)
	{
		const FVector Local = FRotator(0, Yaw, 0).UnrotateVector(World - Center);
		return FVector2D(Local.Y, -Local.X) / FMath::Max(Radius, 1.0);
	}
	static FVector World(const FVector2D& Offset, const FVector& Center, double Yaw, double Radius)
	{
		return Center + FRotator(0, Yaw, 0).RotateVector(FVector(-Offset.Y, Offset.X, 0) * Radius);
	}
	static bool Inside(const FVector2D& Offset, bool bCircular)
	{
		return bCircular ? Offset.SizeSquared() <= 1 : FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y)) <= 1;
	}
	static FVector2D Edge(const FVector2D& Offset, bool bCircular)
	{
		const double Length = bCircular ? Offset.Size() : FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y));
		return Length > UE_SMALL_NUMBER ? Offset / Length : FVector2D::ZeroVector;
	}
	static double FitRadius(const FBox& Bounds)
	{
		return FMath::Max(1.0, FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y));
	}
	static bool Marker(bool bGrouped, int32 ViewerTeam, int32 ViewerId, int32 TargetTeam,
		int32 TargetId, bool bGroupVisible, bool bAlive)
	{
		return bAlive && (FBrFogRules::Group(bGrouped, ViewerTeam, ViewerId) ==
			FBrFogRules::Group(bGrouped, TargetTeam, TargetId) || (bGrouped && bGroupVisible));
	}
};
