#include "BrPostureVisual.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrCharacter.h"
#include "BrCharacterAnimInstance.h"
#include "BrCharacterMovementComponent.h"
#include "BrDeveloperSettings.h"
#include "BrLifeComponent.h"
#include "BrSupinePose.h"
#include "BrFogComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPostureTargetsTest, "BrGame.Posture.TargetSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPostureTargetsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Standing / sprint"), FBrPostureBlend::Select(EBrPosture::Standing, true), FVector(1, 0, 0));
	TestEqual(TEXT("Crouch"), FBrPostureBlend::Select(EBrPosture::Crouching, true), FVector(0, 1, 0));
	TestEqual(TEXT("Prone"), FBrPostureBlend::Select(EBrPosture::Prone, true), FVector(0, 0, 1));
	for (EBrPosture P : {EBrPosture::Standing, EBrPosture::Crouching, EBrPosture::Prone})
		TestEqual(TEXT("Life presentation takes precedence"), FBrPostureBlend::Select(P, false), FVector(1, 0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPostureTimingTest, "BrGame.Posture.BlendTimingAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPostureTimingTest::RunTest(const FString& Parameters)
{
	FBrPostureBlend B;
	B.Advance(EBrPosture::Crouching, true, 0.125f, 0.25f, 0.4f);
	TestTrue(TEXT("Half crouch at 125ms"), B.Weights.Equals(FVector(0.5, 0.5, 0), 0.0001));
	B.Advance(EBrPosture::Crouching, true, 0.125f, 0.25f, 0.4f);
	TestEqual(TEXT("Full crouch at 250ms"), B.Weights, FVector(0, 1, 0));
	B.Advance(EBrPosture::Prone, true, 0.2f, 0.25f, 0.4f);
	TestTrue(TEXT("Crouch-prone midpoint at 200ms"), B.Weights.Equals(FVector(0, 0.5, 0.5), 0.0001));
	B.Advance(EBrPosture::Prone, true, 0.2f, 0.25f, 0.4f);
	TestEqual(TEXT("Full prone at 400ms"), B.Weights, FVector(0, 0, 1));
	B.Advance(EBrPosture::Standing, true, 0.4f, 0.25f, 0.4f);
	TestEqual(TEXT("Prone exit uses 400ms"), B.Weights, FVector(1, 0, 0));
	for (int32 I = 0; I < 1000; ++I)
	{
		B.Advance(static_cast<EBrPosture>(I % 3), true, 0.017f, 0.25f, 0.4f);
		TestTrue(TEXT("Interrupted weights bounded and normalized"),
			B.Weights.GetMin() >= 0 && B.Weights.GetMax() <= 1 && FMath::IsNearlyEqual(B.Weights.X + B.Weights.Y + B.Weights.Z, 1., 0.0001));
	}
	B.Advance(EBrPosture::Prone, true, 0, 0, 0);
	TestEqual(TEXT("Zero-time transition snaps safely"), B.Weights, FVector(0, 0, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPostureRealMeshTest, "BrGame.Posture.RealMeshGroundingFacingAndLife",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPostureRealMeshTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ABrCharacter* C = World->SpawnActor<ABrCharacter>();
	USkeletalMeshComponent* Mesh = C->GetMesh();
	Mesh->SetSkeletalMesh(UBrDeveloperSettings::Get()->DefaultCharacterMesh.LoadSynchronous());
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	Mesh->SetAnimInstanceClass(UBrCharacterAnimInstance::StaticClass());
	const auto Evaluate = [&]()
	{
		for (int32 I = 0; I < 40; ++I)
		{
			Mesh->TickAnimation(0.016f, false);
			Mesh->RefreshBoneTransforms();
			Mesh->UpdateComponentToWorld();
		}
	};
	const auto Height = [&](FName Bone)
	{
		return Mesh->GetBoneLocation(Bone).Z - (C->GetActorLocation().Z - C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	};
	Evaluate();
	const float StandingHead = Height(TEXT("head"));
	const auto GroundRelative = [&](FName Bone)
	{
		return Mesh->GetBoneLocation(Bone) - (C->GetActorLocation() - FVector(0, 0, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	};
	const FVector StandingHeadPosition = GroundRelative(TEXT("head"));
	const FVector StandingShoulderPosition = (GroundRelative(TEXT("upperarm_l")) + GroundRelative(TEXT("upperarm_r"))) * 0.5;
	AddInfo(FString::Printf(TEXT("Standing head %.1f"), StandingHead));
	TestTrue(TEXT("Real locomotion evaluated"), StandingHead > 140);
	C->GetBrMovement()->ApplyAuthoritativePosture(EBrPosture::Crouching);
	Evaluate();
	AddInfo(FString::Printf(TEXT("Crouch head %.1f feet %.1f %.1f mesh %s world %s cap %.1f actor %s"), Height(TEXT("head")), Height(TEXT("foot_l")), Height(TEXT("foot_r")),
		*Mesh->GetRelativeLocation().ToString(), *Mesh->GetComponentLocation().ToString(), C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), *C->GetActorLocation().ToString()));
	TestTrue(TEXT("Head lower by at least 35cm"), Height(TEXT("head")) < StandingHead - 35);
	for (FName Foot : {FName(TEXT("foot_l")), FName(TEXT("foot_r"))})
		TestTrue(TEXT("Crouch feet near capsule bottom"), FMath::Abs(Height(Foot) - 12) < 15);
	// Orthographic silhouette metrics in world centimetres (independent of zoom).
	const FQuat Camera = FRotator(-65, 0, 0).Quaternion();
	const FVector Up = Camera.GetUpVector(), Right = Camera.GetRightVector();
	const double HeadShift = FVector::DotProduct(GroundRelative(TEXT("head")) - StandingHeadPosition, Up);
	const FVector ShoulderPosition = (GroundRelative(TEXT("upperarm_l")) + GroundRelative(TEXT("upperarm_r"))) * 0.5;
	const double ShoulderShift = FVector::DotProduct(ShoulderPosition - StandingShoulderPosition, Up);
	const double TorsoWidth = FMath::Max(FMath::Abs(FVector::DotProduct(GroundRelative(TEXT("upperarm_l")), Right)),
		FMath::Abs(FVector::DotProduct(GroundRelative(TEXT("upperarm_r")), Right)));
	const double KneeWidth = FMath::Min(FMath::Abs(FVector::DotProduct(GroundRelative(TEXT("calf_l")), Right)),
		FMath::Abs(FVector::DotProduct(GroundRelative(TEXT("calf_r")), Right)));
	AddInfo(FString::Printf(TEXT("65deg silhouette: head shift %.1f shoulder shift %.1f knee half-width %.1f torso half-width %.1f"),
		HeadShift, ShoulderShift, KneeWidth, TorsoWidth));
	TestTrue(TEXT("Head projects forward at least 18cm at gameplay pitch"), HeadShift >= 18);
	TestTrue(TEXT("Shoulders project forward at least 8cm at gameplay pitch"), ShoulderShift >= 8);
	TestTrue(TEXT("Both knees extend beyond torso silhouette by 6cm"), KneeWidth >= TorsoWidth + 6);
	C->GetBrMovement()->Velocity = FVector(120, 0, 0);
	Evaluate();
	for (FName Foot : {FName(TEXT("foot_l")), FName(TEXT("foot_r"))})
		TestTrue(TEXT("Crouch-walk keeps sampled feet near ground"), Height(Foot) >= -5 && Height(Foot) < 40);
	for (float Speed : {0.f, 120.f})
		for (float Yaw : {0.f, 37.f, 125.f})
		{
			C->SetActorRotation(FRotator(0, Yaw, 0));
			C->GetBrMovement()->Velocity = C->GetActorForwardVector() * Speed;
			C->GetBrMovement()->ApplyAuthoritativePosture(EBrPosture::Prone);
			Evaluate();
			const FVector Axis = Mesh->GetBoneLocation(TEXT("head")) - Mesh->GetBoneLocation(TEXT("pelvis"));
			AddInfo(FString::Printf(TEXT("Prone speed %.0f yaw %.0f head %.1f axis %s"), Speed, Yaw, Height(TEXT("head")), *Axis.ToString()));
			TestTrue(TEXT("Head close to ground"), Height(TEXT("head")) >= 5 && Height(TEXT("head")) <= 45);
			TestTrue(TEXT("Body aligned with facing"), FVector::DotProduct(Axis.GetSafeNormal2D(), C->GetActorForwardVector()) > 0.98);
			TestTrue(TEXT("Body roughly horizontal"), FMath::Abs(Axis.Z) < 20);
			for (FName Bone : {FName(TEXT("pelvis")), FName(TEXT("head")), FName(TEXT("hand_l")), FName(TEXT("foot_r"))})
				TestTrue(TEXT("Procedural offsets finite and bounded"), !Mesh->GetBoneLocation(Bone).ContainsNaN() &&
					FVector::Distance(Mesh->GetBoneLocation(Bone), C->GetActorLocation()) < 220);
		}
	C->GetLife()->State = EBrLifeState::Downed;
	C->ApplyLifePresentation();
	TestTrue(TEXT("Downed uses supine whole-mesh rotation"), Mesh->GetRelativeRotation().Quaternion().Equals(BrSupinePose::Rotation().Quaternion(), 0.001));
	TestTrue(TEXT("Downed cached base retained"), FMath::IsNearlyEqual(C->GetBaseTranslationOffset().Z,
		-C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + BrSupinePose::GroundLift(Mesh->GetSkeletalMeshAsset()), 0.01));
	TestNull(TEXT("Downed doesn't run posture graph"), Cast<UBrCharacterAnimInstance>(Mesh->GetAnimInstance()));
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPostureFogRevealTest, "BrGame.Posture.FogRevealFirstPoseProductionTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPostureFogRevealTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ABrCharacter* C = World->SpawnActor<ABrCharacter>();
	USkeletalMeshComponent* Mesh = C->GetMesh();
	Mesh->SetSkeletalMesh(UBrDeveloperSettings::Get()->DefaultCharacterMesh.LoadSynchronous());
	Mesh->SetAnimInstanceClass(UBrCharacterAnimInstance::StaticClass());
	// Constructor's production policy, not BeginPlay's BrMatchSmoke override.
	TestEqual(TEXT("Production visibility policy"), Mesh->VisibilityBasedAnimTickOption,
		EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
	TestTrue(TEXT("Production URO enabled"), Mesh->bEnableUpdateRateOptimizations);
	UBrFogComponent* Fog = NewObject<UBrFogComponent>(C);
	C->PrepareForFogReveal(); // Establish standing bones once.
	TestTrue(TEXT("Starting standing head"), Mesh->GetSocketTransform(TEXT("head"), RTS_Component).GetLocation().Z > 140);
	for (EBrPosture Posture : {EBrPosture::Prone, EBrPosture::Crouching, EBrPosture::Standing})
	{
		Fog->ApplyActorPresentation(C, false, false);
		const FVector CachedHead = Mesh->GetSocketTransform(TEXT("head"), RTS_Component).GetLocation();
		C->GetBrMovement()->ApplyAuthoritativePosture(Posture);
		Mesh->SetLastRenderTime(-1000);
		Mesh->bRecentlyRendered = false;
		TestFalse(TEXT("Hidden production mesh skips pose ticking"), Mesh->ShouldTickPose());
		Mesh->TickComponent(0.016f, LEVELTICK_All, nullptr);
		TestTrue(TEXT("Hidden change retains old cached bones until reveal"),
			CachedHead.Equals(Mesh->GetSocketTransform(TEXT("head"), RTS_Component).GetLocation(), 0.001));
		// Explicitly exercise URO's skip-evaluation state as well as visibility gating.
		if (Mesh->AnimUpdateRateParams)
		{
			Mesh->AnimUpdateRateParams->EvaluationRate = 4;
			Mesh->AnimUpdateRateParams->bSkipEvaluation = true;
			Mesh->AnimUpdateRateParams->bSkipUpdate = true;
		}
		int32 Evaluations = 0;
		bool bEvaluatedWhileHidden = true;
		const FDelegateHandle EvaluationHandle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(
			FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda([&]()
			{
				++Evaluations;
				bEvaluatedWhileHidden &= Mesh->bHiddenInGame;
			}));
		Fog->ApplyActorPresentation(C, true, false);
		TestTrue(TEXT("Reveal synchronously finalized new bones"), Evaluations > 0);
		TestTrue(TEXT("Bones finalized BEFORE unhide"), bEvaluatedWhileHidden);
		// NO frame, TickAnimation or RefreshBoneTransforms after unhide: these are the
		// first bones available to rendering at the production fog boundary.
		const float FirstHead = Mesh->GetSocketTransform(TEXT("head"), RTS_Component).GetLocation().Z;
		AddInfo(FString::Printf(TEXT("First fog-reveal head posture %d: %.1f"), int32(Posture), FirstHead));
		TestFalse(TEXT("Mesh now unhidden"), Mesh->bHiddenInGame);
		TestTrue(TEXT("Reveal restores URO policy"), Mesh->bEnableUpdateRateOptimizations);
		TestEqual(TEXT("Reveal preserves visibility policy"), Mesh->VisibilityBasedAnimTickOption,
			EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
		if (Posture == EBrPosture::Standing) TestTrue(TEXT("First revealed pose standing"), FirstHead > 140);
		else TestTrue(TEXT("First revealed pose already at low target"),
			FirstHead >= 5 && FirstHead < (Posture == EBrPosture::Prone ? 45 : 90));
		// A repeated visible update must not reset normal blending or re-evaluate.
		const FVector RevealedHead = Mesh->GetSocketTransform(TEXT("head"), RTS_Component).GetLocation();
		const int32 RevealEvaluations = Evaluations;
		Fog->ApplyActorPresentation(C, true, false);
		TestEqual(TEXT("Already visible actors do not evaluate again"), Evaluations, RevealEvaluations);
		TestTrue(TEXT("No repeated evaluation for already visible actors"),
			RevealedHead.Equals(Mesh->GetSocketTransform(TEXT("head"), RTS_Component).GetLocation(), 0.001));
		Mesh->UnregisterOnBoneTransformsFinalizedDelegate(EvaluationHandle);
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
