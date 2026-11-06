#include "BrArmorRules.h"
#include "BrCrateRules.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrArmorComponent.h"
#include "BrCharacter.h"
#include "BrCombatDefinitions.h"
#include "BrDamageSource.h"
#include "BrDeveloperSettings.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrHealthComponent.h"
#include "BrHitFeedbackComponent.h"
#include "BrLifeComponent.h"
#include "BrPickupDefinition.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrZoneDamageType.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSubsystemTypes.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrArmorAbsorptionTest, "BrGame.Armor.MostDamagedFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrArmorAbsorptionTest::RunTest(const FString&)
{
	TArray<float> Plates = {20, 7};
	auto Hit = FBrArmorRules::Absorb(Plates, 5);
	TestEqual(TEXT("All damage absorbed"), Hit.Absorbed, 5.f);
	TestEqual(TEXT("Health unchanged"), Hit.Remainder, 0.f);
	TestEqual(TEXT("Intact plate untouched"), Plates[0], 20.f);
	TestEqual(TEXT("Lowest plate first regardless of index"), Plates[1], 2.f);
	Hit = FBrArmorRules::Absorb(Plates, 12);
	TestEqual(TEXT("One break"), Hit.Broken, 1);
	TestEqual(TEXT("Carry-over hits next plate"), Plates[0], 10.f);
	Hit = FBrArmorRules::Absorb(Plates, 15);
	TestEqual(TEXT("Carry-over to health in same hit"), Hit.Remainder, 5.f);
	TestEqual(TEXT("Absorbed capped at remaining armor"), Hit.Absorbed, 10.f);
	TestTrue(TEXT("Destroyed slot removed"), Plates.IsEmpty());
	Plates = {3, 20};
	Hit = FBrArmorRules::Absorb(Plates, 40);
	TestEqual(TEXT("Both plates break"), Hit.Broken, 2);
	TestEqual(TEXT("Both plates and health"), Hit.Remainder, 17.f);
	Plates = {20, 6};
	Hit = FBrArmorRules::Absorb(Plates, 12, true);
	TestEqual(TEXT("Zone bypass remainder"), Hit.Remainder, 12.f);
	TestEqual(TEXT("Zone absorbs nothing"), Hit.Absorbed, 0.f);
	TestTrue(TEXT("Zone preserves exact plate values"), Plates == TArray<float>({20, 6}));
	TestEqual(TEXT("NaN damage rejected"), FBrArmorRules::Absorb(Plates,
		std::numeric_limits<float>::quiet_NaN()).Absorbed, 0.f);
	TestEqual(TEXT("Negative damage rejected"), FBrArmorRules::Absorb(Plates, -1).Remainder, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrArmorPickupTest, "BrGame.Armor.PickupGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrArmorPickupTest::RunTest(const FString&)
{
	TArray<float> Plates;
	TestTrue(TEXT("First plate"), FBrArmorRules::AddPlate(Plates, 2, 20));
	TestTrue(TEXT("Second plate"), FBrArmorRules::AddPlate(Plates, 2, 20));
	TestFalse(TEXT("Full cap refuses"), FBrArmorRules::AddPlate(Plates, 2, 20));
	FBrArmorRules::Absorb(Plates, 19);
	TestFalse(TEXT("Partial plate still occupies a slot"), FBrArmorRules::CanAddPlate(Plates, 2, 20));
	FBrArmorRules::Absorb(Plates, 1);
	TestTrue(TEXT("Destroyed plate frees a slot"), FBrArmorRules::AddPlate(Plates, 2, 20));
	TestTrue(TEXT("New plate full, old intact"), Plates == TArray<float>({20, 20}));
	Plates = {0, 7};
	TestTrue(TEXT("Legacy zero is not occupied"), FBrArmorRules::AddPlate(Plates, 2, 20));
	TestTrue(TEXT("Adding never tops up partial armor"), Plates == TArray<float>({7, 20}));
	TestFalse(TEXT("Zero capacity disables pickup"), FBrArmorRules::CanAddPlate({}, 0, 20));
	TestFalse(TEXT("Invalid value cannot create a plate"), FBrArmorRules::CanAddPlate({}, 2, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrArmorCrateTest, "BrGame.Armor.SeededCrateRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrArmorCrateTest::RunTest(const FString&)
{
	FRandomStream A(23), B(23);
	int32 Counts[7] = {};
	for (int32 I = 0; I < 14000; ++I)
	{
		const auto Roll = [](FRandomStream& Stream)
		{ return FBrCrateRules::Roll(Stream, 1, 2, .5f, 3, 8, {60,30,10}, 3, 8, 2, 4, 2, 8); };
		const auto Loot = Roll(A);
		if (Loot != Roll(B)) { AddError(TEXT("Same seed must give identical armor loot")); return false; }
		const int32 N = Loot.FilterByPredicate([](int32 Type) { return Type == FBrCrateRules::Armor; }).Num();
		if (N < 2 || N > 8) { AddError(TEXT("Armor count outside 2-8")); return false; }
		++Counts[N - 2];
	}
	for (int32 N : Counts) TestTrue(TEXT("Every uniform bucket approximately equally likely"), N > 1800 && N < 2200);
	TestEqual(TEXT("Armor appended, old indices unchanged"), static_cast<int32>(FBrCrateRules::Armor), 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrArmorPrivacyTest, "BrGame.Armor.ReplicationPrivacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrArmorPrivacyTest::RunTest(const FString&)
{
	const auto OwnerOnly = [this](const UObject* Object, const TCHAR* Name)
	{
		// Mirror FRepLayout initialization before querying native replication conditions.
		Object->GetClass()->SetUpRuntimeReplicationData();
		TArray<FLifetimeProperty> Properties;
		Object->GetLifetimeReplicatedProps(Properties);
		const FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), Name);
		if (!TestNotNull(Name, Property)) return;
		const FLifetimeProperty* Lifetime = Properties.FindByPredicate(
			[Property](const FLifetimeProperty& P) { return P.RepIndex == Property->RepIndex; });
		if (TestNotNull(TEXT("Lifetime registration"), Lifetime))
			TestTrue(TEXT("Values only replicate to this actor's owner"), Lifetime->Condition == COND_OwnerOnly);
	};
	OwnerOnly(GetDefault<UBrArmorComponent>(), TEXT("Plates"));
	OwnerOnly(GetDefault<ABrPlayerController>(), TEXT("SpectatorArmor"));
	const FProperty* Persistent = FindFProperty<FProperty>(ABrPlayerState::StaticClass(), TEXT("ArmorPlates"));
	if (TestNotNull(TEXT("PlayerState storage"), Persistent))
		TestFalse(TEXT("Public PlayerState never replicates armor"), Persistent->HasAnyPropertyFlags(CPF_Net));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrArmorRuntimeTest, "BrGame.Armor.AuthorityDamageAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrArmorRuntimeTest::RunTest(const FString&)
{
	struct FWorldFixture
	{
		UWorld* World;
		UGameInstance* Instance;
		FWorldFixture()
		{
			const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
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
		}
		~FWorldFixture()
		{
			World->DestroyWorld(false);
			Instance->Shutdown();
			GEngine->DestroyWorldContext(World);
		}
	} Fixture;
	UWorld* World = Fixture.World;
	ABrGameMode* Mode = World->GetAuthGameMode<ABrGameMode>();
	if (!TestNotNull(TEXT("Authoritative mode"), Mode)) return false;
	ABrGameState* GS = Mode->GetGameState<ABrGameState>();
	GS->bLobby = GS->bResults = false;
	GS->MatchSettings.Mode = EBrMode::AllVsAll;
	ABrPlayerController* PC = World->SpawnActor<ABrPlayerController>();
	ABrPlayerState* PS = World->SpawnActor<ABrPlayerState>();
	ABrCharacter* Pawn = World->SpawnActor<ABrCharacter>();
	if (!PC || !PS || !Pawn) return false;
	PC->PlayerState = PS;
	PS->SetOwner(PC);
	PS->SetPlayerId(1);
	const FUniqueNetIdPtr Id = FUniqueNetIdString::Create(TEXT("ArmorTest"), TEXT("NULL"));
	PS->SetUniqueId(FUniqueNetIdRepl(Id));
	Mode->AdmittedControllers.Add(PC);
	PC->Possess(Pawn);
	Pawn->DispatchBeginPlay();
	FBrRoundRecord Record;
	Record.ActiveController = PC;
	Record.PlayerId = 1;
	Mode->RoundRecords.Add(Mode->RoundKey(PC), Record);
	UBrArmorPickupDefinition* Pickup = NewObject<UBrArmorPickupDefinition>(World);
	FBrPickupPayload Payload;
	TestFalse(TEXT("Armor definition defaults to automatic"), Pickup->bRequiresInteract);
	TestTrue(TEXT("Real Apply first plate"), Pickup->Apply(Pawn, Payload));
	TestTrue(TEXT("Real Apply second plate"), Pickup->Apply(Pawn, Payload));
	ABrPlayerController* Spectator = World->SpawnActor<ABrPlayerController>();
	ABrPlayerState* SpectatorPS = World->SpawnActor<ABrPlayerState>();
	if (!Spectator || !SpectatorPS) return false;
	Spectator->PlayerState = SpectatorPS;
	SpectatorPS->SetOwner(Spectator);
	SpectatorPS->bEliminated = true;
	Mode->AdmittedControllers.Add(Spectator);
	Spectator->SpectatorTarget = PS;
	Spectator->FlowSpectatorPawn = Pawn;
	Spectator->SetViewTarget(Pawn);
	Spectator->RefreshSpectatorArmor();
	const TArray<float>* Mirror = Spectator->GetSpectatorArmor(Pawn);
	TestTrue(TEXT("Selected spectator receives exact plate snapshot"), Mirror && *Mirror == Pawn->GetArmor()->GetPlates());
	TestTrue(TEXT("Non-spectating owner has no spectator mirror"), PC->SpectatorArmor.Plates.IsEmpty());
	World->GetTimerManager().ClearTimer(Pawn->AutoPickupRetryTimer);
	Pawn->bAutoPickupRetryQueued = false;
	const FDamageEvent DebugDamage(UDamageType::StaticClass());
	TestEqual(TEXT("Debug damage counts absorbed hit"), Pawn->TakeDamage(5, DebugDamage, nullptr, nullptr), 5.f);
	TestEqual(TEXT("Armor-only hit does not damage health"), Pawn->GetHealth()->Health, 100.f);
	TestFalse(TEXT("Partial plate cannot collect"), Pickup->CanApply(Pawn, Payload));
	TestFalse(TEXT("Partial damage does not queue pickup retry"), Pawn->bAutoPickupRetryQueued);
	Mirror = Spectator->GetSpectatorArmor(Pawn);
	TestTrue(TEXT("Damage immediately updates private spectator mirror"), Mirror && *Mirror == Pawn->GetArmor()->GetPlates());
	TestEqual(TEXT("Break counts applied damage"), Pawn->TakeDamage(15, DebugDamage, nullptr, nullptr), 15.f);
	TestTrue(TEXT("Breaking plate queues stationary auto-pickup retry"), Pawn->bAutoPickupRetryQueued);
	TestTrue(TEXT("Freed slot CanApply"), Pickup->CanApply(Pawn, Payload));
	FDamageEvent ZoneDamage(UBrZoneDamageType::StaticClass());
	Pawn->TakeDamage(10, ZoneDamage, nullptr, nullptr);
	TestEqual(TEXT("Zone health damage"), Pawn->GetHealth()->Health, 90.f);
	TestEqual(TEXT("Zone preserves remaining plate"), Pawn->GetArmor()->GetPlates()[0], 20.f);
	Pawn->TakeDamage(7, DebugDamage, nullptr, nullptr);
	// Native combat types share the same armor path; self bullets are intentionally rejected.
	FDamageEvent BulletDamage(UBrBulletDamageType::StaticClass());
	FDamageEvent ExplosionDamage(UBrExplosionDamageType::StaticClass());
	Pawn->TakeDamage(1, BulletDamage, nullptr, nullptr);
	Pawn->TakeDamage(1, ExplosionDamage, nullptr, nullptr);
	TestEqual(TEXT("Bullets/explosions hit armor, not health"), Pawn->GetHealth()->Health, 90.f);
	TestEqual(TEXT("Both combat damage types absorb"), Pawn->GetArmor()->GetPlates()[0], 11.f);
	const TArray<float> Retained = Pawn->GetArmor()->GetPlates();
	TestTrue(TEXT("Authority PlayerState exact partial values"), PS->GetArmorPlates() == Retained);
	Pawn->GetLife()->Down();
	TestTrue(TEXT("Down retains armor"), Pawn->GetArmor()->GetPlates() == Retained);
	TestEqual(TEXT("Downed damage refused"), Pawn->TakeDamage(100, DebugDamage, nullptr, nullptr), 0.f);
	TestTrue(TEXT("Downed armor untouched"), Pawn->GetArmor()->GetPlates() == Retained);
	Spectator->RefreshSpectatorArmor();
	TestTrue(TEXT("Ineligible downed target clears mirror"), Spectator->SpectatorArmor.Plates.IsEmpty());
	// Exercise new-pawn possession (the shared path for RestartPlayer and deferred respawn).
	ABrCharacter* Respawn = World->SpawnActor<ABrCharacter>(FVector(500, 0, 100), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("New respawn pawn"), Respawn)) return false;
	PC->Possess(Respawn);
	Respawn->DispatchBeginPlay();
	TestTrue(TEXT("New pawn restores exact partial plate"), Respawn->GetArmor()->GetPlates() == Retained);
	Pawn->Destroy(); // Old pawn teardown must never erase new pawn storage.
	TestTrue(TEXT("Old teardown does not clear armor"), PS->GetArmorPlates() == Retained);
	PS->ResetRound();
	TestTrue(TEXT("New round clears PlayerState and current pawn"),
		PS->GetArmorPlates().IsEmpty() && Respawn->GetArmor()->GetPlates().IsEmpty());
	Pickup->Apply(Respawn, Payload);
	Respawn->TakeDamage(25, DebugDamage, nullptr, nullptr);
	TestEqual(TEXT("Armor/health overflow applied in same hit"), Respawn->GetHealth()->Health, 95.f);
	TestTrue(TEXT("Overflow destroys last plate"), Respawn->GetArmor()->GetPlates().IsEmpty());
	Pickup->Apply(Respawn, Payload);
	Respawn->GetLife()->Eliminate();
	TestTrue(TEXT("Final elimination clears both stores"),
		PS->GetArmorPlates().IsEmpty() && Respawn->GetArmor()->GetPlates().IsEmpty());
	return true;
}
#endif
