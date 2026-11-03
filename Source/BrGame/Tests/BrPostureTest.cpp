#include "BrMovement.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BrCharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPostureTest, "BrGame.Movement.PostureAdjustmentAndResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrPostureTest::RunTest(const FString& Parameters)
{
	const FBrPostureAdjustment ToProne = FBrPostureAdjustment::Calculate(96.0f, 60.0f, 42.0f, 1.0f);
	TestEqual(TEXT("Low-to-low absolute mesh base"), ToProne.HeightFromStanding, 54.0f);
	TestEqual(TEXT("Low-to-low incremental smoothing delta"), ToProne.ScaledHeightDelta, 18.0f);
	const FBrPostureAdjustment Duplicate = FBrPostureAdjustment::Calculate(96.0f, 42.0f, 42.0f, 1.0f);
	TestEqual(TEXT("Duplicate keeps absolute mesh base"), Duplicate.HeightFromStanding, 54.0f);
	TestEqual(TEXT("Duplicate adds no smoothing"), Duplicate.ScaledHeightDelta, 0.0f);
	const FBrPostureAdjustment ToCrouch = FBrPostureAdjustment::Calculate(96.0f, 42.0f, 60.0f, 2.0f);
	TestEqual(TEXT("Growing low capsule scales incremental delta"), ToCrouch.ScaledHeightDelta, -36.0f);
	const FBrPostureAdjustment ToStand = FBrPostureAdjustment::Calculate(96.0f, 42.0f, 96.0f, 1.0f);
	TestEqual(TEXT("Standing restores default mesh base"), ToStand.HeightFromStanding, 0.0f);
	TestEqual(TEXT("Standing reverses prone smoothing"), ToStand.ScaledHeightDelta, -54.0f);

	UBrCharacterMovementComponent* Movement = NewObject<UBrCharacterMovementComponent>();
	for (const bool bGoodMove : {false, true})
	{
		for (const EBrPosture Posture : {EBrPosture::Standing, EBrPosture::Crouching, EBrPosture::Prone})
		{
			FBrMoveResponseDataContainer Out;
			Out.ClientAdjustment.bAckGoodMove = bGoodMove;
			Out.ClientAdjustment.TimeStamp = 1.25f;
			Out.Posture = Posture;
			Out.RequestedPosture = EBrPosture::Crouching;
			FBitWriter Writer(512, true);
			TestTrue(TEXT("Serialize posture response"), Out.Serialize(*Movement, Writer, nullptr));
			FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
			FBrMoveResponseDataContainer In;
			TestTrue(TEXT("Deserialize posture response"), In.Serialize(*Movement, Reader, nullptr));
			TestEqual(TEXT("Authoritative actual posture roundtrip"), In.Posture, Posture);
			TestEqual(TEXT("Requested posture stays separate"), In.RequestedPosture, EBrPosture::Crouching);
			TestEqual(TEXT("Response preserves move timestamp"), In.ClientAdjustment.TimeStamp, 1.25f);
			TestEqual(TEXT("Entire response consumed"), Reader.GetPosBits(), Writer.GetNumBits());
		}
	}
	FBrMoveResponseDataContainer Invalid;
	Invalid.ClientAdjustment.bAckGoodMove = true;
	Invalid.Posture = static_cast<EBrPosture>(3);
	FBitWriter InvalidWriter(512, true);
	TestFalse(TEXT("Reject invalid posture encoding"), Invalid.Serialize(*Movement, InvalidWriter, nullptr));
	return true;
}
#endif
