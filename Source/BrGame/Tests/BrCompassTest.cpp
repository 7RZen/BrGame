#include "BrCompassRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCompassHeadingTest, "BrGame.Compass.Heading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrCompassHeadingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Negative yaw wraps west"), FBrCompassRules::NormalizeHeading(-90), 270.0);
	TestEqual(TEXT("Full rotations wrap north"), FBrCompassRules::NormalizeHeading(720), 0.0);
	TestEqual(TEXT("Negative full rotations wrap north"), FBrCompassRules::NormalizeHeading(-720), 0.0);
	TestEqual(TEXT("Multiple turns preserve heading"), FBrCompassRules::NormalizeHeading(810), 90.0);
	TestEqual(TEXT("+X is north"), FString(FBrCompassRules::CardinalLabel(0)), FString(TEXT("N")));
	TestEqual(TEXT("+Y is east"), FString(FBrCompassRules::CardinalLabel(90)), FString(TEXT("E")));
	TestEqual(TEXT("-X is south"), FString(FBrCompassRules::CardinalLabel(180)), FString(TEXT("S")));
	TestEqual(TEXT("-Y is west"), FString(FBrCompassRules::CardinalLabel(-90)), FString(TEXT("W")));
	TestEqual(TEXT("Wrapped north label"), FString(FBrCompassRules::CardinalLabel(360)), FString(TEXT("N")));
	TestEqual(TEXT("Intermediate tick has no cardinal label"),
		FString(FBrCompassRules::CardinalLabel(45)), FString());
	TestEqual(TEXT("East appears right of north"), FBrCompassRules::HeadingOffset(90, 0), 90.0);
	TestEqual(TEXT("North appears left of east"), FBrCompassRules::HeadingOffset(0, 90), -90.0);
	TestEqual(TEXT("North crosses seam on right"), FBrCompassRules::HeadingOffset(0, 359), 1.0);
	TestEqual(TEXT("West side of seam stays left"), FBrCompassRules::HeadingOffset(359, 1), -2.0);
	for (double Yaw : {0.0, 90.0, 180.0, 270.0, -90.0, 720.0})
		TestEqual(TEXT("Viewed heading stays centered"), FBrCompassRules::HeadingOffset(Yaw, Yaw), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrCompassCoordinateTest, "BrGame.Compass.Coordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrCompassCoordinateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("100 cm is one meter"), FBrCompassRules::Meters(100), 1.0);
	TestEqual(TEXT("Conversion preserves negative position"), FBrCompassRules::Meters(-12345), -123.45);
	TestEqual(TEXT("Positive coordinates signed, one decimal"), FBrCompassRules::FormatCoordinate(12346), FString(TEXT("+123.5 m")));
	TestEqual(TEXT("Negative coordinates signed, one decimal"), FBrCompassRules::FormatCoordinate(-12346), FString(TEXT("-123.5 m")));
	TestEqual(TEXT("World origin"), FBrCompassRules::FormatCoordinate(0), FString(TEXT("+0.0 m")));
	TestEqual(TEXT("Suppress negative rounded zero"), FBrCompassRules::FormatCoordinate(-1), FString(TEXT("+0.0 m")));
	TestEqual(TEXT("Negative tenth preserved"), FBrCompassRules::FormatCoordinate(-10), FString(TEXT("-0.1 m")));
	return true;
}
#endif
