#include "BrQuestRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrCharacter.h"
#include "BrCombatDefinitions.h"
#include "BrDamageSource.h"
#include "BrDeveloperSettings.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrHealthComponent.h"
#include "BrInventoryComponent.h"
#include "BrLifeComponent.h"
#include "BrPickup.h"
#include "BrPickupManager.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "OnlineSubsystemTypes.h"

namespace
{
	FBrQuestOffer QuestOffer(EBrQuestObjective Type, UBrWeaponDefinition* Weapon = nullptr)
	{
		FBrQuestOffer Offer;
		Offer.Id = FGuid::NewGuid();
		Offer.Objective = Type;
		Offer.Weapon = Weapon;
		return Offer;
	}

	struct FQuestTestWorld
	{
		UWorld* World = nullptr;
		UGameInstance* Instance = nullptr;
		FQuestTestWorld()
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
			FURL URL;
			URL.AddOption(TEXT("game=/Script/BrGame.BrGameMode"));
			World->SetGameMode(URL);
			World->InitializeActorsForPlay(URL);
		}
		~FQuestTestWorld()
		{
			World->DestroyWorld(false);
			Instance->Shutdown();
			GEngine->DestroyWorldContext(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrQuestSelectionTest, "BrGame.Quest.OfferSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrQuestSelectionTest::RunTest(const FString&)
{
	FRandomStream A(20), B(20);
	int32 Counts[3] = {}, One = 0, Two = 0, DuplicatePairs = 0;
	for (int32 I = 0; I < 2000; ++I)
	{
		const int32 Count = FBrQuestRules::OfferCount(A);
		TestEqual(TEXT("Seeded offer count"), Count, FBrQuestRules::OfferCount(B));
		if (Count == 1) ++One;
		else if (Count == 2) ++Two;
		else { AddError(TEXT("Offer count escaped 1..2")); return false; }
		const EBrQuestObjective First = FBrQuestRules::SelectObjective(A);
		TestTrue(TEXT("Seeded objective"), First == FBrQuestRules::SelectObjective(B));
		++Counts[static_cast<int32>(First)];
		if (Count == 2)
		{
			const EBrQuestObjective Second = FBrQuestRules::SelectObjective(A);
			TestTrue(TEXT("Seeded second objective"), Second == FBrQuestRules::SelectObjective(B));
			if (First == Second) ++DuplicatePairs;
		}
		const int32 Weapon = FBrQuestRules::SelectWeapon(A, 3);
		TestEqual(TEXT("Seeded target"), Weapon, FBrQuestRules::SelectWeapon(B, 3));
		TestTrue(TEXT("Target in validated registry"), Weapon >= 0 && Weapon < 3);
	}
	TestTrue(TEXT("Both counts and all types selected; duplicate types allowed"),
		One > 0 && Two > 0 && Counts[0] > 0 && Counts[1] > 0 && Counts[2] > 0 && DuplicatePairs > 0);
	TestEqual(TEXT("Empty registry cannot select target"), FBrQuestRules::SelectWeapon(A, 0), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrQuestProgressTest, "BrGame.Quest.ProgressPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrQuestProgressTest::RunTest(const FString&)
{
	FBrQuestJournal Journal;
	UBrWeaponDefinition* Target = NewObject<UBrWeaponDefinition>();
	UBrWeaponDefinition* Other = NewObject<UBrWeaponDefinition>();
	TestFalse(TEXT("Invalid identity rejected"), FBrQuestRules::Accept(Journal, {}));
	TestFalse(TEXT("Missing fixed target rejected"),
		FBrQuestRules::Accept(Journal, QuestOffer(EBrQuestObjective::CollectWeapon)));
	TestEqual(TEXT("Pre-accept events have no progress"),
		FBrQuestRules::Progress(Journal, EBrQuestObjective::CollectGrenade), 0);
	const FBrQuestOffer First = QuestOffer(EBrQuestObjective::CollectGrenade);
	TestTrue(TEXT("Accept first"), FBrQuestRules::Accept(Journal, First));
	TestFalse(TEXT("Cannot accept same offer again"), FBrQuestRules::Accept(Journal, First));
	FBrQuestRules::Accept(Journal, QuestOffer(EBrQuestObjective::CollectGrenade));
	FBrQuestRules::Accept(Journal, QuestOffer(EBrQuestObjective::CollectWeapon, Target));
	FBrQuestRules::Accept(Journal, QuestOffer(EBrQuestObjective::DownOpponent));
	TestEqual(TEXT("One event completes duplicate objectives"), FBrQuestRules::Progress(Journal, EBrQuestObjective::CollectGrenade), 2);
	TestEqual(TEXT("Each duplicate awards +2"), Journal.Score, 4);
	TestEqual(TEXT("No repeated completion"), FBrQuestRules::Progress(Journal, EBrQuestObjective::CollectGrenade), 0);
	TestEqual(TEXT("Other weapon does not progress"), FBrQuestRules::Progress(Journal, EBrQuestObjective::CollectWeapon, Other), 0);
	TestEqual(TEXT("Fixed target completes"), FBrQuestRules::Progress(Journal, EBrQuestObjective::CollectWeapon, Target), 1);
	TestEqual(TEXT("Down itself completes"), FBrQuestRules::Progress(Journal, EBrQuestObjective::DownOpponent), 1);
	TestEqual(TEXT("Four completions"), Journal.Score, 8);
	TestTrue(TEXT("Original weapon target unchanged"), Journal.Quests[2].Offer.Weapon == Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrQuestScoreTest, "BrGame.Quest.ScoreEpisodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrQuestScoreTest::RunTest(const FString&)
{
	FBrQuestJournal Journal;
	FBrScoreEpisode Episode;
	TestTrue(TEXT("First down"), FBrQuestRules::Down(Journal, Episode, TEXT("trusted")));
	TestFalse(TEXT("Duplicate down inert"), FBrQuestRules::Down(Journal, Episode, TEXT("other")));
	TestEqual(TEXT("Down penalty"), Journal.Score, -1);
	FBrQuestRules::Revive(Episode);
	TestEqual(TEXT("Revive does not refund down penalty"), Journal.Score, -1);
	TestTrue(TEXT("New down episode"), FBrQuestRules::Down(Journal, Episode, TEXT("trusted")));
	TestTrue(TEXT("Respawn awards no killer credit"), FBrQuestRules::Resolve(Journal, Episode, false).IsEmpty());
	TestEqual(TEXT("Down and respawn penalties stack"), Journal.Score, -3);
	FBrQuestRules::Resolve(Journal, Episode, false);
	TestEqual(TEXT("Respawn charged once"), Journal.Score, -3);
	FBrQuestRules::Down(Journal, Episode, TEXT("trusted"));
	TestEqual(TEXT("Final unresolved down credits stable identity"),
		FBrQuestRules::Resolve(Journal, Episode, true), FString(TEXT("trusted")));
	TestEqual(TEXT("Final penalty stacks independently"), Journal.Score, -5);
	TestTrue(TEXT("Duplicate final credit inert"), FBrQuestRules::Resolve(Journal, Episode, true).IsEmpty());
	TestEqual(TEXT("Duplicate final penalty inert"), Journal.Score, -5);
	FBrQuestJournal Zone;
	FBrScoreEpisode ZoneEpisode;
	FBrQuestRules::Down(Zone, ZoneEpisode, FString());
	TestTrue(TEXT("Self/zone/team attribution never credits"), FBrQuestRules::Resolve(Zone, ZoneEpisode, true).IsEmpty());
	TestEqual(TEXT("Victim still charged twice"), Zone.Score, -2);
	FBrRoundRecord Record;
	Record.Journal = Journal;
	Record.ScoreEpisode = Episode;
	const FBrRoundRecord Reconnected = Record;
	TestEqual(TEXT("Signed ledger copy is exact"), Reconnected.Journal.Score, -5);
	Record = FBrRoundRecord();
	TestEqual(TEXT("New round score zero"), Record.Journal.Score, 0);
	TestTrue(TEXT("New round clears objectives/episode"), Record.Journal.Quests.IsEmpty() && !Record.ScoreEpisode.bFinalCharged);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrQuestRuntimeTest, "BrGame.Quest.AuthorityLedgerAndPickups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrQuestRuntimeTest::RunTest(const FString&)
{
	FQuestTestWorld Fixture;
	UWorld* World = Fixture.World;
	ABrGameMode* Mode = World->GetAuthGameMode<ABrGameMode>();
	if (!TestNotNull(TEXT("Authoritative mode"), Mode)) return false;
	ABrGameState* GS = Mode->GetGameState<ABrGameState>();
	if (!TestNotNull(TEXT("Game state"), GS)) return false;
	GS->bLobby = GS->bResults = false;
	GS->MatchSettings.Mode = EBrMode::AllVsAll;
	ABrPlayerController* Players[2];
	ABrPlayerState* States[2];
	ABrCharacter* Pawns[2];
	FString Keys[2];
	for (int32 I = 0; I < 2; ++I)
	{
		Players[I] = World->SpawnActor<ABrPlayerController>();
		States[I] = World->SpawnActor<ABrPlayerState>();
		Pawns[I] = World->SpawnActor<ABrCharacter>(FVector(I * 500.f, 0, 100), FRotator::ZeroRotator);
		if (!Players[I] || !States[I] || !Pawns[I]) return false;
		Players[I]->PlayerState = States[I];
		States[I]->SetOwner(Players[I]);
		States[I]->SetPlayerId(I + 1);
		const FUniqueNetIdPtr Id = FUniqueNetIdString::Create(
			FString::Printf(TEXT("QuestTest%d"), I), TEXT("NULL"));
		States[I]->SetUniqueId(FUniqueNetIdRepl(Id));
		Mode->AdmittedControllers.Add(Players[I]);
		Players[I]->Possess(Pawns[I]);
		// Begin components only; don't start a mapless match/zone/drop scheduler.
		Pawns[I]->DispatchBeginPlay();
		Keys[I] = Mode->RoundKey(Players[I]);
		if (!TestFalse(TEXT("Identity available (run -nosteam)"), Keys[I].IsEmpty())) return false;
		FBrRoundRecord Record;
		Record.ActiveController = Players[I];
		Record.PlayerId = I + 1;
		Mode->RoundRecords.Add(Keys[I], Record);
	}
	UBrQuestPickupDefinition* Definition = NewObject<UBrQuestPickupDefinition>(World);
	UBrPickupDefinition* Cosmetic = UBrDeveloperSettings::Get()->DroppedWeaponPickup.LoadSynchronous();
	if (!TestNotNull(TEXT("Existing pickup cosmetic template"), Cosmetic)) return false;
	Definition->Mesh = Cosmetic->Mesh;
	Definition->Material = Cosmetic->Material;
	FBrPickupPayload Payload;
	Payload.QuestOffer = QuestOffer(EBrQuestObjective::CollectGrenade);
	ABrPickup* Token = ABrPickup::Spawn(World, FTransform(FVector(1000, 0, 35)), Definition, Payload);
	if (!TestNotNull(TEXT("Quest token"), Token)) return false;
	TestFalse(TEXT("Offers never auto-collect"), Token->TryAutoCollect(Pawns[0]));
	TestTrue(TEXT("First E collection accepts"), Token->TryCollect(Pawns[0]));
	TestFalse(TEXT("Same token cannot be consumed twice"), Token->TryCollect(Pawns[1]));
	TestFalse(TEXT("Same identity globally consumed"), Mode->AcceptQuest(Players[1], Payload.QuestOffer));
	TestEqual(TEXT("Acceptance awards nothing"), States[0]->RoundScore, 0);
	TestEqual(TEXT("Second player has no quest"), States[1]->Quests.Num(), 0);
	Mode->AcceptQuest(Players[0], QuestOffer(EBrQuestObjective::CollectGrenade));
	UBrInventoryComponent* Inventory = Pawns[0]->GetInventory();
	UBrThrowableDefinition* Grenade = UBrDeveloperSettings::Get()->HandGrenade.LoadSynchronous();
	if (!TestNotNull(TEXT("Configured hand grenade"), Grenade)) return false;
	Inventory->Throwable = Grenade;
	Inventory->Grenades = Grenade->MaxCarried;
	UBrGrenadePickupDefinition* GrenadePickup = NewObject<UBrGrenadePickupDefinition>(World);
	GrenadePickup->Mesh = Cosmetic->Mesh;
	GrenadePickup->Material = Cosmetic->Material;
	ABrPickup* GrenadeActor = ABrPickup::Spawn(World, FTransform(FVector(1200, 0, 35)), GrenadePickup, {});
	if (!TestNotNull(TEXT("Grenade pickup"), GrenadeActor)) return false;
	TestFalse(TEXT("Failed pickup at cap"), GrenadeActor->TryCollect(Pawns[0]));
	TestEqual(TEXT("Failed Apply has no quest progress"), States[0]->RoundScore, 0);
	const FBrInventoryState Saved = Inventory->CaptureState();
	Inventory->RestoreState(Saved);
	TestEqual(TEXT("Restored inventory has no pickup progress"), States[0]->RoundScore, 0);
	Inventory->Grenades = 0;
	TestTrue(TEXT("Successful grenade progresses"), GrenadeActor->TryCollect(Pawns[0]));
	TestEqual(TEXT("One pickup completes duplicate accepted quests"), States[0]->RoundScore, 4);
	UBrWeaponDefinition* Target = UBrDeveloperSettings::Get()->StartingWeapon.LoadSynchronous();
	if (!TestNotNull(TEXT("Valid target weapon"), Target)) return false;
	const FBrQuestOffer WeaponQuest = QuestOffer(EBrQuestObjective::CollectWeapon, Target);
	TestTrue(TEXT("Weapon quest accepted"), Mode->AcceptQuest(Players[0], WeaponQuest));
	Mode->ProgressQuest(Players[0], EBrQuestObjective::CollectWeapon, NewObject<UBrWeaponDefinition>(World));
	TestFalse(TEXT("Wrong weapon leaves target incomplete"), States[0]->Quests.Last().bComplete);
	UBrWeaponPickupDefinition* WeaponPickup = NewObject<UBrWeaponPickupDefinition>(World);
	WeaponPickup->Weapon = Target;
	WeaponPickup->Mesh = Cosmetic->Mesh;
	WeaponPickup->Material = Cosmetic->Material;
	Inventory->Weapon = nullptr; // No outgoing weapon/drop manager needed in this fixture.
	ABrPickup* WeaponActor = ABrPickup::Spawn(World, FTransform(FVector(1400, 0, 35)), WeaponPickup, {});
	if (!TestNotNull(TEXT("Weapon actor"), WeaponActor)) return false;
	TestTrue(TEXT("Successful weapon collection"), WeaponActor->TryCollect(Pawns[0]));
	TestEqual(TEXT("Fixed weapon awards +2"), States[0]->RoundScore, 6);
	TestTrue(TEXT("Target remains fixed"), States[0]->Quests.Last().Offer.Weapon == Target);
	Mode->AcceptQuest(Players[0], QuestOffer(EBrQuestObjective::DownOpponent));
	const FBrDamageSource Source = Mode->CaptureDamageSource(Players[0]);
	TestTrue(TEXT("Self has no down credit"), Mode->CaptureOpponentDowner(Source, Pawns[0], false).IsEmpty());
	TestTrue(TEXT("Zone has no down credit"), Mode->CaptureOpponentDowner(Source, Pawns[1], true).IsEmpty());
	GS->MatchSettings.Mode = EBrMode::Grouped;
	States[0]->Team = States[1]->Team = 0;
	const FBrDamageSource TeamSource = Mode->CaptureDamageSource(Players[0]);
	TestTrue(TEXT("Team damage has no quest/score credit"), Mode->CaptureOpponentDowner(TeamSource, Pawns[1], false).IsEmpty());
	GS->MatchSettings.Mode = EBrMode::AllVsAll;
	FBrDamageEvent Event(Source, UBrBulletDamageType::StaticClass());
	Pawns[1]->TakeDamage(0, Event, Players[0], Pawns[0]);
	TestEqual(TEXT("Rejected damage cannot progress down quest"), States[0]->RoundScore, 6);
	Pawns[1]->TakeDamage(10000, Event, Players[0], Pawns[0]);
	TestTrue(TEXT("Damage delegate actually downed opponent"), Pawns[1]->GetLife()->State == EBrLifeState::Downed);
	TestEqual(TEXT("Down, not kill, completes quest"), States[0]->RoundScore, 8);
	TestEqual(TEXT("Victim down penalty"), States[1]->RoundScore, -1);
	Pawns[1]->GetLife()->Eliminate();
	TestEqual(TEXT("Final downer gets +1"), States[0]->RoundScore, 9);
	TestEqual(TEXT("Final elimination stacks -1"), States[1]->RoundScore, -2);
	Pawns[1]->GetLife()->Eliminate();
	TestEqual(TEXT("Duplicate elimination inert"), States[0]->RoundScore, 9);

	// Offline victim episode resolving without any pawn/controller reference.
	FBrRoundRecord& Offline = Mode->RoundRecords.FindChecked(Keys[1]);
	Offline.Life = {};
	Offline.State = EBrLifeState::Downed;
	Offline.ScoreEpisode = {};
	FBrQuestRules::Down(Offline.Journal, Offline.ScoreEpisode, Keys[0]);
	Offline.ActiveController.Reset();
	Offline.DownedEndTime = 0;
	Offline.ReconnectDeadline = 100;
	Offline.bHasPawnSnapshot = false;
	// Force final outcome via the death limit, regardless of timeout-count tuning.
	Offline.Life.Deaths = MAX_int32;
	Mode->ServiceRoundRecord(Keys[1], Offline);
	const int32 OfflineScore = Offline.Journal.Score;
	TestEqual(TEXT("Offline final credited once"), States[0]->RoundScore, 10);
	Mode->ServiceRoundRecord(Keys[1], Offline);
	TestEqual(TEXT("Repeated offline service inert"), Offline.Journal.Score, OfflineScore);
	TestTrue(TEXT("Reconnect ledger restored"), Mode->PreparePlayer(Players[1]) == false);
	TestEqual(TEXT("Signed reconnect restore is exact"), States[1]->RoundScore, OfflineScore);
	TestTrue(TEXT("Reconnect retains final state"), States[1]->bEliminated);
	States[0]->RoundScore = 999;
	States[0]->Quests.Reset();
	Mode->RoundRecords.FindChecked(Keys[0]).ActiveController.Reset();
	TestTrue(TEXT("Live contender reconnect"), Mode->PreparePlayer(Players[0]));
	TestEqual(TEXT("Stale positive PS never Max-merges"), States[0]->RoundScore, 10);
	TestEqual(TEXT("Full quest array restored"), States[0]->Quests.Num(), 4);
	ABrPlayerState* FreshLogin = World->SpawnActor<ABrPlayerState>();
	ABrPlayerState* InactiveCopy = World->SpawnActor<ABrPlayerState>();
	if (!FreshLogin || !InactiveCopy) return false;
	// A real reconnect supplies the same login identity. Super::OverrideWith
	// copies it, so an unset fixture ID would erase subsequent ledger lookup.
	FreshLogin->SetUniqueId(States[1]->GetUniqueId());
	States[1]->OverrideWith(FreshLogin);
	TestEqual(TEXT("Fresh reconnect preserves ledger identity"), Mode->RoundKey(Players[1]), Keys[1]);
	TestEqual(TEXT("Fresh PS defaults cannot erase retained negative score"), States[1]->RoundScore, OfflineScore);
	States[0]->CopyProperties(InactiveCopy);
	TestEqual(TEXT("Inactive PS score copy exact"), InactiveCopy->RoundScore, 10);
	TestEqual(TEXT("Inactive PS copies full completed journal"), InactiveCopy->Quests.Num(), 4);
	FBrRoundRecord OfflineRespawn;
	OfflineRespawn.State = EBrLifeState::Downed;
	OfflineRespawn.DownedEndTime = 0;
	OfflineRespawn.ReconnectDeadline = 100;
	FBrQuestRules::Down(OfflineRespawn.Journal, OfflineRespawn.ScoreEpisode, Keys[0]);
	Mode->ServiceRoundRecord(TEXT("offline-respawn-fixture"), OfflineRespawn);
	// MaxDeaths=0 intentionally selects final elimination rather than a respawn.
	const bool bCanRespawn = UBrDeveloperSettings::Get()->MaxDeaths > 0;
	TestEqual(TEXT("Offline death/respawn always stacks a penalty"), OfflineRespawn.Journal.Score, -2);
	TestEqual(TEXT("Offline respawn never awards +1"), States[0]->RoundScore, bCanRespawn ? 10 : 11);
	Mode->ServiceRoundRecord(TEXT("offline-respawn-fixture"), OfflineRespawn);
	TestEqual(TEXT("Offline respawn charge once"), OfflineRespawn.Journal.Score, -2);
	OfflineRespawn.ReconnectDeadline = 0;
	Mode->ServiceRoundRecord(TEXT("offline-respawn-fixture"), OfflineRespawn);
	TestEqual(TEXT("Grace forfeit adds a separate final penalty only if not already final"),
		OfflineRespawn.Journal.Score, bCanRespawn ? -3 : -2);
	TestTrue(TEXT("Grace forfeit clears attribution"), OfflineRespawn.ScoreEpisode.DownerKey.IsEmpty());
	const int32 BeforeForfeit = States[0]->RoundScore;
	TestTrue(TEXT("Intentional forfeit"), Mode->ForfeitPlayer(Players[0]));
	TestEqual(TEXT("Alive forfeit charged once"), States[0]->RoundScore, BeforeForfeit - 1);
	Mode->ForfeitPlayer(Players[0]);
	TestEqual(TEXT("Repeated forfeit inert"), States[0]->RoundScore, BeforeForfeit - 1);

	// Exhausted pawnless spawn failure is another final path. Reset this fixture
	// record's life only, then exercise the actual bounded failure handler.
	FBrRoundRecord& SpawnFailure = Mode->RoundRecords.FindChecked(Keys[1]);
	Players[1]->UnPossess();
	SpawnFailure.Life = {};
	SpawnFailure.bForfeited = false;
	SpawnFailure.State = EBrLifeState::Alive;
	SpawnFailure.ScoreEpisode = {};
	SpawnFailure.SpawnRetries.Failures = FBrSpawnRetryBudget::MaxAttempts - 1;
	const int32 BeforeFailure = SpawnFailure.Journal.Score;
	Mode->FailedSpawn(Players[1]);
	TestEqual(TEXT("Exhausted pawnless spawn failure final penalty"), SpawnFailure.Journal.Score, BeforeFailure - 1);
	Mode->FailedSpawn(Players[1]);
	TestEqual(TEXT("Repeated spawn failure cannot charge"), SpawnFailure.Journal.Score, BeforeFailure - 1);

	ABrPickupManager* Manager = World->SpawnActor<ABrPickupManager>();
	ABrPickup* Expiring = ABrPickup::Spawn(World, FTransform(FVector(1600, 0, 35)), Definition, Payload);
	if (!TestNotNull(TEXT("Cleanup manager"), Manager) || !TestNotNull(TEXT("Cleanup offer"), Expiring)) return false;
	Mode->PickupManager = Manager;
	Manager->RegisterRoundDrop(Expiring);
	Expiring->SetLifeSpan(60);
	TestTrue(TEXT("Offers support crate lifetime"), Expiring->GetLifeSpan() > 0);
	Mode->ClearRoundRecords();
	TestTrue(TEXT("Round reset destroys offers"), Expiring->IsActorBeingDestroyed());
	TestTrue(TEXT("Reset ledger and consumed identities"), Mode->RoundRecords.IsEmpty() && Mode->AcceptedQuestOffers.IsEmpty());
	TestTrue(TEXT("Reset player quests"), States[0]->Quests.IsEmpty());
	TestEqual(TEXT("Reset player score"), States[0]->RoundScore, 0);
	return true;
}
#endif
