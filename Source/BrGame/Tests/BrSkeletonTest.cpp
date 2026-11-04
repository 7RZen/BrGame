#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrCorpse.h"
#include "BrDeveloperSettings.h"
#include "BrLifeComponent.h"
#include "BrSupinePose.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSkeletonPoseTest, "BrGame.Life.RealMeshSupineGroundingAndFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSkeletonPoseTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Asset = UBrDeveloperSettings::Get()->DefaultCharacterMesh.LoadSynchronous();
	if (!TestNotNull(TEXT("Configured real mannequin"), Asset)) return false;
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ABrCharacter* Character = World->SpawnActor<ABrCharacter>();
	Character->GetMesh()->SetSkeletalMesh(Asset);

	// Calibrate the actual head-bone local face axis from the imported reference
	// skeleton, rather than assuming head X/Y/Z has the same axes as the component.
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const int32 HeadIndex = Ref.FindBoneIndex(TEXT("head"));
	if (!TestTrue(TEXT("Real head bone exists"), HeadIndex != INDEX_NONE))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	FTransform RefHead = Ref.GetRefBonePose()[HeadIndex];
	for (int32 Parent = Ref.GetParentIndex(HeadIndex); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
		RefHead = RefHead * Ref.GetRefBonePose()[Parent];
	const FVector HeadLocalFace = RefHead.InverseTransformVectorNoScale(FVector::RightVector);
	AddInfo(FString::Printf(TEXT("Reference head local face axis: %s"), *HeadLocalFace.ToString()));
	const auto Check = [&](USkeletalMeshComponent* Mesh, const FVector& Facing, float Ground, const TCHAR* Label)
	{
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
		Mesh->TickAnimation(0.f, false);
		Mesh->RefreshBoneTransforms();
		Mesh->UpdateComponentToWorld();
		const FVector Face = Mesh->GetSocketTransform(TEXT("head")).TransformVectorNoScale(HeadLocalFace);
		const FVector Axis = Mesh->GetBoneLocation(TEXT("head")) - Mesh->GetBoneLocation(TEXT("pelvis"));
		float Lowest = TNumericLimits<float>::Max();
		// Deforming landmarks only: root/IK helpers aren't body surface.
		for (FName Bone : {TEXT("pelvis"), TEXT("spine_01"), TEXT("head"), TEXT("hand_l"), TEXT("hand_r"),
			TEXT("calf_l"), TEXT("calf_r"), TEXT("foot_l"), TEXT("foot_r")})
			Lowest = FMath::Min(Lowest, static_cast<float>(Mesh->GetBoneLocation(Bone).Z - Ground));
		const FBoxSphereBounds Bounds = Asset->GetImportedBounds().TransformBy(Mesh->GetComponentTransform());
		const double SkinBottom = Bounds.Origin.Z - Bounds.BoxExtent.Z - Ground;
		AddInfo(FString::Printf(TEXT("%s face=%s head-pelvis=%s lowest bone=%.2f skin bottom=%.2f"),
			Label, *Face.ToString(), *Axis.ToString(), Lowest, SkinBottom));
		TestTrue(TEXT("Head/face normal points up"), FVector::DotProduct(Face, FVector::UpVector) > 0.99);
		TestTrue(TEXT("Head toward last facing"), FVector::DotProduct(Axis.GetSafeNormal2D(), Facing) > 0.99);
		TestTrue(TEXT("Torso horizontal"), FMath::Abs(Axis.Z) < 10);
		TestTrue(TEXT("Lowest body bone close to ground with skin clearance"), Lowest >= 0 && Lowest <= 25);
		TestTrue(TEXT("Reference skin bounds rest on ground"), FMath::IsNearlyEqual(SkinBottom, 0.5, 0.1));
	};
	for (float Yaw : {0.f, 37.f, 125.f, 270.f})
	{
		Character->SetActorRotation(FRotator(0, Yaw, 0));
		for (EBrPosture Posture : {EBrPosture::Standing, EBrPosture::Crouching, EBrPosture::Prone})
		{
			Character->GetLife()->State = EBrLifeState::Alive;
			Character->GetBrMovement()->ApplyAuthoritativePosture(Posture);
			Character->GetLife()->State = EBrLifeState::Downed;
			Character->ApplyLifePresentation();
			Check(Character->GetMesh(), Character->GetActorForwardVector(),
				Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), TEXT("Downed"));
			TestTrue(TEXT("CMC caches full supine rotation"), Character->GetBaseRotationOffset().Equals(
				Character->GetMesh()->GetRelativeRotation().Quaternion(), 0.001));
		}
		ABrCorpse* Corpse = World->SpawnActor<ABrCorpse>(FVector(500, 300, 20), FRotator(0, Yaw, 0));
		Corpse->DispatchBeginPlay();
		USkeletalMeshComponent* CorpseMesh = Corpse->FindComponentByClass<USkeletalMeshComponent>();
		Check(CorpseMesh, Corpse->GetActorForwardVector(), Corpse->GetActorLocation().Z, TEXT("Corpse"));
		TestFalse(TEXT("Corpse mesh tick remains disabled"), CorpseMesh->IsComponentTickEnabled());
		TestNull(TEXT("Corpse keeps reference pose"), CorpseMesh->GetAnimInstance());
		Corpse->Destroy();
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
