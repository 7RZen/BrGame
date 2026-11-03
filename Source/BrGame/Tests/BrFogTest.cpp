#include "BrFogRules.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFogVisibilityTest, "BrGame.Fog.Visibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrFogVisibilityTest::RunTest(const FString& Parameters)
{
	const double Radius = 321, Now = 10, Grace = 0.4;
	TestTrue(TEXT("Inside with LOS"), FBrFogRules::Visible(false, Radius * Radius, Radius, true, Now, -100, Grace));
	TestFalse(TEXT("Blocked"), FBrFogRules::Visible(false, 1, Radius, false, Now, Now, Grace));
	TestTrue(TEXT("Friendly regardless of walls/distance"), FBrFogRules::Visible(true, 1e12, Radius, false, Now, -100, Grace));
	TestFalse(TEXT("Outside never seen"), FBrFogRules::Visible(false, FMath::Square(Radius * 1.05), Radius, true, Now, -100, Grace));
	TestTrue(TEXT("Distance hysteresis"), FBrFogRules::Visible(false, FMath::Square(Radius * 1.05), Radius, true, Now, Now - Grace / 2, Grace));
	TestFalse(TEXT("Grace expires"), FBrFogRules::Visible(false, FMath::Square(Radius * 1.05), Radius, true, Now, Now - Grace * 2, Grace));
	TestFalse(TEXT("Grace never extends through walls"), FBrFogRules::Visible(false, 1, Radius, false, Now, Now, Grace));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFogGroupsTest, "BrGame.Fog.GroupsAndActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrFogGroupsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Shared team"), FBrFogRules::Group(true, 2, 10), FBrFogRules::Group(true, 2, 99));
	TestNotEqual(TEXT("All vs all isolates equal team IDs"), FBrFogRules::Group(false, 2, 10), FBrFogRules::Group(false, 2, 99));
	TestNotEqual(TEXT("Teams distinct"), FBrFogRules::Group(true, 2, 10), FBrFogRules::Group(true, 3, 99));
	const int64 Own = FBrFogRules::Group(true, 2, 10);
	const int64 Target = FBrFogRules::Group(true, 3, 99);
	TestEqual(TEXT("Spectator inherits target group"), FBrFogRules::Viewer(true, Own, Target), Target);
	TestEqual(TEXT("Player retains own group"), FBrFogRules::Viewer(false, Own, Target), Own);
	TestTrue(TEXT("Grenade at visible position"), FBrFogRules::Grenade(true));
	TestFalse(TEXT("Unseen grenade withheld regardless of thrower visibility"), FBrFogRules::Grenade(false));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFogMappingTest, "BrGame.Fog.WorldUV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrFogMappingTest::RunTest(const FString& Parameters)
{
	const FBox Bounds(FVector(-731, 217, -100), FVector(287, 3011, 200));
	TestEqual(TEXT("Min"), FBrFogRules::UV(Bounds.Min, Bounds), FVector2D::ZeroVector);
	TestEqual(TEXT("Max"), FBrFogRules::UV(Bounds.Max, Bounds), FVector2D(1, 1));
	TestEqual(TEXT("Center"), FBrFogRules::UV(Bounds.GetCenter(), Bounds), FVector2D(0.5, 0.5));
	const FVector Position = Bounds.Min + Bounds.GetSize() * 0.3;
	// A rotating orthographic camera transforms screen coordinates, not this world mapping.
	for (float Yaw : {0.f, 37.f, 180.f, 299.f})
	{
		const FRotator Rotation(0, Yaw, 0);
		const FVector Restored = Bounds.GetCenter() + Rotation.RotateVector(
			Rotation.UnrotateVector(Position - Bounds.GetCenter()));
		TestTrue(TEXT("View rotation invariant"), FBrFogRules::UV(Restored, Bounds).Equals(FVector2D(0.3, 0.3), 1e-6));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFogObstacleTest, "BrGame.Fog.ObstaclePresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrFogObstacleTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Unknown group stays black"), FBrFogRules::ObstacleStencil(false, 0, 2), 0);
	TestEqual(TEXT("Hit blocker is lit"), FBrFogRules::ObstacleStencil(true, 0, 2), 2);
	TestEqual(TEXT("Last seen fades"), FBrFogRules::ObstacleStencil(true, 1, 2), 10);
	TestEqual(TEXT("Explored stays dimmed"), FBrFogRules::ObstacleStencil(true, 100, 2), 17);
	TestEqual(TEXT("Future timestamps clamp"), FBrFogRules::ObstacleStencil(true, -1, 2), 2);
	TestEqual(TEXT("Zero delay safe"), FBrFogRules::ObstacleStencil(true, 1, 0), 17);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrFogGroundTest, "BrGame.Fog.GroundExclusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrFogGroundTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Ground plane"), FBrFogRules::GroundLike(FVector(10000, 10000, 10)));
	TestTrue(TEXT("Floor slab"), FBrFogRules::GroundLike(FVector(500, 400, 25)));
	TestTrue(TEXT("Thick road slab"), FBrFogRules::GroundLike(FVector(10000, 400, 40)));
	TestFalse(TEXT("Thin upright wall"), FBrFogRules::GroundLike(FVector(500, 10, 150)));
	TestFalse(TEXT("Low cover wall"), FBrFogRules::GroundLike(FVector(300, 10, 50)));
	TestFalse(TEXT("Crate"), FBrFogRules::GroundLike(FVector(70, 70, 50)));
	TestFalse(TEXT("Container"), FBrFogRules::GroundLike(FVector(800, 300, 150)));
	return true;
}
#endif
