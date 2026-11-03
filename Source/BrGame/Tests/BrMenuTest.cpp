#include "BrMenuRules.h"
#include "BrRoundRecord.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrCorpse.h"
#include "BrCombatDefinitions.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "OnlineSubsystemTypes.h"

namespace
{
	struct FMenuTestWorld
	{
		UWorld* World = nullptr;
		UGameInstance* Instance = nullptr;
		FMenuTestWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::SM5, &Values);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			Instance = NewObject<UGameInstance>(GEngine);
			Context.OwningGameInstance = Instance;
			World->SetGameInstance(Instance);
			Instance->Init();
		}
		~FMenuTestWorld()
		{
			World->DestroyWorld(false);
			Instance->Shutdown();
			GEngine->DestroyWorldContext(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMenuBindingTest, "BrGame.Menu.BindingConflicts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrMenuBindingTest::RunTest(const FString& Parameters)
{
	TMap<FName, FKey> Keys = {{TEXT("Br.Fire"), EKeys::LeftMouseButton}, {TEXT("Br.Menu"), EKeys::Escape}};
	FString Error;
	TestFalse(TEXT("Conflict rejected"), FBrMenuRules::CanBind(TEXT("Br.Menu"), EKeys::LeftMouseButton, Keys, Error));
	TestTrue(TEXT("Current key allowed"), FBrMenuRules::CanBind(TEXT("Br.Menu"), EKeys::Escape, Keys, Error));
	Keys[TEXT("Br.Menu")] = EKeys::F10;
	TestFalse(TEXT("Escape reserved even after Menu rebind"), FBrMenuRules::CanBind(TEXT("Br.Fire"), EKeys::Escape, Keys, Error));
	TestTrue(TEXT("Menu default may restore Escape"), FBrMenuRules::CanBind(TEXT("Br.Menu"), EKeys::Escape, Keys, Error));
	TestTrue(TEXT("Mouse binding allowed"), FBrMenuRules::CanBind(TEXT("Br.Fire"), EKeys::RightMouseButton, Keys, Error));
	TestFalse(TEXT("Console tilde reserved"), FBrMenuRules::CanBind(TEXT("Br.Fire"), EKeys::Tilde, Keys, Error));
	TestFalse(TEXT("Console period reserved"), FBrMenuRules::CanBind(TEXT("Br.Fire"), EKeys::Period, Keys, Error));
	TestTrue(TEXT("Zoom wheel axis"), FBrMenuRules::CanBind(TEXT("Zoom"), EKeys::MouseWheelAxis, Keys, Error));
	TestFalse(TEXT("Non zoom cannot use axis"), FBrMenuRules::CanBind(TEXT("Br.Fire"), EKeys::MouseWheelAxis, Keys, Error));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMenuSameFrameForfeitTest, "BrGame.Menu.SameFrameForfeits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrMenuSameFrameForfeitTest::RunTest(const FString& Parameters)
{
	FMenuTestWorld Fixture;
	UWorld* World = Fixture.World;
	ABrGameMode* Mode = World->SpawnActor<ABrGameMode>();
	World->InitializeActorsForPlay(FURL());
	ABrGameState* GS = Mode->GetGameState<ABrGameState>();
	if (!TestNotNull(TEXT("Game state"), GS)) return false;
	ABrPlayerController* Players[2];
	UBrWeaponDefinition* Weapon = NewObject<UBrWeaponDefinition>(World);
	for (int32 I = 0; I < 2; ++I)
	{
		Players[I] = World->SpawnActor<ABrPlayerController>();
		ABrPlayerState* PS = World->SpawnActor<ABrPlayerState>();
		Players[I]->PlayerState = PS;
		PS->SetOwner(Players[I]);
		PS->SetPlayerId(I + 1);
		const FUniqueNetIdPtr Id = FUniqueNetIdString::Create(
			FString::Printf(TEXT("MenuTest%d"), I), TEXT("NULL"));
		PS->SetUniqueId(FUniqueNetIdRepl(Id));
		Mode->AdmittedControllers.Add(Players[I]);
		Mode->Participants.Add(I + 1);
		FBrRoundRecord Record;
		Record.ActiveController = Players[I];
		Record.PlayerId = I + 1;
		Record.PlayerName = FString::Printf(TEXT("Contestant%d"), I);
		Record.bHasPawnSnapshot = true;
		Record.GroundLocation = FVector(I * 200, 0, 0);
		Record.Gear.Weapon = Weapon;
		Mode->RoundRecords.Add(Mode->RoundKey(Players[I]), Record);
	}
	TestTrue(TEXT("First forfeit accepted"), Mode->ForfeitPlayer(Players[0]));
	TestFalse(TEXT("No premature winner"), GS->bResults);
	TestTrue(TEXT("Second forfeit in same frame accepted"), Mode->ForfeitPlayer(Players[1]));
	for (ABrPlayerController* PC : Players)
	{
		TestTrue(TEXT("Immediately eliminated"), PC->GetPlayerState<ABrPlayerState>()->bEliminated);
		const FBrRoundRecord& Record = Mode->RoundRecords.FindChecked(Mode->RoundKey(PC));
		TestTrue(TEXT("Ledger/corpse committed"), Record.bForfeited && Record.Life.bEliminated && Record.bOfflineCorpseLeft);
	}
	int32 Corpses = 0;
	for (TActorIterator<ABrCorpse> It(World); It; ++It)
		if (It->GetGear().Weapon == Weapon) ++Corpses;
	TestEqual(TEXT("Both geared corpses left immediately"), Corpses, 2);
	Mode->FlowService();
	TestTrue(TEXT("Deferred results"), GS->bResults);
	TestEqual(TEXT("Both forfeits produce draw"), GS->WinnerText, FString(TEXT("Draw")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrUnstuckWallTest, "BrGame.Unstuck.BlockingWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrUnstuckWallTest::RunTest(const FString& Parameters)
{
	FMenuTestWorld Fixture;
	UWorld* World = Fixture.World;
	auto Box = [&](const FVector& Position, const FVector& Extent, FName Profile)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Component = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Component);
		Component->SetBoxExtent(Extent);
		Component->SetCollisionProfileName(Profile);
		Component->RegisterComponent();
		Actor->SetActorLocation(Position);
		return Component;
	};
	const FVector Origin(0, 0, 100), Across(400, 0, 104), Open(0, 400, 104);
	Box(FVector(200, 0, 140), FVector(10, 300, 140), TEXT("BlockAll"));
	TestFalse(TEXT("280cm wall rejects clear destination beyond it"),
		FBrUnstuckRules::PathClear(World, nullptr, Origin, Across, 40, 90));
	TestTrue(TEXT("Nearest open direction still reachable"),
		FBrUnstuckRules::PathClear(World, nullptr, Origin, Open, 40, 90));
	UBoxComponent* Embedded = Box(Origin, FVector(10, 10, 20), TEXT("BlockAll"));
	TestTrue(TEXT("Origin-embedded component ignored for escape"),
		FBrUnstuckRules::PathClear(World, nullptr, Origin, Open, 40, 90));
	TestFalse(TEXT("Embedded origin does not ignore separate wall"),
		FBrUnstuckRules::PathClear(World, nullptr, Origin, Across, 40, 90));
	Embedded->DestroyComponent();
	Box(FVector(0, 200, 55), FVector(300, 10, 55), TEXT("BrLowCover"));
	TestFalse(TEXT("110cm movement-blocking cover rejects crossing"),
		FBrUnstuckRules::PathClear(World, nullptr, Origin, Open, 40, 90));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMenuForfeitTest, "BrGame.Menu.ForfeitLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrMenuForfeitTest::RunTest(const FString& Parameters)
{
	FBrRoundRecord Record;
	Record.Life.Deaths = 3;
	Record.State = EBrLifeState::Downed;
	TestTrue(TEXT("Intentional leave forfeits immediately"), FBrReconnectRules::Forfeit(Record));
	TestTrue(TEXT("Eliminated"), Record.Life.bEliminated && Record.State == EBrLifeState::Eliminated);
	TestTrue(TEXT("Fresh rejoin cannot resurrect"), FBrReconnectRules::Merge(Record.Life, FBrLifeBookkeeping()).bEliminated);
	TestFalse(TEXT("Duplicate leave inert"), FBrReconnectRules::Forfeit(Record));
	TestEqual(TEXT("No respawn death penalty"), Record.Life.Deaths, 3);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrUnstuckCancelTest, "BrGame.Unstuck.CancelConditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrUnstuckCancelTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Match alive"), FBrUnstuckRules::Eligible(true, true, true, false));
	TestFalse(TEXT("Lobby/results"), FBrUnstuckRules::Eligible(false, true, true, false));
	TestFalse(TEXT("Downed/eliminated/spectating"), FBrUnstuckRules::Eligible(true, true, false, false));
	TestFalse(TEXT("Sudden death"), FBrUnstuckRules::Eligible(true, true, true, true));
	TestFalse(TEXT("Not admitted"), FBrUnstuckRules::Eligible(true, false, true, false));
	TestFalse(TEXT("Still within tolerance"), FBrUnstuckRules::Cancelled(true, false, false, false, 100, 10, false));
	TestTrue(TEXT("Moving"), FBrUnstuckRules::Cancelled(true, false, false, false, 101, 10, false));
	TestTrue(TEXT("Damage"), FBrUnstuckRules::Cancelled(true, true, false, false, 0, 10, false));
	TestTrue(TEXT("Fire/throw"), FBrUnstuckRules::Cancelled(true, false, true, false, 0, 10, false));
	TestTrue(TEXT("Posture"), FBrUnstuckRules::Cancelled(true, false, false, true, 0, 10, false));
	TestTrue(TEXT("Zone change"), FBrUnstuckRules::Cancelled(true, false, false, false, 0, 10, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrUnstuckCooldownTest, "BrGame.Unstuck.CooldownAndZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrUnstuckCooldownTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No spot retains cooldown"), FBrUnstuckRules::CompletionCooldown(false, 12, 20, 60), 12.f);
	TestEqual(TEXT("Success consumes cooldown"), FBrUnstuckRules::CompletionCooldown(true, 12, 20, 60), 80.f);
	TestEqual(TEXT("Remaining"), FBrUnstuckRules::Remaining(80, 25), 55.f);
	TestEqual(TEXT("Expired"), FBrUnstuckRules::Remaining(80, 90), 0.f);
	TestTrue(TEXT("Inside stays inside"), FBrUnstuckRules::ZoneAllowed(true, true));
	TestTrue(TEXT("Outside stays outside"), FBrUnstuckRules::ZoneAllowed(false, false));
	TestFalse(TEXT("Cannot escape zone"), FBrUnstuckRules::ZoneAllowed(false, true));
	TestFalse(TEXT("Cannot move out of safety"), FBrUnstuckRules::ZoneAllowed(true, false));
	FBrRoundRecord Record;
	Record.UnstuckCooldownEnd = 80;
	Record.Life.Deaths++;
	TestEqual(TEXT("Round ledger survives respawn"), Record.UnstuckCooldownEnd, 80.f);
	Record = FBrRoundRecord();
	TestEqual(TEXT("New round resets"), Record.UnstuckCooldownEnd, 0.f);
	return true;
}
#endif
