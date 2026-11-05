#pragma once

#include "CoreMinimal.h"

/** Compass convention matches the minimap: +X north, +Y east, positive yaw clockwise. */
struct FBrCompassRules
{
	static double NormalizeHeading(double Yaw)
	{
		const double Heading = FMath::Fmod(Yaw, 360.0);
		return Heading < 0 ? Heading + 360.0 : Heading;
	}

	/** Signed strip offset: headings clockwise from the view appear to its right. */
	static double HeadingOffset(double Bearing, double ViewYaw)
	{
		return NormalizeHeading(Bearing - ViewYaw + 180.0) - 180.0;
	}

	/** Only cardinal ticks have labels; intermediate ticks remain unlabelled. */
	static const TCHAR* CardinalLabel(double Bearing)
	{
		const double Heading = NormalizeHeading(Bearing);
		if (Heading == 0.0) return TEXT("N");
		if (Heading == 90.0) return TEXT("E");
		if (Heading == 180.0) return TEXT("S");
		if (Heading == 270.0) return TEXT("W");
		return TEXT("");
	}

	static double Meters(double Centimeters)
	{
		return Centimeters / 100.0;
	}

	static FString FormatCoordinate(double Centimeters)
	{
		// Avoid a misleading "-0.0" when a tiny negative coordinate rounds to zero.
		const double Value = Meters(Centimeters);
		return FString::Printf(TEXT("%+.1f m"), FMath::Abs(Value) < 0.05 ? 0.0 : Value);
	}
};
