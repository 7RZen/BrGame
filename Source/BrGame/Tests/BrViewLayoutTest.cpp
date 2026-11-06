#include "BrViewLayoutRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrViewLayoutTest, "BrGame.ViewLayout.PanelsAndEngineConstraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrViewLayoutTest::RunTest(const FString& Parameters)
{
	for (FIntPoint Size : {FIntPoint(1920, 1080), FIntPoint(2560, 1080), FIntPoint(1280, 960), FIntPoint(1280, 1024),
		FIntPoint(1080, 1920), FIntPoint(1279, 719), FIntPoint(3840, 2160), FIntPoint(50, 90),
		FIntPoint(1, 1), FIntPoint(2, 2), FIntPoint(200, 720), FIntPoint(82, 720)})
	{
		const float Scale = FBrViewLayoutRules::Scale(Size.Y);
		const auto Layout = FBrViewLayoutRules::Make(Size, 4.f / 3.f, 160, Scale);
		TestEqual(TEXT("Equal integer side bars"), Layout.Left.Width(), Layout.Right.Width());
		TestEqual(TEXT("Left touches play"), Layout.Left.Max.X, Layout.Play.Min.X);
		TestEqual(TEXT("Play touches right"), Layout.Play.Max.X, Layout.Right.Min.X);
		TestEqual(TEXT("No gaps or overlap"), Layout.Left.Width() + Layout.Play.Width() + Layout.Right.Width(), Size.X);
		TestTrue(TEXT("Nonempty play rectangle"), Layout.Play.Width() > 0 && Layout.Play.Height() == Size.Y);
		TestTrue(TEXT("Minimum panel width when physically possible"),
			Layout.Left.Width() >= FMath::Min(FMath::CeilToInt(160 * Scale), (Size.X - 1) / 2));
		// UE 5.8's public constraint implementation, not a duplicate in test code.
		const FIntRect EngineRect = FViewport::CalculateViewExtents(Layout.AspectRatio(),
			static_cast<float>(Size.X) / Size.Y, FIntRect(0, 0, Size.X, Size.Y), Size);
		TestEqual(TEXT("Camera engine constraint matches HUD layout"), EngineRect, Layout.Play);
	}
	const auto HD = FBrViewLayoutRules::Make({1920, 1080}, 4.f / 3.f, 160, 1.5f);
	TestEqual(TEXT("1080p 4:3 width"), HD.Play.Width(), 1440);
	TestEqual(TEXT("1080p panel width"), HD.Left.Width(), 240);
	const auto Narrow = FBrViewLayoutRules::Make({1280, 1024}, 4.f / 3.f, 160, FBrViewLayoutRules::Scale(1024));
	TestTrue(TEXT("Narrow views prioritize panels over aspect"), Narrow.AspectRatio() < 4.f / 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPanelColorTest, "BrGame.ViewLayout.LegibleTeamColors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPanelColorTest::RunTest(const FString& Parameters)
{
	for (FLinearColor Color : {FLinearColor::Black, FLinearColor::Blue, FLinearColor::Red,
		FLinearColor::Green, FLinearColor::White, FLinearColor(0.01f, 0.02f, 0.04f)})
	{
		const FLinearColor Text = FBrViewLayoutRules::PanelTextColor(Color);
		TestTrue(TEXT("Team text meets luminance floor on black"), Text.GetLuminance() >= 0.4f - UE_KINDA_SMALL_NUMBER);
		TestEqual(TEXT("Team text opaque"), Text.A, 1.f);
		if (Color.GetLuminance() >= 0.4f)
			TestEqual(TEXT("Already legible colors unchanged"), Text, Color);
	}
	const FLinearColor Blue = FBrViewLayoutRules::PanelTextColor(FLinearColor::Blue);
	TestTrue(TEXT("Dark blue remains visibly blue"), Blue.B > Blue.R && Blue.B > Blue.G);
	return true;
}
#endif
