#include "BrHitFeedbackRules.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrHitFeedbackComponent.h"
#include "BrCharacter.h"
#include "BrSupinePose.h"
#include "BrCharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "BrLifeComponent.h"
#include "BrDeveloperSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Camera/CameraTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrHitFeedbackDirectionTest, "BrGame.HitFeedback.ScreenDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrHitFeedbackDirectionTest::RunTest(const FString& Parameters)
{
	for (float Yaw : {0.f, 37.f, 90.f, 180.f, -90.f})
		for (float Pitch : {30.f, 65.f, 90.f})
			for (float Offset : {0.f, 0.25f, 0.4f})
			{
				const FRotator Rotation(0, Yaw, 0);
				const FVector2D Up = FBrHitFeedbackRules::ScreenDirection(Rotation.RotateVector(FVector(100, 0, 0)), Yaw, Pitch);
				const FVector2D Right = FBrHitFeedbackRules::ScreenDirection(Rotation.RotateVector(FVector(0, 100, 0)), Yaw, Pitch);
				TestTrue(TEXT("Forward always screen-up"), Up.Equals(FVector2D(0, -1), 0.001));
				TestTrue(TEXT("Right always screen-right"), Right.Equals(FVector2D(1, 0), 0.001));
				const FVector2D Pawn(640, 720 * (0.5f + Offset)), Size(1280, 720);
				TestTrue(TEXT("Forward reaches top edge from displaced pawn"),
					FBrHitFeedbackRules::EdgePoint(Pawn, Up, Size, 22).Equals(FVector2D(640, 22), 0.01));
				TestTrue(TEXT("Right stays aligned with displaced pawn"),
					FBrHitFeedbackRules::EdgePoint(Pawn, Right, Size, 22).Equals(FVector2D(1258, Pawn.Y), 0.01));
			}
	TestTrue(TEXT("Turning changes direction of a fixed world source"),
		FBrHitFeedbackRules::ScreenDirection(FVector(100, 0, 0), 90, 65).Equals(FVector2D(-1, 0), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrHitFeedbackGateTest, "BrGame.HitFeedback.DamageAndVisibilityGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrHitFeedbackGateTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Missed roll / zero health loss"), FBrHitFeedbackRules::DamageEligible(0, true, false));
	TestFalse(TEXT("Invulnerable"), FBrHitFeedbackRules::DamageEligible(20, true, true));
	TestFalse(TEXT("Downed, including downing hit"), FBrHitFeedbackRules::DamageEligible(20, false, false));
	TestTrue(TEXT("Actual damage including friendly fire"), FBrHitFeedbackRules::DamageEligible(20, true, false));
	TestFalse(TEXT("Fog-hidden observer"), FBrHitFeedbackRules::RemoteEligible(false, false, true));
	TestFalse(TEXT("Owner doesn't double-present remote reaction"), FBrHitFeedbackRules::RemoteEligible(true, true, true));
	TestFalse(TEXT("Downed observer reaction excluded"), FBrHitFeedbackRules::RemoteEligible(true, false, false));
	TestTrue(TEXT("Visible remote reaction"), FBrHitFeedbackRules::RemoteEligible(true, false, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrHitFeedbackCameraTest, "BrGame.HitFeedback.BoundedCameraProfilesAndControlRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrHitFeedbackCameraTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ABrCharacter* C = World->SpawnActor<ABrCharacter>();
	APlayerController* PC = World->SpawnActor<APlayerController>();
	PC->Player = NewObject<ULocalPlayer>(GEngine);
	PC->Possess(C);
	PC->SetViewTarget(C);
	PC->SetControlRotation(FRotator(0, 37, 0));
	UBrHitFeedbackComponent* F = C->GetHitFeedback();
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	F->ClientDamage_Implementation(FVector(1000, 0, 0), false, 10);
	TestEqual(TEXT("One indicator"), F->Indicators.Num(), 1);
	TestEqual(TEXT("Bullet duration"), F->ShakeDuration, S->BulletHitDuration);
	TestEqual(TEXT("Bullet angle"), F->ShakeAmplitude, S->BulletHitAngle);
	TestEqual(TEXT("Bullet has no positional jolt"), F->JoltAmplitude, 0.f);
	F->ClientDamage_Implementation(FVector(1000, 0, 0), true, 10);
	TestEqual(TEXT("Grenade duration"), F->ShakeDuration, S->GrenadeHitDuration);
	TestEqual(TEXT("Grenade angle"), F->ShakeAmplitude, S->GrenadeHitAngle);
	TestEqual(TEXT("Grenade jolt"), F->JoltAmplitude, S->GrenadeHitJolt);
	for (int32 I = 0; I < 100; ++I) F->ClientDamage_Implementation(FVector(1000, 0, 0), false, 10);
	TestEqual(TEXT("Indicator cap"), F->Indicators.Num(), S->HitIndicatorCap);
	TestEqual(TEXT("No accumulating amplitude"), F->ShakeAmplitude, S->GrenadeHitAngle);
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		F->ShakeTime = -Frame * 0.01f;
		FMinimalViewInfo View;
		View.Rotation = FRotator(-65, 37, 0);
		F->Camera(View);
		TestTrue(TEXT("Bounded roll"), FMath::Abs(View.Rotation.Roll) <= S->GrenadeHitAngle);
		TestTrue(TEXT("Bounded yaw"), FMath::Abs(View.Rotation.Yaw - 37) <= S->GrenadeHitAngle + 0.001);
		TestTrue(TEXT("Bounded translation"), View.Location.Size() <= S->GrenadeHitJolt + 0.001);
		TestTrue(TEXT("Control rotation untouched"), PC->GetControlRotation().Equals(FRotator(0, 37, 0)));
	}
	// Test the resulting view basis, not independent Euler component bounds. At pitch
	// 90 the former yaw+roll implementation could produce twice the requested twist.
	for (float Amplitude : {S->BulletHitAngle, S->GrenadeHitAngle})
		for (float Pitch : {30.f, 65.f, 90.f})
			for (float RollSeed : {-1.f, 1.f})
				for (float YawSeed : {-1.f, 1.f})
				{
					F->ShakeTime = 0;
					F->ShakeAmplitude = Amplitude;
					F->ShakeSeed = FVector(RollSeed, YawSeed, 1);
					FMinimalViewInfo View;
					View.Rotation = FRotator(-Pitch, 37, 0);
					const FQuat Before = View.Rotation.Quaternion();
					F->Camera(View);
					const FQuat After = View.Rotation.Quaternion();
					const double Angle = FMath::RadiansToDegrees(Before.AngularDistance(After));
					const double RightAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
						FVector::DotProduct(Before.GetRightVector(), After.GetRightVector()), -1.0, 1.0)));
					TestTrue(TEXT("Total view orientation bounded at extreme seeds"), Angle <= Amplitude + 0.0001);
					TestTrue(TEXT("Screen basis twist bounded at extreme seeds"), RightAngle <= Amplitude + 0.0001);
					TestTrue(TEXT("Extreme seed reaches single configured twist"), FMath::IsNearlyEqual(Angle, double(Amplitude), 0.0001));
					TestTrue(TEXT("View axis unchanged by local twist"), Before.GetForwardVector().Equals(After.GetForwardVector(), 0.0001));
					TestTrue(TEXT("Extreme shake leaves control rotation untouched"), PC->GetControlRotation().Equals(FRotator(0, 37, 0)));
				}
	PC->SetViewTarget(PC);
	F->ShakeTime = 0;
	FMinimalViewInfo SpectatorView;
	F->Camera(SpectatorView);
	TestTrue(TEXT("Not watching owned pawn: no shake"), SpectatorView.Rotation.IsZero());
	C->GetLife()->State = EBrLifeState::Downed;
	const uint32 Events = F->OwnerEvents;
	F->ClientDamage_Implementation(FVector(1000, 0, 0), true, 10);
	TestEqual(TEXT("Downed receives nothing"), F->OwnerEvents, Events);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrHitFeedbackSmoothingTest, "BrGame.HitFeedback.PreservesNetworkSmoothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrHitFeedbackSmoothingTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	for (ENetworkSmoothingMode Mode : {ENetworkSmoothingMode::Exponential, ENetworkSmoothingMode::Linear})
	{
		ABrCharacter* C = World->SpawnActor<ABrCharacter>();
		C->SetActorRotation(FRotator(0, 37, 0));
		UBrCharacterMovementComponent* Movement = C->GetBrMovement();
		Movement->NetworkSmoothingMode = Mode;
		FNetworkPredictionData_Client_Character* Data = Movement->GetPredictionData_Client_Character();
		UBrHitFeedbackComponent* Feedback = C->GetHitFeedback();
		const FVector LifeBase = C->GetBaseTranslationOffset();
		const FQuat LifeRotation = C->GetBaseRotationOffset();
		Data->MeshTranslationOffset = FVector(31, -17, 9);
		Data->MeshRotationOffset = FRotator(4, 23, -3).Quaternion();
		// Exercise UE's actual visual smoothing writer before the character's reaction tick.
		// No network interpolation is run in this standalone fixture.
		Movement->SmoothClientPosition_UpdateVisuals();
		const FVector SmoothedLocation = C->GetMesh()->GetRelativeLocation();
		const FQuat SmoothedRotation = C->GetMesh()->GetRelativeRotation().Quaternion();
		const FVector TranslationOffset = Data->MeshTranslationOffset;
		const FQuat RotationOffset = Data->MeshRotationOffset;
		Feedback->MulticastReaction_Implementation(FVector::ForwardVector);
		C->Tick(0);
		const FVector FirstJerk = Feedback->MeshOffset();
		TestTrue(TEXT("Reaction actually applied"), !FirstJerk.IsNearlyZero());
		TestTrue(TEXT("CMC translation preserved underneath jerk"),
			C->GetMesh()->GetRelativeLocation().Equals(SmoothedLocation + FirstJerk, 0.0001));
		TestTrue(TEXT("CMC rotation preserved underneath jerk"),
			C->GetMesh()->GetRelativeRotation().Quaternion().Equals(SmoothedRotation, 0.0001));
		TestTrue(TEXT("Cache includes additive reaction for next CMC update"),
			C->GetBaseTranslationOffset().Equals(LifeBase + FirstJerk, 0.0001));
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Movement->SmoothClientPosition_UpdateVisuals();
			C->Tick(0);
			TestTrue(TEXT("CMC then owner tick never doubles or erases impulse"),
				C->GetMesh()->GetRelativeLocation().Equals(SmoothedLocation + FirstJerk, 0.0001));
			TestTrue(TEXT("Rotation residual survives repeated smoothing writes"),
				C->GetMesh()->GetRelativeRotation().Quaternion().Equals(SmoothedRotation, 0.0001));
		}
		// New network correction and a decayed jerk in the same frame.
		Data->MeshTranslationOffset *= 0.5;
		Data->MeshRotationOffset = FRotator(-2, -11, 5).Quaternion();
		Movement->SmoothClientPosition_UpdateVisuals();
		const FVector NewSmoothedLocation = C->GetMesh()->GetRelativeLocation() - FirstJerk;
		const FQuat NewSmoothedRotation = C->GetMesh()->GetRelativeRotation().Quaternion();
		Feedback->ReactionTime = -UBrDeveloperSettings::Get()->HitReactionDuration * 0.5f;
		C->Tick(0);
		TestTrue(TEXT("Fresh correction retained while reaction changes"),
			C->GetMesh()->GetRelativeLocation().Equals(NewSmoothedLocation + Feedback->MeshOffset(), 0.0001));
		TestTrue(TEXT("Fresh rotation correction retained"),
			C->GetMesh()->GetRelativeRotation().Quaternion().Equals(NewSmoothedRotation, 0.0001));
		// CMC may skip its visual update once smoothing completes, or be disabled.
		// Expiring must remove only the reaction, without depending on another CMC write.
		C->Tick(0);
		Feedback->ReactionTime = -1000;
		C->Tick(0);
		TestTrue(TEXT("Restoration preserves translation residual without another CMC tick"),
			C->GetMesh()->GetRelativeLocation().Equals(NewSmoothedLocation, 0.0001));
		TestTrue(TEXT("Restoration preserves rotation residual"),
			C->GetMesh()->GetRelativeRotation().Quaternion().Equals(NewSmoothedRotation, 0.0001));
		TestTrue(TEXT("No permanent cached impulse"), C->GetBaseTranslationOffset().Equals(LifeBase, 0.0001));
		TestTrue(TEXT("Cached base rotation unchanged"), C->GetBaseRotationOffset().Equals(LifeRotation, 0.0001));
		TestTrue(TEXT("Reaction does not mutate CMC translation data"), Data->MeshTranslationOffset.Equals(TranslationOffset * 0.5, 0.0001));
		TestTrue(TEXT("Reaction does not mutate CMC rotation data"), Data->MeshRotationOffset.Equals(FRotator(-2, -11, 5).Quaternion(), 0.0001));
		Data->MeshTranslationOffset = TranslationOffset;
		Data->MeshRotationOffset = RotationOffset;
		Movement->SmoothClientPosition_UpdateVisuals();
		TestTrue(TEXT("Next CMC write has no restoration snap or permanent offset"),
			C->GetMesh()->GetRelativeLocation().Equals(SmoothedLocation, 0.0001));
		TestTrue(TEXT("Next CMC write retains original smoothing rotation"),
			C->GetMesh()->GetRelativeRotation().Quaternion().Equals(SmoothedRotation, 0.0001));
		C->GetLife()->State = EBrLifeState::Downed;
		C->ApplyLifePresentation();
		TestTrue(TEXT("Issue 1 downed base height retained"), FMath::IsNearlyEqual(
			C->GetBaseTranslationOffset().Z, -C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() +
				BrSupinePose::GroundLift(C->GetMesh()->GetSkeletalMeshAsset()), 0.0001));
		TestTrue(TEXT("Issue 1 downed cached supine rotation retained"),
			C->GetBaseRotationOffset().Equals(BrSupinePose::Rotation().Quaternion(), 0.0001));
		C->Destroy();
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
