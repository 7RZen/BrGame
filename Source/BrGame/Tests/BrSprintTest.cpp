#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrDeveloperSettings.h"
#include "BrInventoryComponent.h"
#include "BrHealthComponent.h"
#include "BrPlayerController.h"
#include "BrGameState.h"
#include "BrGameMode.h"
#include "BrPlayerState.h"
#include "BrLifeComponent.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystemTypes.h"
#include "TimerManager.h"
#include "BrAudioRules.h"
#include "BrInputConfig.h"
#include "InputMappingContext.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSprintRulesTest, "BrGame.Sprint.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSprintRulesTest::RunTest(const FString& Parameters)
{
	for (float Weapon : {1.f, 0.9f, 0.8f})
	{
		TestTrue(TEXT("130 percent times weapon"), FMath::IsNearlyEqual(
			FBrMovementSpeed::Calculate(EBrPosture::Standing, true, Weapon, 600, 1.3f, .2f, .2f), 780 * Weapon));
		TestEqual(TEXT("Prone never sprints"), FBrMovementSpeed::Calculate(EBrPosture::Prone, true, Weapon, 600, 1.3f, .2f, .2f), 120.f);
	}
	for (float Facing : {0.f, 90.f, -179.f})
	{
		for (float Angle : {-49.f, 0.f, 49.f})
			TestTrue(TEXT("Forward diagonal"), FBrSprintState::IsForward(FRotator(0, Facing + Angle, 0).Vector(), Facing, 50));
		for (float Angle : {-90.f, 51.f, 90.f, 180.f})
			TestFalse(TEXT("Reject strafe/back/outside cone"), FBrSprintState::IsForward(FRotator(0, Facing + Angle, 0).Vector(), Facing, 50));
	}
	TestFalse(TEXT("No input"), FBrSprintState::IsForward(FVector::ZeroVector, 0, 50));
	TestEqual(TEXT("Sprint kind"), FBrAudioRules::StepKind(EBrPosture::Standing, true), EBrSoundKind::SprintStep);
	TestEqual(TEXT("Crouch ignores sprint"), FBrAudioRules::StepKind(EBrPosture::Crouching, true), EBrSoundKind::CrouchStep);
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	TestEqual(TEXT("20 metre sprint"), S->SprintStepHearingRange, 2000.f);
	TestTrue(TEXT("Louder than run"), S->SprintStepVolume > 1);
	TestEqual(TEXT("Prone silent"), FBrAudioRules::StepRange(EBrPosture::Prone, true, 1500, 2000, 300), 0.f);
	TestFalse(TEXT("Active sprint forbids fire, even with grace"), FBrSprintState::CanFire(true, 10, 0, .03f));
	TestFalse(TEXT("Recovery window rejects"), FBrSprintState::CanFire(false, 10.1, 10.2, .03f));
	TestTrue(TEXT("Small arrival grace"), FBrSprintState::CanFire(false, 10.18, 10.2, .03f));
	const UBrInputConfig* Input = S->InputConfig.LoadSynchronous();
	if (TestNotNull(TEXT("Generated sprint action"), Input ? Input->SprintAction.Get() : nullptr))
	{
		int32 SprintRows = 0;
		for (const FEnhancedActionKeyMapping& Mapping : Input->DefaultMappingContext->GetMappings())
		{
			TestNotEqual(TEXT("No obsolete mapping"), Mapping.GetMappingName(), FName(TEXT("Br.Walk")));
			if (Mapping.Action == Input->SprintAction)
			{
				++SprintRows;
				TestEqual(TEXT("Rebindable Sprint row"), Mapping.GetMappingName(), FName(TEXT("Br.Sprint")));
				TestEqual(TEXT("Shift default"), Mapping.Key, EKeys::LeftShift);
			}
		}
		TestEqual(TEXT("One sprint binding"), SprintRows, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSprintStaminaTest, "BrGame.Sprint.Stamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSprintStaminaTest::RunTest(const FString& Parameters)
{
	FBrSprintState State;
	State.Advance(2, true, 5, .5f, 1, 5);
	TestEqual(TEXT("Partial use"), State.Stamina, 3.f);
	State.Advance(.5f, false, 5, .5f, 1, 5);
	TestEqual(TEXT("Regen waits"), State.Stamina, 3.f);
	State.Advance(1, false, 5, .5f, 1, 5);
	TestEqual(TEXT("Only elapsed time past delay regenerates"), State.Stamina, 3.5f);
	State.Advance(3.5f, true, 5, .5f, 1, 5);
	TestEqual(TEXT("Drained"), State.Stamina, 0.f);
	TestFalse(TEXT("Empty stops sprint"), State.bActive);
	State.Advance(1.4f, true, 5, .5f, 1, 5);
	TestFalse(TEXT("Minimum blocks restart"), State.bActive);
	State.Advance(.1f, false, 5, .5f, 1, 5);
	TestTrue(TEXT("Half second worth restored"), FMath::IsNearlyEqual(State.Stamina, .5f));
	State.Advance(.1f, true, 5, .5f, 1, 5);
	TestTrue(TEXT("Partial restart"), State.bActive);
	State.Advance(6, false, 5, .5f, 1, 5);
	TestEqual(TEXT("Full regen after delay plus five seconds"), State.Stamina, 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSprintIntegrationTest, "BrGame.Sprint.MovementPredictionAndCombat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSprintIntegrationTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ABrGameState* State = World->SpawnActor<ABrGameState>();
	State->bLobby = false;
	World->SetGameState(State);
	ABrCharacter* Pawn = World->SpawnActor<ABrCharacter>();
	ABrPlayerController* PC = World->SpawnActor<ABrPlayerController>();
	ABrPlayerState* PS = World->SpawnActor<ABrPlayerState>();
	PC->PlayerState = PS;
	PS->SetOwner(PC);
	PC->Player = NewObject<ULocalPlayer>(GEngine);
	PC->Possess(Pawn);
	PC->SetControlRotation(FRotator::ZeroRotator);
	UBrCharacterMovementComponent* Move = Pawn->GetBrMovement();
	UBrInventoryComponent* Gear = Pawn->GetInventory();
	Gear->Weapon = NewObject<UBrWeaponDefinition>(World);
	Gear->Loaded = 5;
	Gear->Throwable = NewObject<UBrThrowableDefinition>(World);
	Gear->Grenades = 2;
	Pawn->GetHealth()->Health = 100;
	Move->SetMovementMode(MOVE_Walking);
	Move->RequestPosture(EBrPosture::Crouching);
	Move->Crouch();
	Move->RefreshPosture();
	TestEqual(TEXT("Fixture crouched"), Move->GetPosture(), EBrPosture::Crouching);
	Pawn->SprintStarted();
	TestEqual(TEXT("Shift requests uncrouch"), Move->GetRequestedPosture(), EBrPosture::Standing);
	Move->Acceleration = FVector(1000, 0, 0);
	Move->UpdateCharacterStateBeforeMovement(.1f);
	TestEqual(TEXT("Crouch expands before sprint"), Move->GetPosture(), EBrPosture::Standing);
	TestTrue(TEXT("Standing sprint"), Move->IsSprinting());
	const float Energy = Move->Sprint.Stamina;
	FNetworkPredictionData_Client_Character* Prediction = Move->GetPredictionData_Client_Character();
	FSavedMovePtr Saved = Prediction->AllocateNewMove();
	Saved->SetMoveFor(Pawn, .1f, Move->Acceleration, *Prediction);
	Saved->PostUpdate(Pawn, FSavedMove_Character::PostUpdate_Record);
	const uint8 Flags = Saved->GetCompressedFlags();
	TestTrue(TEXT("Sprint is Custom_0"), (Flags & FSavedMove_Character::FLAG_Custom_0) != 0);
	Move->UpdateFromCompressedFlags(0);
	Move->UpdateFromCompressedFlags(Flags);
	TestTrue(TEXT("Compressed flag round trip"), Move->WantsSprint());
	FBrNetworkMoveData Data;
	Data.ClientFillNetworkMoveData(*Saved, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
	TestEqual(TEXT("Saved end stamina carried in network move"), Data.Sprint.Stamina, Energy);
	FBrMoveResponseDataContainer Response;
	Response.ClientAdjustment.bAckGoodMove = true;
	Response.Sprint = Data.Sprint;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("Serialize authoritative move response"), Response.Serialize(*Move, Writer, nullptr));
	FBrMoveResponseDataContainer Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("Deserialize move response"), Loaded.Serialize(*Move, Reader, nullptr));
	TestEqual(TEXT("Stamina response round trip"), Loaded.Sprint.Stamina, Energy);
	TestEqual(TEXT("Rest response round trip"), Loaded.Sprint.RestTime, Response.Sprint.RestTime);
	TestEqual(TEXT("Active response round trip"), Loaded.Sprint.bActive, true);
	FSavedMovePtr First = Prediction->AllocateNewMove();
	FSavedMovePtr Second = Prediction->AllocateNewMove();
	First->SetMoveFor(Pawn, .01f, Move->Acceleration, *Prediction);
	Move->UpdateSprint(.01f);
	First->PostUpdate(Pawn, FSavedMove_Character::PostUpdate_Record);
	Second->SetMoveFor(Pawn, .01f, Move->Acceleration, *Prediction);
	TestTrue(TEXT("Linear sprint moves may combine"), First->CanCombineWith(Second, Pawn, .125f));
	Second->CombineWith(First.Get(), Pawn, PC, Pawn->GetActorLocation());
	TestEqual(TEXT("Native combining rewinds energy to pending move start"), Move->Sprint.Stamina, Energy);
	Move->UpdateSprint(Second->DeltaTime);
	TestTrue(TEXT("Combined simulation drains once, not twice"), FMath::IsNearlyEqual(Move->Sprint.Stamina, Energy - .02f));
	Gear->ServerFire_Implementation(FVector::ForwardVector, 0);
	Gear->ServerGrenade_Implementation(FVector::ForwardVector, 0);
	TestEqual(TEXT("Server rejects active sprint shot"), Gear->Loaded, 5);
	TestEqual(TEXT("Server rejects active sprint throw"), Gear->Grenades, 2);
	Gear->StartFire();
	TestFalse(TEXT("Fire cancels sprint"), Move->IsSprinting());
	TestTrue(TEXT("Shot deferred by timer"), Gear->bPendingFire);
	TestEqual(TEXT("No immediate shot"), Gear->Loaded, 5);
	Gear->ServerFire_Implementation(FVector::ForwardVector, 0);
	TestEqual(TEXT("Authoritative recovery window rejects"), Gear->Loaded, 5);
	TestEqual(TEXT("Still deferred before delay"), Gear->Loaded, 5);
	TestTrue(TEXT("Authority retains the valid recovery shot"), Gear->PendingServerFire.IsSet());
	// Tick world time without needing a map or an external server.
	World->Tick(LEVELTICK_All, .25f);
	// This synchronous test runs inside one engine frame; TimerManager's once-per-frame
	// guard cannot expire a newly pending timer. Exercise its callback after recovery;
	// the multi-process smoke below covers actual timer expiry over real frames.
	Gear->ServicePendingFire();
	Gear->DeferredFire();
	TestEqual(TEXT("Deferred callback consumes ammo after recovery"), Gear->Loaded, 4);
	Gear->CancelCombat();
	Move->RequestPosture(EBrPosture::Prone);
	Move->Crouch();
	Move->RefreshPosture();
	Pawn->SprintStarted();
	TestEqual(TEXT("Prone Shift stays prone"), Move->GetRequestedPosture(), EBrPosture::Prone);
	TestFalse(TEXT("Prone Shift has no sprint intent"), Move->WantsSprint());
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

namespace
{
	/** All callbacks below run on the game thread; no editor/map or user's saves are used. */
	struct FBrSprintReviewFixture
	{
		UWorld* World;
		UGameInstance* Instance;
		ABrCharacter* Pawn;
		ABrPlayerController* PC;
		ABrPlayerState* PS;
		UBrCharacterMovementComponent* Move;
		UBrInventoryComponent* Gear;

		FBrSprintReviewFixture()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			Instance = NewObject<UGameInstance>(GEngine);
			Context.OwningGameInstance = Instance;
			World->SetGameInstance(Instance);
			Instance->Init();
			FURL URL;
			URL.AddOption(TEXT("game=/Script/BrGame.BrGameMode"));
			World->SetGameMode(URL);
			World->InitializeActorsForPlay(URL);
			PC = World->SpawnActor<ABrPlayerController>();
			PS = World->SpawnActor<ABrPlayerState>();
			PC->PlayerState = PS;
			PS->SetOwner(PC);
			PS->SetPlayerId(42);
			const FUniqueNetIdRef Identity = FUniqueNetIdString::Create(TEXT("SprintReview"), TEXT("NULL"));
			PS->SetUniqueId(FUniqueNetIdRepl(Identity));
			PC->Player = NewObject<ULocalPlayer>(GEngine);
			Pawn = World->SpawnActor<ABrCharacter>();
			PC->Possess(Pawn);
			PC->AcknowledgedPawn = Pawn;
			PC->SetControlRotation(FRotator::ZeroRotator);
			Move = Pawn->GetBrMovement();
			Move->SetMovementMode(MOVE_Walking);
			Gear = Pawn->GetInventory();
			Gear->Weapon = NewObject<UBrWeaponDefinition>(World);
			Gear->Loaded = 5;
			Gear->Throwable = NewObject<UBrThrowableDefinition>(World);
			Gear->Grenades = 2;
			Pawn->GetHealth()->Health = 100;
		}

		~FBrSprintReviewFixture()
		{
			Gear->CancelCombat();
			World->DestroyWorld(false);
			Instance->Shutdown();
			GEngine->DestroyWorldContext(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSprintReplayYawTest, "BrGame.Sprint.ReplayHistoricalFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSprintReplayYawTest::RunTest(const FString& Parameters)
{
	FBrSprintReviewFixture F;
	auto* Prediction = F.Move->GetPredictionData_Client_Character();
	F.Move->Acceleration = FVector(1000, 0, 0);
	for (float HistoricalYaw : {0.f, 180.f})
	{
		F.Move->Sprint = FBrSprintState();
		F.Move->RestoreSprintIntent(true);
		F.PC->SetControlRotation(FRotator(0, HistoricalYaw, 0));
		FSavedMovePtr Saved = Prediction->AllocateNewMove();
		Saved->SetMoveFor(F.Pawn, .1f, F.Move->Acceleration, *Prediction);
		F.Move->UpdateSprint(.1f);
		const FBrSprintState Expected = F.Move->Sprint;
		const float ExpectedSpeed = F.Move->GetMaxSpeed();
		Saved->PostUpdate(F.Pawn, FSavedMove_Character::PostUpdate_Record);
		F.Move->Sprint = FBrSprintState(); // Model correction to the move's start.
		const float LiveYaw = HistoricalYaw == 0 ? 180.f : 0.f;
		F.PC->SetControlRotation(FRotator(0, LiveYaw, 0));
		Saved->PrepMoveFor(F.Pawn);
		F.Move->UpdateSprint(.1f);
		TestEqual(TEXT("Replay eligibility uses historical yaw"), F.Move->Sprint.bActive, Expected.bActive);
		TestEqual(TEXT("Replay stamina unchanged by live turn"), F.Move->Sprint.Stamina, Expected.Stamina);
		TestEqual(TEXT("Replay speed unchanged by live turn"), F.Move->GetMaxSpeed(), ExpectedSpeed);
		TestEqual(TEXT("Live control yaw never overwritten"), F.PC->GetControlRotation().Yaw, static_cast<double>(LiveYaw));
		Saved->PostUpdate(F.Pawn, FSavedMove_Character::PostUpdate_Replay);
		TestFalse(TEXT("Per-move replay context cleared"), F.Move->SprintReplayYaw.IsSet());
		F.Move->Sprint = FBrSprintState();
		F.Move->UpdateSprint(.1f);
		TestEqual(TEXT("Next live move uses live yaw"), F.Move->Sprint.bActive, HistoricalYaw != 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSprintReplayRecoveryTest, "BrGame.Sprint.ReplayDoesNotExtendTapRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSprintReplayRecoveryTest::RunTest(const FString& Parameters)
{
	FBrSprintReviewFixture F;
	F.Move->Sprint.bActive = true;
	F.Gear->StartFire();
	F.Gear->StopFire(); // Quick tap must survive both release and correction replay.
	const double OriginalDeadline = F.Move->GetSprintFireReadyTime();
	TestTrue(TEXT("Quick tap is pending"), F.Gear->bPendingFire);
	auto* Prediction = F.Move->GetPredictionData_Client_Character();
	FSavedMovePtr Stop = Prediction->AllocateNewMove();
	Stop->SetMoveFor(F.Pawn, .1f, FVector::ZeroVector, *Prediction);
	Stop->PostUpdate(F.Pawn, FSavedMove_Character::PostUpdate_Record);
	F.World->Tick(LEVELTICK_All, .25f);
	for (int32 Replay = 0; Replay < 2; ++Replay)
	{
		F.Move->Sprint.bActive = true; // Corrected pre-stop state.
		Stop->PrepMoveFor(F.Pawn);
		F.Move->Acceleration = FVector::ZeroVector;
		F.Move->UpdateSprint(.1f);
		Stop->PostUpdate(F.Pawn, FSavedMove_Character::PostUpdate_Replay);
		TestEqual(TEXT("Historical stop does not restart recovery at replay now"),
			F.Move->GetSprintFireReadyTime(), OriginalDeadline);
	}
	F.Gear->DeferredFire(); // Same callback invoked by the local recovery timer.
	TestEqual(TEXT("Released quick tap fires after original delay"), F.Gear->Loaded, 4);
	TestFalse(TEXT("Tap consumed exactly once"), F.Gear->bPendingFire);
	// With no retained deadline, reconstruct from the saved simulation time.
	F.Move->SprintFireReadyTime = 0;
	F.Move->Sprint.bActive = true;
	Stop->PrepMoveFor(F.Pawn);
	F.Move->UpdateSprint(.1f);
	Stop->PostUpdate(F.Pawn, FSavedMove_Character::PostUpdate_Replay);
	TestEqual(TEXT("Missing recovery reconstructed from historical move time"),
		F.Move->GetSprintFireReadyTime(), OriginalDeadline);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSprintPendingCombatTest, "BrGame.Sprint.CoalescedIntentAndActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSprintPendingCombatTest::RunTest(const FString& Parameters)
{
	FBrSprintReviewFixture F;
	F.World->GetAuthGameMode<ABrGameMode>()->AdmittedControllers.Add(F.PC);
	F.Move->Sprint.bActive = true;
	F.Gear->ServerCombatIntent_Implementation(true);
	F.Gear->ServerFire_Implementation(FVector::ForwardVector, 0);
	F.Gear->ServerGrenade_Implementation(FVector::ForwardVector, 0);
	TestTrue(TEXT("Same-arrival fire retained"), F.Gear->PendingServerFire.IsSet());
	TestTrue(TEXT("Same-arrival throw retained"), F.Gear->PendingServerGrenade.IsSet());
	TestEqual(TEXT("No immediate shot"), F.Gear->Loaded, 5);
	TestEqual(TEXT("No immediate throw"), F.Gear->Grenades, 2);
	F.Gear->ServerFire_Implementation(FVector::BackwardVector, 0);
	TestEqual(TEXT("Duplicate request cannot replace/bank first shot"), F.Gear->PendingServerFire->Aim, FVector::ForwardVector);
	F.Gear->ServerCombatIntent_Implementation(false);
	TestTrue(TEXT("Tap release does not cancel accepted queued actions"), F.Gear->IsCombatHeld());
	F.Gear->ServicePendingFire(); // Even an early callback must defer again.
	TestEqual(TEXT("Authority still enforces recovery"), F.Gear->Loaded, 5);
	TestTrue(TEXT("Early callback preserves fire"), F.Gear->PendingServerFire.IsSet());
	F.World->Tick(LEVELTICK_All, .25f);
	// Synchronous one-engine-frame automation explicitly services callbacks after
	// advancing world time. Match smoke validates actual timer expiry over frames.
	F.Gear->ServicePendingFire();
	F.Gear->ServicePendingGrenade();
	TestEqual(TEXT("Queued shot executes after authoritative recovery"), F.Gear->Loaded, 4);
	TestEqual(TEXT("Queued grenade executes after authoritative recovery"), F.Gear->Grenades, 1);
	TestFalse(TEXT("Pending slots released"), F.Gear->IsCombatHeld());
	F.Gear->ServicePendingFire();
	F.Gear->ServicePendingGrenade();
	TestEqual(TEXT("No duplicate shot"), F.Gear->Loaded, 4);
	TestEqual(TEXT("No duplicate grenade"), F.Gear->Grenades, 1);
	F.Gear->NextAllowedFire = 0;
	F.Gear->NextGrenade = 0;
	F.Move->SprintFireReadyTime = F.World->GetTimeSeconds() + .2;
	F.Gear->ServerFire_Implementation(FVector::ForwardVector, 0);
	F.Gear->ServerGrenade_Implementation(FVector::ForwardVector, 0);
	F.Gear->ServerCancelCombat_Implementation(); // Remote menu cancellation.
	TestFalse(TEXT("Menu cancels queued fire"), F.Gear->PendingServerFire.IsSet());
	TestFalse(TEXT("Menu cancels queued throw"), F.Gear->PendingServerGrenade.IsSet());
	F.Gear->ServerGrenade_Implementation(FVector::ForwardVector, 0);
	F.Pawn->GetLife()->State = EBrLifeState::Downed;
	F.Gear->ServicePendingGrenade();
	TestFalse(TEXT("Downing discards pending action at revalidation"), F.Gear->PendingServerGrenade.IsSet());
	TestEqual(TEXT("Downed throw consumes nothing"), F.Gear->Grenades, 1);
	F.Pawn->GetLife()->State = EBrLifeState::Alive;
	F.Gear->ServerFire_Implementation(FVector::ForwardVector, 0);
	F.Gear->bReloading = true;
	F.Gear->ServicePendingFire();
	TestFalse(TEXT("Reloading invalidates pending shot"), F.Gear->PendingServerFire.IsSet());
	TestEqual(TEXT("Reloading consumes nothing"), F.Gear->Loaded, 4);
	F.Gear->bReloading = false;
	F.Gear->ServerFire_Implementation(FVector::ForwardVector, 0);
	F.Gear->Loaded = 0;
	F.Gear->ServicePendingFire();
	TestFalse(TEXT("Empty ammo invalidates pending shot"), F.Gear->PendingServerFire.IsSet());
	return true;
}

#endif
