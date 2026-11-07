#include "BrPenaltyRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrPenaltyComponent.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrLifeComponent.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrPlayerState.h"
#include "BrPlayerController.h"
#include "BrDeveloperSettings.h"
#include "BrInputConfig.h"
#include "BrMenuRules.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "OnlineSubsystemTypes.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPenaltyEligibilityTest, "BrGame.Penalty.Eligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPenaltyEligibilityTest::RunTest(const FString&)
{
	TestTrue(TEXT("Anyone at revive range"), FBrPenaltyRules::CanPlant(EBrLifeState::Alive, EBrLifeState::Downed, 22500, 150, false, false));
	TestFalse(TEXT("Out of range"), FBrPenaltyRules::CanPlant(EBrLifeState::Alive, EBrLifeState::Downed, 22501, 150, false, false));
	TestFalse(TEXT("Non-alive planter"), FBrPenaltyRules::CanPlant(EBrLifeState::Downed, EBrLifeState::Downed, 0, 150, false, false));
	TestFalse(TEXT("Revived target"), FBrPenaltyRules::CanPlant(EBrLifeState::Alive, EBrLifeState::Alive, 0, 150, false, false));
	TestFalse(TEXT("Eliminated target"), FBrPenaltyRules::CanPlant(EBrLifeState::Alive, EBrLifeState::Eliminated, 0, 150, false, false));
	TestFalse(TEXT("Self"), FBrPenaltyRules::CanPlant(EBrLifeState::Alive, EBrLifeState::Downed, 0, 150, true, false));
	TestFalse(TEXT("One per down"), FBrPenaltyRules::CanPlant(EBrLifeState::Alive, EBrLifeState::Downed, 0, 150, false, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPenaltyRaceTest, "BrGame.Penalty.Deadlines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPenaltyRaceTest::RunTest(const FString&)
{
	TestEqual(TEXT("Ten-second hold progress"), FBrPenaltyRules::Progress(15, 10, 20), .5f);
	TestEqual(TEXT("Progress clamped"), FBrPenaltyRules::Progress(30, 10, 20), 1.f);
	const auto Exit = [](float Timeout, float Revive)
	{
		return FBrLifeRules::SelectDownedExit(Timeout, Revive > 0, Revive, false, 0);
	};
	TestFalse(TEXT("Not finished"), FBrPenaltyRules::Wins(20, 19, Exit(50, 0)));
	TestFalse(TEXT("Revive first cancels even in a late service frame"), FBrPenaltyRules::Wins(20, 25, Exit(50, 19)));
	TestFalse(TEXT("Revive tie wins"), FBrPenaltyRules::Wins(20, 25, Exit(50, 20)));
	TestTrue(TEXT("Plant first lets revive continue"), FBrPenaltyRules::Wins(20, 25, Exit(50, 21)));
	TestFalse(TEXT("Respawn/timeout first"), FBrPenaltyRules::Wins(20, 25, Exit(19, 0)));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPenaltyTimerTest, "BrGame.Penalty.TimerAndSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPenaltyTimerTest::RunTest(const FString&)
{
	TestFalse(TEXT("Dormant while downed"), FBrPenaltyRules::Active(true, 0, 0, 20));
	TestEqual(TEXT("Downed does not start timer"), FBrPenaltyRules::StartDormant(0, false, 20), 0.f);
	TestEqual(TEXT("Revive starts timer"), FBrPenaltyRules::StartDormant(0, true, 20), 20.f);
	TestEqual(TEXT("Later revive never renews active timer"), FBrPenaltyRules::StartDormant(20, true, 25), 20.f);
	TestTrue(TEXT("Active until deadline regardless of life state"), FBrPenaltyRules::Active(true, 20, 35, 34));
	TestFalse(TEXT("Expires at deadline"), FBrPenaltyRules::Active(true, 20, 35, 35));
	TestFalse(TEXT("Cleared elimination"), FBrPenaltyRules::Active(false, 20, 35, 25));
	TestEqual(TEXT("Rocket multiplies weapon-adjusted normal run"), FBrPenaltyRules::Speed(.8f, 2), 1.6f);
	TestEqual(TEXT("Buttplug multiplies weapon-adjusted normal run"), FBrPenaltyRules::Speed(.8f, .5f), .4f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPenaltyContentTest, "BrGame.Penalty.ContentAndInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPenaltyContentTest::RunTest(const FString&)
{
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	TestEqual(TEXT("Default hold"), S->PlantDuration, 10.f);
	TestEqual(TEXT("Four configured definitions"), S->PenaltyDefinitions.Num(), 4);
	TSet<FName> Ids;
	for (const auto& Ref : S->PenaltyDefinitions)
	{
		auto* Def = Ref.LoadSynchronous();
		if (!TestNotNull(TEXT("Configured definition loads"), Def)) continue;
		TestFalse(TEXT("Unique extensible ID"), Ids.Contains(Def->Id));
		Ids.Add(Def->Id);
		TestEqual(TEXT("Duration"), Def->Duration, Def->Id == TEXT("Rocket") ? 15.f : 30.f);
	}
	UBrInputConfig* Input = S->InputConfig.LoadSynchronous();
	if (!TestNotNull(TEXT("Input config"), Input)) return false;
	TestNotNull(TEXT("Plant action"), Input->PlantAction.Get());
	int32 FCount = 0;
	for (const auto& Mapping : Input->DefaultMappingContext->GetMappings())
	{
		if (Mapping.Key == EKeys::F)
		{
			++FCount;
			TestTrue(TEXT("F exclusively Plant"), Mapping.Action == Input->PlantAction);
			TestEqual(TEXT("Player-rebindable name"), Mapping.GetMappingName(), FName(TEXT("Br.Plant")));
		}
	}
	TestEqual(TEXT("No F conflict"), FCount, 1);
	FString Error;
	TMap<FName, FKey> Bindings = {{TEXT("Br.Plant"), EKeys::F}, {TEXT("Br.Interact"), EKeys::E}};
	TestFalse(TEXT("Plant conflict rejected"), FBrMenuRules::CanBind(TEXT("Br.Plant"), EKeys::E, Bindings, Error));
	TestFalse(TEXT("Tilde remains reserved"), FBrMenuRules::CanBind(TEXT("Br.Plant"), EKeys::Tilde, Bindings, Error));
	TestFalse(TEXT("Period remains reserved"), FBrMenuRules::CanBind(TEXT("Br.Plant"), EKeys::Period, Bindings, Error));
	TestTrue(TEXT("Free key rebind allowed"), FBrMenuRules::CanBind(TEXT("Br.Plant"), EKeys::H, Bindings, Error));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPenaltyRuntimeTest, "BrGame.Penalty.AuthorityRacesRestoreMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPenaltyRuntimeTest::RunTest(const FString&)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Context.OwningGameInstance = Instance;
	World->SetGameInstance(Instance);
	Instance->Init();
	FURL URL; URL.AddOption(TEXT("game=/Script/BrGame.BrGameMode"));
	World->SetGameMode(URL);
	World->InitializeActorsForPlay(URL);
	ABrGameMode* Mode = World->GetAuthGameMode<ABrGameMode>();
	ABrGameState* GS = Mode->GetGameState<ABrGameState>();
	GS->bLobby = GS->bResults = false;
	ABrCharacter* Pawns[3];
	ABrPlayerController* PCs[3];
	FString Keys[3];
	for (int32 I = 0; I < 3; ++I)
	{
		PCs[I] = World->SpawnActor<ABrPlayerController>();
		auto* PS = World->SpawnActor<ABrPlayerState>();
		PCs[I]->PlayerState = PS;
		PS->SetOwner(PCs[I]); PS->SetPlayerId(I + 1);
		const FUniqueNetIdPtr Id = FUniqueNetIdString::Create(FString::Printf(TEXT("PenaltyTest%d"), I), TEXT("NULL"));
		PS->SetUniqueId(FUniqueNetIdRepl(Id));
		Mode->AdmittedControllers.Add(PCs[I]);
		Pawns[I] = World->SpawnActor<ABrCharacter>(FVector(I * 50.f, 0, 100), FRotator::ZeroRotator);
		PCs[I]->Possess(Pawns[I]);
		Pawns[I]->DispatchBeginPlay();
		Keys[I] = Mode->RoundKey(PCs[I]);
		FBrRoundRecord Record; Record.ActiveController = PCs[I]; Record.PlayerId = I + 1;
		Mode->RoundRecords.Add(Keys[I], Record);
	}
	// Spawn collision adjustment is unrelated to interaction policy. Fix fixture positions explicitly.
	for (int32 I = 0; I < 3; ++I) Pawns[I]->SetActorLocation(FVector(I * 50.f, 0, 100));
	auto* Target = Pawns[0]->GetPenalty();
	auto* A = Pawns[1]->GetPenalty();
	auto* B = Pawns[2]->GetPenalty();
	const auto Resolve = [&](float Now)
	{
		const auto* Life = Pawns[0]->GetLife();
		Target->ResolvePlants(Now, FBrLifeRules::SelectDownedExit(Life->DownedEndTime,
			IsValid(Life->Reviver) && Life->Reviver->GetLife()->CanRevive(Pawns[0]),
			Life->ReviveEndTime, true, Life->RespawnHoldEndTime));
	};
	Pawns[0]->GetLife()->RestoreDowned(100);
	A->StartPlant(); B->StartPlant();
	TestTrue(TEXT("Concurrent planters allowed"), A->PlantTarget == Pawns[0] && B->PlantTarget == Pawns[0]);
	TestEqual(TEXT("Server derives ten-second deadline"), A->PlantEndTime - A->PlantStartTime, 10.f);
	A->StopPlant();
	TestNull(TEXT("Release cancels"), A->PlantTarget.Get());
	A->StartPlant(); Pawns[1]->GetLife()->DamageReceived();
	TestNull(TEXT("Damage cancels"), A->PlantTarget.Get());
	A->StartPlant();
	Pawns[1]->SetActorLocation(FVector(1000, 0, 100));
	A->Service();
	TestNull(TEXT("Range cancels"), A->PlantTarget.Get());
	Pawns[1]->SetActorLocation(FVector(50, 0, 100));
	A->StartPlant();
	A->PlantEndTime = 10; B->PlantEndTime = 11;
	Pawns[0]->GetLife()->Reviver = Pawns[2];
	Pawns[0]->GetLife()->ReviveEndTime = 12;
	Resolve(13); // Late callback; earliest plant deadline wins, revive still present.
	TestTrue(TEXT("Item applied dormant"), Target->Penalty.Definition && Target->Penalty.StartTime == 0);
	TestTrue(TEXT("Revive not cancelled"), Pawns[0]->GetLife()->Reviver == Pawns[2]);
	TestNull(TEXT("Winner hold cleared"), A->PlantTarget.Get());
	TestNull(TEXT("Other planters cleared"), B->PlantTarget.Get());
	Target->Cosmetics();
	TestNotNull(TEXT("Dormant tube exists"), Target->Tube.Get());
	if (Target->Tube)
	{
		TestEqual(TEXT("Tube attaches to pelvis"), Target->Tube->GetAttachSocketName(), FName(TEXT("pelvis")));
		TestTrue(TEXT("Tube visible while downed"), Target->Tube->IsVisible());
	}
	TestTrue(TEXT("Puff pool bounded"), Target->Puffs.Num() <= 16);
	TestEqual(TEXT("Winner score +1"), PCs[1]->GetPlayerState<ABrPlayerState>()->RoundScore, 1);
	TestEqual(TEXT("Ledger score +1"), Mode->RoundRecords.FindChecked(Keys[1]).Journal.Score, 1);
	TestEqual(TEXT("Loser score unchanged"), PCs[2]->GetPlayerState<ABrPlayerState>()->RoundScore, 0);
	A->StartPlant();
	TestNull(TEXT("Cannot replant this down"), A->PlantTarget.Get());
	Resolve(14);
	TestEqual(TEXT("No duplicate award"), Mode->RoundRecords.FindChecked(Keys[1]).Journal.Score, 1);
	const FBrPenaltyState Snapshot = Mode->RoundRecords.FindChecked(Keys[0]).Penalty;
	TestTrue(TEXT("Ledger retains one-per-down and dormant definition"), Snapshot.bPlantedThisDown && Snapshot.Definition);
	Pawns[0]->GetLife()->CancelRevive();
	Target->NewDown();
	B->StartPlant();
	Pawns[2]->GetLife()->State = EBrLifeState::Downed;
	B->LifeChanged();
	TestNull(TEXT("Non-alive planter cancelled"), B->PlantTarget.Get());
	Pawns[2]->GetLife()->State = EBrLifeState::Alive;
	Pawns[0]->GetLife()->Reviver = Pawns[2]; Pawns[0]->GetLife()->ReviveEndTime = 9;
	A->StartPlant(); A->PlantEndTime = 10;
	Resolve(13);
	TestFalse(TEXT("Revive-first grants no new item"), Target->Penalty.bPlantedThisDown);
	Pawns[0]->GetLife()->CancelRevive();
	Pawns[0]->GetLife()->State = EBrLifeState::Alive;
	Target->LifeChanged();
	TestTrue(TEXT("Revive starts dormant effect"), Target->Penalty.StartTime > 0);
	const float End = Target->Penalty.EndTime;
	Pawns[0]->GetLife()->State = EBrLifeState::Downed;
	Target->NewDown();
	TestEqual(TEXT("Later down preserves active deadline"), Target->Penalty.EndTime, End);
	A->StartPlant(); A->PlantEndTime = 10;
	GS->MatchSettings.Mode = EBrMode::Grouped;
	Resolve(11);
	TestTrue(TEXT("Later plant replaces active effect with dormant effect"), Target->Penalty.Definition && Target->Penalty.StartTime == 0);
	TestEqual(TEXT("Grouped score uses same ledger"), Mode->RoundRecords.FindChecked(Keys[1]).Journal.Score, 2);
	Target->Restore(Snapshot);
	TestEqual(TEXT("Restore dormant timer exactly"), Target->Penalty.StartTime, 0.f);
	auto* Rocket = LoadObject<UBrPenaltyDefinition>(nullptr, TEXT("/Game/BrGame/Penalties/DA_Penalty_Rocket.DA_Penalty_Rocket"));
	if (TestNotNull(TEXT("Rocket asset"), Rocket))
	{
		Pawns[0]->GetLife()->State = EBrLifeState::Alive;
		Target->Penalty.Definition = Rocket;
		Target->Penalty.StartTime = 1; Target->Penalty.EndTime = 16;
		auto* Move = Pawns[0]->GetBrMovement();
		Move->SetMovementMode(MOVE_Walking);
		Move->PreparePenaltyReplay(5);
		Move->PrepareSprintReplay(90, 5);
		Move->RequestPosture(EBrPosture::Prone);
		Move->SetSprinting(true); Move->RequestSlide();
		Move->UpdateCharacterStateBeforeMovement(.1f);
		TestEqual(TEXT("Rocket standing request enforced"), Move->GetRequestedPosture(), EBrPosture::Standing);
		TestFalse(TEXT("Rocket disables sprint and slide"), Move->IsSprinting() || Move->IsSliding());
		Move->Acceleration = FVector::ZeroVector;
		Move->CalcVelocity(.1f, 8, false, 2000);
		TestTrue(TEXT("No input still moves along saved facing"), Move->Velocity.GetSafeNormal().Equals(FVector(0, 1, 0), .001));
		TestTrue(TEXT("Rocket double normal speed"), FMath::IsNearlyEqual(Move->Velocity.Size(), UBrDeveloperSettings::Get()->BaseRunSpeed * Pawns[0]->GetWeaponRunSpeedMultiplier() * 2));
		Move->PreparePenaltyReplay(16);
		TestFalse(TEXT("Historical expiry removes forcing"), Target->ForcesForward(Move->PenaltyMoveTime()));
		Move->FinishSprintReplay();
		// Client-side fog hides both new emissions and retained world-space puffs.
		auto* Smoke = LoadObject<UBrPenaltyDefinition>(nullptr, TEXT("/Game/BrGame/Penalties/DA_Penalty_Smoke.DA_Penalty_Smoke"));
		Target->Penalty.Definition = Smoke;
		Target->Penalty.StartTime = UE_SMALL_NUMBER; Target->Penalty.EndTime = 30;
		Target->Cosmetics();
		TestEqual(TEXT("Smoke pool cap"), Target->Puffs.Num(), 16);
		Pawns[0]->GetMesh()->SetHiddenInGame(true);
		Target->Cosmetics();
		for (UStaticMeshComponent* Puff : Target->Puffs) TestFalse(TEXT("Fog hides lingering puffs"), Puff->IsVisible());
		TestFalse(TEXT("Fog hides tube"), Target->Tube->IsVisible());
		Pawns[0]->GetMesh()->SetHiddenInGame(false);
	}
	// One delayed service frame crosses voluntary < plant < revive < timeout.
	// A mapless respawn can fail placement; regardless, it must never choose the later revive.
	Target->ResetRound();
	Pawns[0]->GetLife()->RestoreDowned(25);
	A->StartPlant(); A->PlantEndTime = 15;
	Pawns[0]->GetLife()->Reviver = Pawns[2];
	Pawns[0]->GetLife()->ReviveEndTime = 20;
	Pawns[0]->GetLife()->RespawnHoldEndTime = 10;
	const int32 BeforeLateFrame = Mode->RoundRecords.FindChecked(Keys[1]).Journal.Score;
	Pawns[0]->GetLife()->ServiceDowned(30);
	TestTrue(TEXT("Late frame never revives past an earlier voluntary deadline"),
		Pawns[0]->GetLife()->State != EBrLifeState::Alive);
	TestNull(TEXT("Earlier exit rejects later plant"), Target->Penalty.Definition.Get());
	TestEqual(TEXT("Rejected late plant gives no score"), Mode->RoundRecords.FindChecked(Keys[1]).Journal.Score, BeforeLateFrame);
	TestNull(TEXT("Voluntary resolution cancels the later revive"), Pawns[0]->GetLife()->Reviver.Get());
	Pawns[0]->GetLife()->State = EBrLifeState::Eliminated;
	Target->LifeChanged();
	TestNull(TEXT("Elimination clears item"), Target->Penalty.Definition.Get());
	Target->Restore(Snapshot); Target->ResetRound();
	TestFalse(TEXT("Round reset clears item and one-per-down"), Target->Penalty.Definition || Target->Penalty.bPlantedThisDown);
	World->DestroyWorld(false); Instance->Shutdown(); GEngine->DestroyWorldContext(World);
	return true;
}
#endif
