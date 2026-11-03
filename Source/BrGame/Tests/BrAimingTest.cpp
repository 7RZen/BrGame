#include "BrAimingRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrInventoryComponent.h"
#include "BrGameMode.h"
#include "BrPlayerState.h"
#include "OnlineSubsystemTypes.h"
#include "BrPlayerController.h"
#include "BrLifeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "BrDeveloperSettings.h"
#include "BrGameInstance.h"
#include "BrInputConfig.h"
#include "Camera/CameraTypes.h"
#include "SceneView.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAimingDeltaTest, "BrGame.Aiming.MouseDeltaSensitivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAimingDeltaTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Positive delta turns clockwise"), FBrAimingRules::TurnYaw(10, 100, 0.2f), 30.f);
	TestEqual(TEXT("Negative delta"), FBrAimingRules::TurnYaw(10, -100, 0.2f), -10.f);
	TestEqual(TEXT("Comfort setting scales delta"), FBrAimingRules::TurnYaw(10, 100, 0.5f), 60.f);
	TestEqual(TEXT("Wrap"), FBrAimingRules::TurnYaw(179, 20, 0.2f), -177.f);
	TestEqual(TEXT("Zero X (including Y-only movement) has no effect"), FBrAimingRules::TurnYaw(42, 0, 1), 42.f);
	float Split = 0;
	for (int32 I = 0; I < 10; ++I) Split = FBrAimingRules::TurnYaw(Split, 10, 0.2f);
	TestEqual(TEXT("Not frame-rate scaled"), Split, FBrAimingRules::TurnYaw(0, 100, 0.2f));
	TestEqual(TEXT("Lower bound"), FBrAimingRules::Sensitivity(-1), 0.02f);
	TestEqual(TEXT("Upper bound"), FBrAimingRules::Sensitivity(10), 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAimingOffsetTest, "BrGame.Aiming.OrthographicScreenOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAimingOffsetTest::RunTest(const FString& Parameters)
{
	for (float Width : {1200.f, 2500.f, 4000.f})
		for (float Aspect : {4.f / 3.f, 16.f / 9.f, 21.f / 9.f})
			for (float Pitch : {30.f, 45.f, 65.f, 90.f})
				for (float Fraction : {0.f, 0.25f, 0.4f})
					for (float Yaw : {0.f, 37.f, 180.f})
					{
						const FVector Offset = FRotator(0, Yaw, 0).Vector() *
							FBrAimingRules::ForwardOffset(Width, Aspect, Pitch, Fraction);
						const FVector CameraUp = FRotationMatrix(FRotator(-Pitch, Yaw, 0)).GetUnitAxis(EAxis::Z);
						const float ProjectedY = 0.5f - FVector::DotProduct(-Offset, CameraUp) / (Width / Aspect);
						TestTrue(TEXT("Pawn origin projects to configured fraction below centre"),
							FMath::IsNearlyEqual(ProjectedY, 0.5f + Fraction, 0.0001f));
					}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAimingMoveTest, "BrGame.Aiming.FacingRelativeMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAimingMoveTest::RunTest(const FString& Parameters)
{
	for (float Yaw : {0.f, 37.f, 90.f, -179.f})
	{
		const FRotationMatrix Rotation(FRotator(0, Yaw, 0));
		TestTrue(TEXT("W forward"), FBrAimingRules::Movement(Yaw, {0, 1}).Equals(Rotation.GetUnitAxis(EAxis::X)));
		TestTrue(TEXT("S backward"), FBrAimingRules::Movement(Yaw, {0, -1}).Equals(-Rotation.GetUnitAxis(EAxis::X)));
		TestTrue(TEXT("A left"), FBrAimingRules::Movement(Yaw, {-1, 0}).Equals(-Rotation.GetUnitAxis(EAxis::Y)));
		TestTrue(TEXT("D right"), FBrAimingRules::Movement(Yaw, {1, 0}).Equals(Rotation.GetUnitAxis(EAxis::Y)));
		TestTrue(TEXT("Diagonal capped"), FMath::IsNearlyEqual(FBrAimingRules::Movement(Yaw, {1, 1}).Size(), 1.f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAimingCameraTest, "BrGame.Aiming.CameraFollowsReplicatedFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAimingCameraTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ABrCharacter* Character = World->SpawnActor<ABrCharacter>();
	if (TestNotNull(TEXT("Unpossessed spectator target"), Character))
	{
		for (float Yaw : {0.f, 37.f, 90.f, -179.f})
		{
			Character->SetActorRotation(FRotator(0, Yaw, 0));
			FMinimalViewInfo View;
			Character->SetSpectatorCameraActive();
			Character->CalcCamera(0, View);
			TestTrue(TEXT("Camera uses watched pawn facing without local controller"),
				FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(View.Rotation.Yaw, Yaw), 0.001f));
			TestTrue(TEXT("Minimap yaw matches rendered yaw"),
				FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Character->GetCameraYaw(), View.Rotation.Yaw), 0.001f));
			// Match PlayerCameraManager, which overwrites this even after CalcCamera.
			View.SetCameraToViewTarget(Character->GetActorLocation());
			FSceneViewProjectionData Projection;
			Projection.ViewOrigin = View.Location;
			Projection.ViewRotationMatrix = FInverseRotationMatrix(View.Rotation) * FMatrix(
				FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
			const FIntRect Rect(0, 0, 1280, 720);
			Projection.SetViewRectangle(Rect);
			FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, AspectRatio_MaintainXFOV, Rect, Projection);
			const FVector4 Clip = Projection.ComputeViewProjectionMatrix().TransformFVector4(FVector4(Character->GetActorLocation(), 1));
			const float ScreenY = 0.5f - Clip.Y / Clip.W * 0.5f;
			TestTrue(TEXT("Engine ortho correction preserves screen offset"),
				FMath::IsNearlyEqual(ScreenY, 0.5f + UBrDeveloperSettings::Get()->PlayerScreenOffset, 0.0001f));
		}
		// Model CMC's visual smoothing offset, not a second camera interpolation.
		Character->GetMesh()->SetRelativeRotation((FQuat(FVector::UpVector, FMath::DegreesToRadians(12.f)) *
			Character->GetBaseRotationOffset()).Rotator());
		TestTrue(TEXT("Camera includes mesh yaw smoothing"),
			FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Character->GetCameraYaw(), -167.f), 0.001f));
		Character->GetMesh()->SetRelativeRotation(Character->GetBaseRotationOffset());
		ABrPlayerController* PC = World->SpawnActor<ABrPlayerController>();
		// UE 5.8 classifies no-net-driver PCs by their ULocalPlayer, not net mode alone.
		PC->Player = NewObject<ULocalPlayer>(GEngine);
		PC->Possess(Character);
		PC->SetControlRotation(FRotator(0, 83, 0));
		TestEqual(TEXT("Local camera instantly follows control yaw"), Character->GetCameraYaw(), 83.f);
		Character->GetLife()->State = EBrLifeState::Downed;
		Character->ApplyLifePresentation();
		Character->FaceRotation(PC->GetControlRotation());
		TestEqual(TEXT("Downed facing can still turn"), Character->GetActorRotation().Yaw, 83.0);
		TestFalse(TEXT("Downed still cannot move"), Character->CanMove());
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAimingSettingsTest, "BrGame.Aiming.SettingsAndInputAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAimingSettingsTest::RunTest(const FString& Parameters)
{
	UBrAudioProfile* Profile = NewObject<UBrAudioProfile>();
	Profile->MouseSensitivity = 0.73f;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Serialize without touching user's save"), UGameplayStatics::SaveGameToMemory(Profile, Bytes));
	const UBrAudioProfile* Loaded = Cast<UBrAudioProfile>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (TestNotNull(TEXT("Settings round trip"), Loaded))
	{
		TestEqual(TEXT("Version"), Loaded->Version, 2);
		TestEqual(TEXT("Sensitivity persists"), Loaded->MouseSensitivity, 0.73f);
	}
	const UBrInputConfig* Config = UBrDeveloperSettings::Get()->InputConfig.LoadSynchronous();
	if (!TestNotNull(TEXT("Input config"), Config) || !TestNotNull(TEXT("Look action"), Config->LookAction.Get())) return false;
	TestEqual(TEXT("X-only scalar"), Config->LookAction->ValueType, EInputActionValueType::Axis1D);
	if (!TestNotNull(TEXT("Mapping context"), Config->DefaultMappingContext.Get())) return false;
	int32 LookCount = 0;
	for (const FEnhancedActionKeyMapping& Mapping : Config->DefaultMappingContext->GetMappings())
	{
		if (Mapping.Action != Config->LookAction) continue;
		++LookCount;
		TestEqual(TEXT("Raw mouse X"), Mapping.Key, EKeys::MouseX);
		TestFalse(TEXT("Not a rebinding row"), Mapping.IsPlayerMappable());
	}
	TestEqual(TEXT("Exactly one look axis, no Y"), LookCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrServerAimTest, "BrGame.Aiming.ServerAimSetsFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrServerAimTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Context.OwningGameInstance = Instance;
	World->SetGameInstance(Instance);
	Instance->Init();
	FURL URL;
	URL.AddOption(TEXT("game=/Script/BrGame.BrGameMode"));
	World->SetGameMode(URL);
	World->InitializeActorsForPlay(URL);
	ABrGameMode* Mode = World->GetAuthGameMode<ABrGameMode>();
	ABrPlayerController* PC = World->SpawnActor<ABrPlayerController>();
	ABrCharacter* Pawn = World->SpawnActor<ABrCharacter>();
	if (TestNotNull(TEXT("Authoritative mode"), Mode) && TestNotNull(TEXT("Controller"), PC) &&
		TestNotNull(TEXT("Shooter"), Pawn))
	{
		ABrPlayerState* PS = World->SpawnActor<ABrPlayerState>();
		PC->PlayerState = PS;
		PS->SetOwner(PC);
		PS->SetPlayerId(42);
		const FUniqueNetIdPtr Id = FUniqueNetIdString::Create(TEXT("ServerAimTest"), TEXT("NULL"));
		PS->SetUniqueId(FUniqueNetIdRepl(Id));
		Mode->AdmittedControllers.Add(PC);
		PC->Possess(Pawn);
		PC->AcknowledgedPawn = Pawn;
		UBrCharacterMovementComponent* Movement = Pawn->GetBrMovement();
		Movement->DisableMovement(); // Fixture has no floor; life remains Alive.
		UBrInventoryComponent* Inventory = Pawn->GetInventory();
		Inventory->Weapon = NewObject<UBrWeaponDefinition>(World);
		Inventory->Loaded = 2;
		Inventory->Throwable = NewObject<UBrThrowableDefinition>(World);
		Inventory->Grenades = 2;
		auto ExpectFacing = [&](const TCHAR* Label, double Yaw)
		{
			TestTrue(FString(Label) + TEXT(" control yaw"), FMath::IsNearlyZero(
				FMath::FindDeltaAngleDegrees(PC->GetControlRotation().Yaw, Yaw), 0.001));
			TestTrue(FString(Label) + TEXT(" actor yaw"), FMath::IsNearlyZero(
				FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw, Yaw), 0.001));
		};
		PC->SetControlRotation(FRotator::ZeroRotator);
		Pawn->SetActorRotation(FRotator::ZeroRotator);
		Inventory->ServerFire_Implementation(FVector::BackwardVector, 0.2f);
		TestEqual(TEXT("Behind-facing fire accepted without flick rejection"), Inventory->Loaded, 1);
		ExpectFacing(TEXT("Accepted fire faces aim"), 180);
		TestEqual(TEXT("One authoritative bullet"), Inventory->Bullets.Num(), 1);
		if (!Inventory->Bullets.IsEmpty())
			TestTrue(TEXT("Bullet matches facing"), Inventory->Bullets[0].Direction.Equals(Pawn->GetActorForwardVector(), 0.001));

		auto Move = [&](float TimeStamp, double Yaw)
		{
			FBrNetworkMoveData Data;
			Data.TimeStamp = TimeStamp;
			Data.ControlRotation = FRotator(0, Yaw, 0);
			Data.Location = Pawn->GetActorLocation();
			Data.MovementMode = Movement->PackNetworkMovementMode();
			Movement->ServerMove_PerformMovement(Data);
		};
		Move(0.1f, 0);
		TestEqual(TEXT("Old move still simulated/validated by CMC"), Movement->GetPredictionData_Server_Character()->CurrentClientTimeStamp, 0.1f);
		ExpectFacing(TEXT("Queued pre-shot move cannot undo aim"), 180);
		Move(0.2f, 0);
		ExpectFacing(TEXT("Same-timestamp batched move cannot undo aim"), 180);
		Move(0.3f, -45);
		ExpectFacing(TEXT("Newer legitimate mouse turn is not locked"), -45);

		Inventory->ServerGrenade_Implementation(FVector::RightVector, 0.4f);
		TestEqual(TEXT("Different-facing grenade accepted"), Inventory->Grenades, 1);
		ExpectFacing(TEXT("Accepted grenade faces aim"), 90);
		Move(0.35f, -45);
		ExpectFacing(TEXT("Queued move cannot undo grenade aim"), 90);
		Move(0.5f, 37);
		ExpectFacing(TEXT("Post-grenade movement yaw resumes"), 37);

		Inventory->ServerFire_Implementation(FVector::ForwardVector, 0.5f); // Fire cooldown.
		TestEqual(TEXT("Cooldown still enforced"), Inventory->Loaded, 1);
		ExpectFacing(TEXT("Rejected fire does not turn pawn"), 37);
		Inventory->NextAllowedFire = 0;
		Inventory->ServerFire_Implementation(FVector(0, 0, 1), 0.5f);
		Inventory->ServerFire_Implementation(FVector(2, 0, 0), 0.5f);
		TestEqual(TEXT("Aim validity still enforced"), Inventory->Loaded, 1);
		ExpectFacing(TEXT("Invalid fire aim does not turn pawn"), 37);
		Inventory->ServerGrenade_Implementation(FVector::ForwardVector, 0.5f);
		TestEqual(TEXT("Grenade cooldown still enforced"), Inventory->Grenades, 1);
		ExpectFacing(TEXT("Rejected grenade does not turn pawn"), 37);
		TestFalse(TEXT("Non-finite ordering hint rejected"), Movement->IsValidCombatMoveTimeStamp(
			std::numeric_limits<float>::infinity()));
		TestFalse(TEXT("Unbounded ordering hint rejected"), Movement->IsValidCombatMoveTimeStamp(100000));
	}
	TestFalse(TEXT("Equal timestamp is pre-shot"), FBrAimingRules::IsMoveAfterCombatAim(1, 1, 240));
	TestTrue(TEXT("New timestamp after CMC reset"), FBrAimingRules::IsMoveAfterCombatAim(0.1f, 239.9f, 240));
	TestFalse(TEXT("Old epoch move after aim reset"), FBrAimingRules::IsMoveAfterCombatAim(239.9f, 0.1f, 240));
	World->DestroyWorld(false);
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
