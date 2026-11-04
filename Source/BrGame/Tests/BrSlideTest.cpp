#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrDeveloperSettings.h"
#include "BrCombatMath.h"
#include "BrLifeComponent.h"
#include "BrPlayerController.h"
#include "BrGameState.h"
#include "BrInputConfig.h"
#include "InputMappingContext.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/BitWriter.h"
#include "Serialization/BitReader.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace
{
	struct FSlideFixture
	{
		UWorld* World;
		ABrCharacter* Pawn;
		UBrCharacterMovementComponent* Move;
		FSlideFixture()
		{
			const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			auto* GS = World->SpawnActor<ABrGameState>();
			GS->bLobby = false;
			World->SetGameState(GS);
			Pawn = World->SpawnActor<ABrCharacter>();
			auto* PC = World->SpawnActor<ABrPlayerController>();
			PC->Player = NewObject<ULocalPlayer>(GEngine);
			PC->Possess(Pawn);
			PC->SetControlRotation(FRotator::ZeroRotator);
			Move = Pawn->GetBrMovement();
			Move->SetMovementMode(MOVE_Walking);
			Move->Velocity = FVector(780, 0, 0);
		}
		~FSlideFixture()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSlideRulesTest, "BrGame.Slide.EligibilityAndCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSlideRulesTest::RunTest(const FString& Parameters)
{
	const auto Eligible = [](float Speed, float Angle, float Cooldown, EBrPosture Posture, bool Alive, bool Ground, bool Block)
	{
		return FBrSlideState::Eligible(Speed, 600, FRotator(0, Angle, 0).Vector(), 0, 50, Cooldown, Posture, Alive, Ground, Block);
	};
	TestTrue(TEXT("Run threshold"), Eligible(600, 0, 0, EBrPosture::Standing, true, true, false));
	TestTrue(TEXT("Sprint diagonal"), Eligible(780, 45, 0, EBrPosture::Standing, true, true, false));
	TestFalse(TEXT("Too slow"), Eligible(599, 0, 0, EBrPosture::Standing, true, true, false));
	TestFalse(TEXT("Strafe"), Eligible(780, 90, 0, EBrPosture::Standing, true, true, false));
	TestFalse(TEXT("Outside forward cone"), Eligible(780, 51, 0, EBrPosture::Standing, true, true, false));
	TestFalse(TEXT("Cooldown"), Eligible(780, 0, .1f, EBrPosture::Standing, true, true, false));
	TestFalse(TEXT("Prone"), Eligible(780, 0, 0, EBrPosture::Prone, true, true, false));
	TestFalse(TEXT("Downed"), Eligible(780, 0, 0, EBrPosture::Standing, false, true, false));
	TestFalse(TEXT("Airborne"), Eligible(780, 0, 0, EBrPosture::Standing, true, false, false));
	TestFalse(TEXT("Menu/input block"), Eligible(780, 0, 0, EBrPosture::Standing, true, true, true));
	const auto* S = UBrDeveloperSettings::Get();
	FBrSlideState Curve;
	Curve.InitialSpeed = 780 * S->SlideSpeedMultiplier;
	Curve.FinalSpeed = S->BaseRunSpeed * S->CrouchSpeedMultiplier;
	Curve.Duration = Curve.Remaining = S->SlideDuration;
	TestEqual(TEXT("1.2 initial"), Curve.Speed(), 936.f);
	TestTrue(TEXT("Linear midpoint"), FMath::IsNearlyEqual(Curve.Speed(.4f), 528.f));
	TestEqual(TEXT("Crouch final"), Curve.Speed(.8f), 120.f);
	TestTrue(TEXT("Sprint slide distance 4.224m"), FMath::IsNearlyEqual((Curve.Speed() + Curve.Speed(.8f)) * .4f, 422.4f));
	const auto* Input = S->InputConfig.LoadSynchronous();
	if (TestNotNull(TEXT("Slide input"), Input ? Input->SlideAction.Get() : nullptr))
	{
		int32 Rows = 0;
		for (const auto& Mapping : Input->DefaultMappingContext->GetMappings())
			if (Mapping.Action == Input->SlideAction)
			{
				++Rows;
				TestEqual(TEXT("Rebindable row"), Mapping.GetMappingName(), FName(TEXT("Br.Slide")));
				TestEqual(TEXT("Ctrl"), Mapping.Key, EKeys::LeftControl);
			}
		TestEqual(TEXT("One binding"), Rows, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSlideIntegrationTest, "BrGame.Slide.DirectionPostureAndCancels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSlideIntegrationTest::RunTest(const FString& Parameters)
{
	FSlideFixture F;
	auto* M = F.Move;
	M->Acceleration = FVector(1000, 0, 0);
	M->SetSprinting(true);
	M->UpdateSprint(.01f);
	TestTrue(TEXT("Fixture sprinting"), M->IsSprinting());
	const float Energy = M->GetSprintState().Stamina;
	M->RequestSlide();
	M->UpdateCharacterStateBeforeMovement(.01f);
	TestTrue(TEXT("Started slide"), M->IsSliding());
	TestFalse(TEXT("Sprint cancelled"), M->IsSprinting());
	TestFalse(TEXT("Sprint intent cleared"), M->WantsSprint());
	TestTrue(TEXT("Immediate fire/throw permitted"), M->CanSprintFire());
	TestEqual(TEXT("Crouch combat posture"), F.Pawn->GetPosture(), EBrPosture::Crouching);
	TestEqual(TEXT("No stamina cost"), M->GetSprintState().Stamina, Energy);
	M->Acceleration = FVector(0, 1000, 0);
	F.Pawn->GetController()->SetControlRotation(FRotator(0, 90, 0));
	float Distance = 0;
	for (int32 I = 0; I < 8; ++I)
	{
		M->CalcVelocity(.1f, 8, false, 2048);
		TestTrue(TEXT("Movement direction locked while facing turns"), M->Velocity.GetSafeNormal2D().Equals(FVector::ForwardVector));
		Distance += M->Velocity.Size2D() * .1f;
	}
	TestTrue(TEXT("Integrated curve distance"), FMath::IsNearlyEqual(Distance, 422.4f, .01f));
	M->UpdateCharacterStateAfterMovement(.1f);
	TestFalse(TEXT("Duration ends slide"), M->IsSliding());
	TestEqual(TEXT("Ends crouched"), M->GetPosture(), EBrPosture::Crouching);
	TestEqual(TEXT("One second cooldown from END"), M->Slide.Cooldown, 1.f);
	TestEqual(TEXT("Sliding damage uses crouch multiplier"), FBrCombatMath::Damage(100, F.Pawn->GetPosture(),
		false, false, 1, .75f, .5f), 75.f);
	M->Velocity = FVector(780, 0, 0);
	M->Acceleration = FVector(0, 1000, 0);
	M->RequestSlide();
	M->UpdateSlideBeforeMovement(.01f);
	TestFalse(TEXT("Cooldown rejects another press"), M->IsSliding());
	M->Slide.Cooldown = 0;
	M->RequestSlide();
	M->UpdateSlideBeforeMovement(.01f);
	TestTrue(TEXT("Reeligible"), M->IsSliding());
	M->RequestPosture(EBrPosture::Prone);
	TestFalse(TEXT("Prone request cancels immediately"), M->IsSliding());
	const auto* S = UBrDeveloperSettings::Get();
	TestEqual(TEXT("Hit chance uses actual crouching shooter/target"), FBrCombatMath::HitChance(EBrPosture::Crouching,
		F.Pawn->GetPosture(), S->StandingHitChance, S->CrouchingHitChance, S->ProneHitChance), S->CrouchingHitChance.Crouching);
	M->bWantsToProne = false;
	M->Slide.bActive = true;
	M->Slide.Remaining = .5f;
	M->Velocity = FVector::ZeroVector;
	M->UpdateCharacterStateAfterMovement(.01f);
	TestFalse(TEXT("Wall velocity loss ends"), M->IsSliding());
	M->Slide.bActive = true;
	M->PrepareSprintReplay(0, 0);
	M->RestoreInputBlock(true);
	M->UpdateSlideBeforeMovement(.01f);
	TestFalse(TEXT("Historical menu input block ends"), M->IsSliding());
	M->RestoreInputBlock(false);
	M->FinishSprintReplay();
	M->Slide.bActive = true;
	M->SetMovementMode(MOVE_Falling);
	M->UpdateSlideBeforeMovement(.01f);
	TestFalse(TEXT("Falling ends"), M->IsSliding());
	M->Slide.bActive = true;
	F.Pawn->GetLife()->State = EBrLifeState::Downed;
	M->UpdateCharacterStateBeforeMovement(.01f);
	TestFalse(TEXT("Downed ends"), M->IsSliding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSlideReplayTest, "BrGame.Slide.SavedMoveRoundTripReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSlideReplayTest::RunTest(const FString& Parameters)
{
	FSlideFixture F;
	auto* M = F.Move;
	M->Acceleration = FVector(1000, 0, 0);
	M->RequestSlide();
	auto* Prediction = M->GetPredictionData_Client_Character();
	FSavedMovePtr Start = Prediction->AllocateNewMove();
	Start->SetMoveFor(F.Pawn, .1f, M->Acceleration, *Prediction);
	M->UpdateCharacterStateBeforeMovement(.1f);
	M->CalcVelocity(.1f, 8, false, 2048);
	Start->PostUpdate(F.Pawn, FSavedMove_Character::PostUpdate_Record);
	FBrNetworkMoveData Data;
	Data.ClientFillNetworkMoveData(*Start, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
	TestTrue(TEXT("Edge request saved on wire"), Data.bSlidePressed);
	TestTrue(TEXT("State reported"), Data.Slide.bActive);
	TestTrue(TEXT("Remaining captured"), FMath::IsNearlyEqual(Data.Slide.Remaining, .7f));
	FBitWriter MoveWriter(2048, true);
	TestTrue(TEXT("Serialize packed network move"), Data.Serialize(*M, MoveWriter, nullptr,
		FCharacterNetworkMoveData::ENetworkMoveType::NewMove));
	FBitReader MoveReader(MoveWriter.GetData(), MoveWriter.GetNumBits());
	FBrNetworkMoveData Wire;
	TestTrue(TEXT("Read packed network move"), Wire.Serialize(*M, MoveReader, nullptr,
		FCharacterNetworkMoveData::ENetworkMoveType::NewMove));
	TestTrue(TEXT("Request edge wire round trip"), Wire.bSlidePressed);
	TestEqual(TEXT("State wire round trip"), Wire.Slide.Remaining, Data.Slide.Remaining);
	TestEqual(TEXT("Entire move consumed"), MoveReader.GetPosBits(), MoveWriter.GetNumBits());
	FBrMoveResponseDataContainer Response;
	Response.ClientAdjustment.bAckGoodMove = true;
	Response.Slide = Data.Slide;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Serialize correction"), Response.Serialize(*M, Writer, nullptr));
	FBrMoveResponseDataContainer Read;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Read correction"), Read.Serialize(*M, Reader, nullptr));
	TestEqual(TEXT("Direction round trip"), Read.Slide.Direction, Data.Slide.Direction);
	TestEqual(TEXT("Remaining round trip"), Read.Slide.Remaining, Data.Slide.Remaining);
	TestEqual(TEXT("Start time round trip"), Read.Slide.StartMoveTime, Data.Slide.StartMoveTime);
	FSavedMovePtr Next = Prediction->AllocateNewMove();
	Next->SetMoveFor(F.Pawn, .1f, M->Acceleration, *Prediction);
	TestFalse(TEXT("Never combine across start"), Start->CanCombineWith(Next, F.Pawn, .125f));
	M->Slide = Read.Slide; // Same correction boundary as ClientHandleMoveResponse.
	Next->PrepMoveFor(F.Pawn);
	M->CalcVelocity(.1f, 8, false, 2048);
	const auto Replayed = M->Slide;
	M->Slide = Read.Slide;
	Next->PrepMoveFor(F.Pawn);
	M->CalcVelocity(.1f, 8, false, 2048);
	TestEqual(TEXT("Repeated replay identical"), M->Slide.Remaining, Replayed.Remaining);
	TestEqual(TEXT("Replay keeps authority direction"), M->Slide.Direction, Read.Slide.Direction);
	M->CancelSlide();
	const float Cooldown = M->Slide.Cooldown;
	Next->PrepMoveFor(F.Pawn);
	TestEqual(TEXT("Prep never restores stale cooldown"), M->Slide.Cooldown, Cooldown);
	Prediction->CurrentTimeStamp = .01f; // Native reset affects no relative deadline.
	M->FinishSprintReplay();
	M->UpdateSlideBeforeMovement(.1f);
	TestTrue(TEXT("Timestamp reset cannot skip cooldown"), FMath::IsNearlyEqual(M->Slide.Cooldown, .9f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSlideReplayWireYawTest, "BrGame.Slide.ReplayPreservesSprintAndSlideWireFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSlideReplayWireYawTest::RunTest(const FString& Parameters)
{
	// Both valid forward starts and invalid backward starts must survive replay
	// and retransmission unchanged, regardless of the controller's current yaw.
	for (const bool SlideRequest : {false, true})
		for (const float HistoricalYaw : {0.f, 180.f})
		{
			FSlideFixture Client;
			auto* M = Client.Move;
			auto* Controller = Client.Pawn->GetController();
			Controller->SetControlRotation(FRotator(7, HistoricalYaw, 3));
			M->Acceleration = FVector(1000, 0, 0);
			M->SetSprinting(true);
			if (SlideRequest) M->RequestSlide();
			auto* Prediction = M->GetPredictionData_Client_Character();
			Prediction->CurrentTimeStamp = .1f;
			FSavedMovePtr Saved = Prediction->AllocateNewMove();
			Saved->SetMoveFor(Client.Pawn, .01f, M->Acceleration, *Prediction);
			M->UpdateCharacterStateBeforeMovement(.01f);
			const bool ExpectedSprint = M->IsSprinting();
			const bool ExpectedSlide = M->IsSliding();
			const bool Forward = HistoricalYaw == 0;
			TestEqual(TEXT("Recorded sprint eligibility"), ExpectedSprint, !SlideRequest && Forward);
			TestEqual(TEXT("Recorded slide eligibility"), ExpectedSlide, SlideRequest && Forward);
			Saved->PostUpdate(Client.Pawn, FSavedMove_Character::PostUpdate_Record);
			const FRotator RecordedRotation = Saved->SavedControlRotation;

			for (const float LiveYaw : {HistoricalYaw + 180.f, HistoricalYaw + 90.f})
			{
				// Model an authority correction to before the start, then replay.
				M->RequestPosture(EBrPosture::Standing);
				M->ApplyAuthoritativePosture(EBrPosture::Standing);
				M->Slide = FBrSlideState();
				M->Sprint = FBrSprintState();
				M->Velocity = FVector(780, 0, 0);
				Controller->SetControlRotation(FRotator(19, LiveYaw, 11));
				const FRotator LiveRotation = Controller->GetControlRotation();
				Saved->PrepMoveFor(Client.Pawn);
				M->UpdateFromCompressedFlags(Saved->GetCompressedFlags());
				M->UpdateCharacterStateBeforeMovement(.01f);
				TestEqual(TEXT("Replay sprint eligibility"), M->IsSprinting(), ExpectedSprint);
				TestEqual(TEXT("Replay slide eligibility"), M->IsSliding(), ExpectedSlide);
				Saved->PostUpdate(Client.Pawn, FSavedMove_Character::PostUpdate_Replay);
				TestTrue(TEXT("Replay preserves full recorded control rotation"),
					Saved->SavedControlRotation.Equals(RecordedRotation, .001f));
				TestTrue(TEXT("Replay leaves live controller rotation untouched"),
					Controller->GetControlRotation().Equals(LiveRotation, .001f));
				TestFalse(TEXT("PostUpdate clears replay yaw context"), M->SprintReplayYaw.IsSet());

				FBrNetworkMoveData Out;
				Out.ClientFillNetworkMoveData(*Saved, FCharacterNetworkMoveData::ENetworkMoveType::OldMove);
				FBitWriter Writer(2048, true);
				TestTrue(TEXT("Serialize replayed retransmission"), Out.Serialize(*M, Writer, nullptr,
					FCharacterNetworkMoveData::ENetworkMoveType::OldMove));
				FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
				FBrNetworkMoveData Wire;
				TestTrue(TEXT("Read replayed retransmission"), Wire.Serialize(*M, Reader, nullptr,
					FCharacterNetworkMoveData::ENetworkMoveType::OldMove));
				TestTrue(TEXT("Wire control rotation remains historical (native 16-bit quantization)"),
					Wire.ControlRotation.Equals(RecordedRotation, .01f));
				TestEqual(TEXT("Entire wire move consumed"), Reader.GetPosBits(), Writer.GetNumBits());

				FSlideFixture Authority;
				auto* Server = Authority.Move;
				// Apply the decoded move's inputs to the actual authority eligibility path;
				// no client end-state/stamina/direction is used to grant either gait.
				Authority.Pawn->GetController()->SetControlRotation(Wire.ControlRotation);
				Server->Acceleration = Wire.Acceleration;
				Server->UpdateFromCompressedFlags(Wire.CompressedMoveFlags);
				Server->RestoreSlideRequest(Wire.bSlidePressed);
				Server->RestoreInputBlock(Wire.bInputBlocked);
				Server->MoveInputYaw = Wire.ControlRotation.Yaw;
				Server->UpdateCharacterStateBeforeMovement(.01f);
				TestEqual(TEXT("Authority sprint eligibility matches prediction after retransmission"),
					Server->IsSprinting(), ExpectedSprint);
				TestEqual(TEXT("Authority slide eligibility matches prediction after retransmission"),
					Server->IsSliding(), ExpectedSlide);
			}
		}
	return true;
}
#endif
