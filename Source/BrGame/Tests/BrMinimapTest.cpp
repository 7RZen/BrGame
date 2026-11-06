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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMinimapMarkerSizeTest, "BrGame.Minimap.BoundedTinyMapMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrMinimapMarkerSizeTest::RunTest(const FString& Parameters)
{
	for (float Size : {1.f, 10.f, 20.f, 140.f, 220.f, 1728.f})
		for (float Reference : {2.f, 5.f, 16.f})
			for (float Scale : {0.65f, 1.f, 2.f})
			{
				const float Marker = FBrMinimapRules::MarkerPixels(Size, Reference, Scale);
				const double Inset = FBrMinimapRules::MarkerInset(Size, Marker);
				TestTrue(TEXT("Positive marker fits map"), Marker > 0 && Marker <= Size / 12.f);
				TestTrue(TEXT("Strictly positive positioning inset"), FMath::IsFinite(Inset) && Inset >= 0.49);
				for (bool bCircular : {false, true})
					for (FVector2D Offset : {FVector2D::ZeroVector, FVector2D(1, 0), FVector2D(0, -1), FVector2D(1, 1)})
					{
						const FVector2D Icon = FBrMinimapRules::InsetOffset(Offset, bCircular, Inset);
						TestFalse(TEXT("Finite icon position"), Icon.ContainsNaN());
						TestTrue(TEXT("No flipped icon direction"), FVector2D::DotProduct(Icon, Offset) >= 0);
						const double Extent = bCircular ? Icon.Size() : FMath::Max(FMath::Abs(Icon.X), FMath::Abs(Icon.Y));
						TestTrue(TEXT("Icon plus stroke/arrow envelope fits map"),
							Extent * Size / 2 + 3 * Marker <= Size / 2 + UE_KINDA_SMALL_NUMBER);
					}
			}
	return true;
}
#endif
