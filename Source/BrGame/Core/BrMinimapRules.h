#pragma once
#include "CoreMinimal.h"
#include "BrFogRules.h"

/** All distances are world centimetres; map offsets are screen-right/screen-down. */
struct FBrMinimapRules
{
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
