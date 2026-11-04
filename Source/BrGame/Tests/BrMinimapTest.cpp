#include "BrMinimapRules.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMinimapTransformTest, "BrGame.Minimap.Transform",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrMinimapTransformTest::RunTest(const FString& Parameters)
{
	const FVector Center(123, -456, 0);
	for (double Yaw : {0., 37., 90., 180., 270.})
	{
		const FVector Forward = FRotator(0, Yaw, 0).Vector();
		TestTrue(
			TEXT("Camera forward is screen up"),
			FBrMinimapRules::Offset(Center + Forward * 6000, Center, Yaw, 6000).Equals(FVector2D(0, -1), 1e-6));
		const FVector Right = FRotator(0, Yaw + 90, 0).Vector();
		TestTrue(
			TEXT("Camera right is screen right"),
			FBrMinimapRules::Offset(Center + Right * 6000, Center, Yaw, 6000).Equals(FVector2D(1, 0), 1e-6));
		const FVector2D Offset(0.2, -0.4);
		TestTrue(TEXT("Shader inverse mapping"), FBrMinimapRules::Offset(
			         FBrMinimapRules::World(Offset, Center, Yaw, 6000), Center, Yaw, 6000).Equals(Offset, 1e-6));
	}
	TestTrue(TEXT("Radius boundary"), FBrMinimapRules::Inside(FVector2D(1, 0), true));
	TestFalse(TEXT("Outside radius"), FBrMinimapRules::Inside(FVector2D(0.8, 0.8), true));
	TestTrue(TEXT("Square corner"), FBrMinimapRules::Inside(FVector2D(1, 1), false));
	TestTrue(
		TEXT("Edge direction normalized"), FBrMinimapRules::Edge(FVector2D(3, 4), true).Equals(FVector2D(0.6, 0.8)));
	TestEqual(TEXT("Square edge preserves direction"), FBrMinimapRules::Edge(FVector2D(3, -4), false),
	          FVector2D(0.75, -1));
	TestEqual(TEXT("Zero indicator safe"), FBrMinimapRules::Edge(FVector2D::ZeroVector, true), FVector2D::ZeroVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMinimapFilterTest, "BrGame.Minimap.MarkerFilter",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrMinimapFilterTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Distant teammate always shown"), FBrMinimapRules::Marker(true, 2, 10, 2, 11, false, true));
	TestTrue(
		TEXT("Enemy seen only by teammate uses shared group decision"),
		FBrMinimapRules::Marker(true, 2, 10, 3, 11, true, true));
	TestFalse(TEXT("Grace-only enemy hidden"), FBrMinimapRules::Marker(true, 2, 10, 3, 11, false, true));
	TestFalse(TEXT("All vs all equal team not friends"), FBrMinimapRules::Marker(false, 2, 10, 2, 11, false, true));
	TestFalse(
		TEXT("All vs all visible enemies also omitted"), FBrMinimapRules::Marker(false, 2, 10, 2, 11, true, true));
	TestFalse(
		TEXT("All vs all different team visible enemy omitted"),
		FBrMinimapRules::Marker(false, 2, 10, 3, 11, true, true));
	TestTrue(TEXT("Self always"), FBrMinimapRules::Marker(false, 2, 10, 2, 10, false, true));
	TestFalse(TEXT("Eliminated friend omitted"), FBrMinimapRules::Marker(true, 2, 10, 2, 11, true, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMinimapFitTest, "BrGame.Minimap.FullMapFit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrMinimapFitTest::RunTest(const FString& Parameters)
{
	const FBox Bounds(FVector(-731, 217, -100), FVector(287, 3011, 200));
	const double Radius = FBrMinimapRules::FitRadius(Bounds);
	TestEqual(TEXT("Largest half extent"), Radius, 1397.);
	for (double X : {Bounds.Min.X, Bounds.Max.X})
	{
		for (double Y : {Bounds.Min.Y, Bounds.Max.Y})
		{
			TestTrue(TEXT("Non-square corners fit north-up"), FBrMinimapRules::Inside(
				         FBrMinimapRules::Offset(FVector(X, Y, 0), Bounds.GetCenter(), 0, Radius), false));
		}
	}
	return true;
}
#endif
