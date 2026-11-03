#include "BrMovement.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMovementSpeedTest, "BrGame.Movement.Speed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrMovementSpeedTest::RunTest(const FString& Parameters)
{
	const auto Speed = [](EBrPosture Posture, bool bWalking, float WeaponMultiplier)
	{
		return FBrMovementSpeed::Calculate(Posture, bWalking, WeaponMultiplier, 600.0f, 0.5f, 0.2f, 0.2f);
	};
	TestEqual(TEXT("Pistol run"), Speed(EBrPosture::Standing, false, 1.0f), 600.0f);
	TestEqual(TEXT("AR run"), Speed(EBrPosture::Standing, false, 0.9f), 540.0f);
	TestEqual(TEXT("MG run"), Speed(EBrPosture::Standing, false, 0.8f), 480.0f);
	for (const float WeaponMultiplier : {1.0f, 0.9f, 0.8f})
	{
		TestEqual(TEXT("Walk ignores weapon"), Speed(EBrPosture::Standing, true, WeaponMultiplier), 300.0f);
		for (const bool bWalking : {false, true})
		{
			TestEqual(TEXT("Crouch ignores gait/weapon"), Speed(EBrPosture::Crouching, bWalking, WeaponMultiplier), 120.0f);
			TestEqual(TEXT("Prone ignores gait/weapon"), Speed(EBrPosture::Prone, bWalking, WeaponMultiplier), 120.0f);
		}
	}
	TestEqual(TEXT("Custom tuning"), FBrMovementSpeed::Calculate(EBrPosture::Prone, true, 0.8f,
		1000.0f, 0.4f, 0.3f, 0.1f), 100.0f);
	return true;
}
#endif
