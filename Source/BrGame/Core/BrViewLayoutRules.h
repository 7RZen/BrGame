#pragma once

#include "CoreMinimal.h"

/** Physical-pixel layout shared by the camera and HUD; no UObject dependencies. */
struct FBrViewLayoutRules
{
	FIntRect Play;
	FIntRect Left;
	FIntRect Right;

	static float Scale(int32 Height) { return FMath::Clamp(Height / 720.f, 0.65f, 2.f); }

	/** Mix dark team colors toward white while retaining their hue identity on black. */
	static FLinearColor PanelTextColor(FLinearColor Color)
	{
		Color = Color.GetClamped();
		Color.A = 1;
		const float Luminance = Color.GetLuminance();
		return Luminance < 0.4f ? FMath::Lerp(Color, FLinearColor::White,
			(0.4f - Luminance) / (1.f - Luminance)) : Color;
	}

	static FBrViewLayoutRules Make(FIntPoint Size, float Aspect, float MinPanelWidth, float PixelScale)
	{
		Size.X = FMath::Max(1, Size.X);
		Size.Y = FMath::Max(1, Size.Y);
		Aspect = FMath::IsFinite(Aspect) ? FMath::Clamp(Aspect, 0.75f, 2.5f) : 4.f / 3.f;
		const float Panel = FMath::IsFinite(MinPanelWidth) ? FMath::Clamp(MinPanelWidth, 0.f, 600.f) : 160.f;
		PixelScale = FMath::IsFinite(PixelScale) ? FMath::Clamp(PixelScale, 0.01f, 10.f) : 1.f;
		const int32 MinSide = FMath::Min(FMath::CeilToInt(Panel * PixelScale), (Size.X - 1) / 2);
		const int32 WantedWidth = FMath::Clamp(FMath::RoundToInt(Size.Y * Aspect), 1, Size.X - 2 * MinSide);
		// Match viewport rounding, then preserve parity: equal integer bars even at odd widths.
		const int32 Side = FMath::Min((Size.X - 1) / 2, FMath::DivideAndRoundUp(Size.X - WantedWidth, 2));
		return {FIntRect(Side, 0, Size.X - Side, Size.Y),
			FIntRect(0, 0, Side, Size.Y), FIntRect(Size.X - Side, 0, Size.X, Size.Y)};
	}

	float AspectRatio() const { return static_cast<float>(Play.Width()) / Play.Height(); }
};
