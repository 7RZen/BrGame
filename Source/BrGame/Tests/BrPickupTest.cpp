#include "BrPickupRules.h"
#include "BrMatchSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrPickup.h"
#include "BrArmorComponent.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrDeveloperSettings.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrHealthComponent.h"
#include "BrInventoryComponent.h"
#include "BrLifeComponent.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupInteractTest, "BrGame.Pickups.UserInitiatedCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupInteractTest::RunTest(const FString& Parameters)
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
	ABrPlayerController* PC = World->SpawnActor<ABrPlayerController>();
	ABrPlayerState* PS = World->SpawnActor<ABrPlayerState>();
	ABrCharacter* Pawn = World->SpawnActor<ABrCharacter>();
	if (!TestNotNull(TEXT("Controller"), PC) || !TestNotNull(TEXT("PlayerState"), PS) ||
		!TestNotNull(TEXT("Pawn"), Pawn)) return false;
	PC->PlayerState = PS;
	PS->SetOwner(PC);
	PC->Possess(Pawn);
	Pawn->DispatchBeginPlay();
	Pawn->GetBrMovement()->DisableMovement();
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	TestEqual(TEXT("Interaction radius remains 150 cm"), Settings->InteractRadius, 150.f);
	UBrInventoryComponent* Inventory = Pawn->GetInventory();
	UBrWeaponDefinition* Weapon = Settings->StartingWeapon.LoadSynchronous();
	if (!TestNotNull(TEXT("Starting weapon"), Weapon) || !TestNotNull(TEXT("Ammo type"), Weapon->AmmoType.Get())) return false;
	Inventory->Weapon = Weapon;
	Inventory->Ammo.Reset();
	UBrPickupDefinition* Cosmetic = Settings->DroppedWeaponPickup.LoadSynchronous();
	if (!TestNotNull(TEXT("Pickup cosmetics"), Cosmetic)) return false;
	UBrAmmoPickupDefinition* Ammo = NewObject<UBrAmmoPickupDefinition>(World);
	UBrHealthPickupDefinition* Health = NewObject<UBrHealthPickupDefinition>(World);
	UBrArmorPickupDefinition* Armor = NewObject<UBrArmorPickupDefinition>(World);
	const auto Spawn = [&](UBrPickupDefinition* Definition, float Distance)
	{
		Definition->Mesh = Cosmetic->Mesh;
		Definition->Material = Cosmetic->Material;
		ABrPickup* Item = ABrPickup::Spawn(World, FTransform(Pawn->GetActorLocation() + FVector(Distance, 0, 0)), Definition, {});
		if (Item) Item->DispatchBeginPlay();
		return Item;
	};
	ABrPickup* HealthItem = Spawn(Health, 0);
	ABrPickup* AmmoItem = Spawn(Ammo, 20);
	ABrPickup* ArmorItem = Spawn(Armor, 40);
	if (!HealthItem || !AmmoItem || !ArmorItem) return false;
	Pawn->GetHealth()->SetHealth(50);
	Pawn->GetCapsuleComponent()->UpdateOverlaps();
	World->Tick(LEVELTICK_All, .05f);
	TestEqual(TEXT("Touch/tick does not grant ammo"), Inventory->GetReserve(), 0);
	TestEqual(TEXT("Touch/tick does not heal"), Pawn->GetHealth()->Health, 50.f);
	TestTrue(TEXT("Touch/tick does not grant armor"), Pawn->GetArmor()->GetPlates().IsEmpty());
	TestFalse(TEXT("Trigger never generates overlap events"), AmmoItem->FindComponentByClass<USphereComponent>()->GetGenerateOverlapEvents());
	TestTrue(TEXT("Nearest usable health targets"), ABrPickup::FindInteractPickup(Pawn) == HealthItem);
	Pawn->GetHealth()->SetHealth(Pawn->GetHealth()->MaxHealth);
	TestTrue(TEXT("Full health skipped in favor of usable ammo"), ABrPickup::FindInteractPickup(Pawn) == AmmoItem);
	Pawn->GetLife()->StartInteract();
	TestEqual(TEXT("Interact grants one ammo pickup"), Inventory->GetReserve(), Weapon->MagazineSize);
	TestTrue(TEXT("One press leaves armor available"), ArmorItem->CanCollect(Pawn));
	World->Tick(LEVELTICK_All, .05f);
	TestTrue(TEXT("Holding interact never vacuums next item"), Pawn->GetArmor()->GetPlates().IsEmpty());
	TestTrue(TEXT("Next press targets armor"), ABrPickup::FindInteractPickup(Pawn) == ArmorItem);

	// A valid revive wins over an otherwise usable pickup.
	ABrCharacter* Target = World->SpawnActor<ABrCharacter>(FVector(100, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Revive target"), Target)) return false;
	Target->GetLife()->State = EBrLifeState::Downed;
	Target->GetLife()->DownedEndTime = Pawn->GetLife()->ServerTime() + 30;
	ABrGameState* GS = World->GetGameState<ABrGameState>();
	if (!TestNotNull(TEXT("Game state"), GS)) return false;
	GS->bLobby = GS->bResults = false;
	GS->MatchSettings.ReviveRule = EBrReviveRule::Anyone;
	Pawn->GetLife()->StartInteract();
	TestTrue(TEXT("Interact prioritizes revive"), Pawn->GetLife()->ReviveTarget == Target);
	TestTrue(TEXT("Revive leaves pickup available"), ArmorItem->CanCollect(Pawn));
	Pawn->GetLife()->StopInteract();
	Target->Destroy();
	Pawn->GetLife()->StartInteract();
	TestEqual(TEXT("Next press grants one plate"), Pawn->GetArmor()->GetPlates().Num(), 1);
	Pawn->GetHealth()->SetHealth(50);
	TestTrue(TEXT("Health becomes targetable without re-entering"), ABrPickup::FindInteractPickup(Pawn) == HealthItem);
	Pawn->GetLife()->StartInteract();
	TestTrue(TEXT("Interact heals"), Pawn->GetHealth()->Health > 50);
	TestNull(TEXT("Consumed items no longer targetable"), ABrPickup::FindInteractPickup(Pawn));
	ABrPickup* Outside = Spawn(Armor, Settings->InteractRadius + 1);
	if (!TestNotNull(TEXT("Outside-radius pickup"), Outside)) return false;
	TestNull(TEXT("Trigger edge does not extend center-based interaction radius"), ABrPickup::FindInteractPickup(Pawn));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrWeaponPlaceholderLengthTest, "BrGame.Pickups.WeaponPlaceholderLengths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrWeaponPlaceholderLengthTest::RunTest(const FString& Parameters)
{
	const TCHAR* Names[] = { TEXT("Pistol"), TEXT("AssaultRifle"), TEXT("MachineGun") };
	const double ExpectedLengths[] = { 25.0, 50.0, 75.0 };
	double PreviousLength = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Game/BrGame/Weapons/DA_Weapon_%s.DA_Weapon_%s"),
			Names[Index], Names[Index]);
		const UBrWeaponDefinition* Weapon = LoadObject<UBrWeaponDefinition>(nullptr, *Path);
		if (!TestNotNull(*Path, Weapon)) return false;
		if (!TestNotNull(TEXT("Placeholder weapon has a mesh"), Weapon->Mesh.Get())) return false;
		const FVector Size = Weapon->Mesh->GetBoundingBox().GetSize() * Weapon->VisualScale;
		TestTrue(TEXT("Placeholder has expected approximate length"), FMath::IsNearlyEqual(Size.X, ExpectedLengths[Index], 1.0));
		TestTrue(TEXT("Pistol < assault rifle < machine gun"), Size.X > PreviousLength);
		TestTrue(TEXT("Local X remains the long axis"), Size.X > Size.Y && Size.X > Size.Z);
		PreviousLength = Size.X;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupVisualResolutionTest, "BrGame.Pickups.WeaponVisualResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupVisualResolutionTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!TestNotNull(TEXT("Cube fixture"), Cube) || !TestNotNull(TEXT("Sphere fixture"), Sphere) ||
		!TestNotNull(TEXT("Pickup material fixture"), Material)) return false;
	// Exercise the real rep-notify without changing the production actor's private API.
	FStructProperty* StateProperty = FindFProperty<FStructProperty>(ABrPickup::StaticClass(), TEXT("State"));
	UFunction* RepNotify = ABrPickup::StaticClass()->FindFunctionByName(TEXT("OnRep_State"));
	if (!TestNotNull(TEXT("Replicated pickup state"), StateProperty) ||
		!TestNotNull(TEXT("Pickup rep-notify"), RepNotify)) return false;

	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ABrPickup* Pickup = World->SpawnActor<ABrPickup>();
	if (!TestNotNull(TEXT("Pickup actor"), Pickup))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	UStaticMeshComponent* Visual = Pickup->FindComponentByClass<UStaticMeshComponent>();
	UTextRenderComponent* Label = Pickup->FindComponentByClass<UTextRenderComponent>();
	if (TestNotNull(TEXT("Visual component"), Visual) && TestNotNull(TEXT("Label component"), Label))
	{
		UBrWeaponDefinition* Pistol = NewObject<UBrWeaponDefinition>(World);
		Pistol->Mesh = Cube;
		Pistol->VisualScale = FVector(0.25f, 0.08f, 0.08f);
		Pistol->DisplayName = FText::FromString(TEXT("Pistol"));
		UBrWeaponDefinition* DroppedWeapon = NewObject<UBrWeaponDefinition>(World);
		DroppedWeapon->Mesh = Sphere; // Different mesh as well as scale detects template-mesh regressions.
		DroppedWeapon->VisualScale = FVector(0.75f, 0.08f, 0.08f);
		DroppedWeapon->DisplayName = FText::FromString(TEXT("Machine Gun"));
		UBrWeaponPickupDefinition* Template = NewObject<UBrWeaponPickupDefinition>(World);
		Template->Weapon = Pistol;
		Template->Mesh = Sphere;
		Template->VisualScale = FVector(0.2f);
		Template->Material = Material;
		Template->Color = FLinearColor::Green;
		FBrPickupState* State = StateProperty->ContainerPtrToValuePtr<FBrPickupState>(Pickup);
		State->Definition = Template;
		State->bAvailable = true;
		Pickup->ProcessEvent(RepNotify, nullptr);
		TestTrue(TEXT("Fresh pickup uses weapon mesh, not pickup mesh"), Visual->GetStaticMesh() == Cube);
		TestEqual(TEXT("Fresh pickup uses weapon scale"), Visual->GetRelativeScale3D(), Pistol->VisualScale);
		TestEqual(TEXT("Fresh weapon label preserved"), Pickup->GetPickupLabel().ToString(), FString(TEXT("Pistol")));

		State->Payload.bDropped = true;
		State->Payload.DroppedWeapon = DroppedWeapon;
		State->Payload.LoadedRounds = 7;
		Pickup->ProcessEvent(RepNotify, nullptr);
		TestTrue(TEXT("Dropped pickup uses payload mesh, not template pistol"), Visual->GetStaticMesh() == Sphere);
		TestEqual(TEXT("Dropped pickup uses payload scale"), Visual->GetRelativeScale3D(), DroppedWeapon->VisualScale);
		TestEqual(TEXT("Dropped label uses payload weapon"), Pickup->GetPickupLabel().ToString(), FString(TEXT("Machine Gun (7 loaded)")));
		TestEqual(TEXT("Weapon pickup retains label color"), Label->TextRenderColor, Template->Color.ToFColor(true));
		if (TestNotNull(TEXT("Weapon pickup retains material"), Visual->GetMaterial(0)))
			TestTrue(TEXT("Weapon pickup material derives from pickup definition"), Visual->GetMaterial(0)->GetBaseMaterial() == Material->GetBaseMaterial());

		UBrAmmoPickupDefinition* Ammo = NewObject<UBrAmmoPickupDefinition>(World);
		Ammo->Mesh = Cube;
		Ammo->VisualScale = FVector(0.3f, 0.4f, 0.5f);
		Ammo->Material = Material;
		Ammo->Color = FLinearColor::Blue;
		Ammo->Label = FText::FromString(TEXT("Ammo"));
		Pickup->ResetPickup(Ammo);
		TestTrue(TEXT("Nonweapon mesh unchanged"), Visual->GetStaticMesh() == Cube);
		TestEqual(TEXT("Nonweapon scale unchanged"), Visual->GetRelativeScale3D(), Ammo->VisualScale);
		TestEqual(TEXT("Nonweapon label unchanged"), Pickup->GetPickupLabel().ToString(), FString(TEXT("Ammo")));
		TestEqual(TEXT("Nonweapon label color unchanged"), Label->TextRenderColor, Ammo->Color.ToFColor(true));
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupWeightedTest, "BrGame.Pickups.WeightedSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupWeightedTest::RunTest(const FString& Parameters)
{
	FRandomStream A(49271), B(49271);
	const TArray<float> Weights = {0, 2, 6, 0};
	int32 Counts[4] = {};
	for (int32 Index = 0; Index < 10000; ++Index)
	{
		const int32 Chosen = FBrPickupRules::WeightedIndex(Weights, A);
		if (Chosen < 0 || Chosen >= 4) { AddError(TEXT("Invalid weighted index")); return false; }
		++Counts[Chosen];
		if (Chosen != FBrPickupRules::WeightedIndex(Weights, B)) { AddError(TEXT("Seeded stream not deterministic")); return false; }
	}
	TestEqual(TEXT("Zero weight at beginning never chosen"), Counts[0], 0);
	TestEqual(TEXT("Zero weight at end never chosen"), Counts[3], 0);
	TestTrue(TEXT("Relative weights produce sane 1:3 distribution"), Counts[1] > 2300 && Counts[1] < 2700);
	TestEqual(TEXT("No positive weights fails explicitly"), FBrPickupRules::WeightedIndex({0, -2, 0}, A), INDEX_NONE);
	TestEqual(TEXT("Single positive entry always selected"), FBrPickupRules::WeightedIndex({0, 0, 7}, A), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupApplicationTest, "BrGame.Pickups.ApplicationRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupApplicationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Health fraction respects penalized maximum"), FBrPickupRules::HealthGrant(10, 60, 0.5f), 30.0f);
	TestEqual(TEXT("Health clamps near maximum"), FBrPickupRules::HealthGrant(55, 60, 0.5f), 5.0f);
	TestEqual(TEXT("Full health refuses"), FBrPickupRules::HealthGrant(60, 60, 1), 0.0f);
	TestEqual(TEXT("Grenade below cap grants one"), FBrPickupRules::GrenadeGrant(2, 3), 1);
	TestEqual(TEXT("Grenade at cap refuses"), FBrPickupRules::GrenadeGrant(3, 3), 0);
	TestEqual(TEXT("Ammo uses current magazine size"), FBrPickupRules::AmmoGrant(17, 32, 0), 17);
	TestEqual(TEXT("Ammo uses a different current weapon's magazine size"), FBrPickupRules::AmmoGrant(43, 32, 0), 43);
	TestEqual(TEXT("Reserve cap allows partial magazine"), FBrPickupRules::AmmoGrant(17, 32, 40), 8);
	TestEqual(TEXT("At cap refuses"), FBrPickupRules::AmmoGrant(17, 40, 40), 0);
	TestEqual(TEXT("Above cap also refuses"), FBrPickupRules::AmmoGrant(17, 50, 40), 0);
	TestEqual(TEXT("Unlimited arithmetic doesn't overflow"), FBrPickupRules::AmmoGrant(17, MAX_int32 - 2, 0), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupWeaponTest, "BrGame.Pickups.WeaponPayloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupWeaponTest::RunTest(const FString& Parameters)
{
	const FBrWeaponPickupPlan Fresh = FBrPickupRules::WeaponPlan(true, false, 5, 17, 3, false, 0);
	TestTrue(TEXT("Different weapon drops previous instance"), Fresh.bDropOldWeapon);
	TestEqual(TEXT("Drop contains only loaded rounds"), Fresh.DropLoaded, 5);
	TestEqual(TEXT("Fresh weapon loads one magazine"), Fresh.LoadedAfter, 17);
	TestEqual(TEXT("Loaded plus spare equals complete starting grant"), Fresh.LoadedAfter + Fresh.ReserveGrant, 51);
	const FBrWeaponPickupPlan Dropped = FBrPickupRules::WeaponPlan(true, false, 11, 17, 3, true, 5);
	TestEqual(TEXT("Dropped weapon restores partial magazine"), Dropped.LoadedAfter, 5);
	TestEqual(TEXT("Dropped weapon grants no new spare"), Dropped.ReserveGrant, 0);
	TestEqual(TEXT("Swapping dropped instances conserves represented loaded rounds"),
		Dropped.LoadedAfter + Dropped.DropLoaded + Dropped.ReserveGrant, 16);
	const FBrWeaponPickupPlan SameFresh = FBrPickupRules::WeaponPlan(true, true, 4, 17, 3, false, 0);
	TestFalse(TEXT("Same type creates no drop"), SameFresh.bDropOldWeapon);
	TestEqual(TEXT("Same type never refills current loaded rounds"), SameFresh.LoadedAfter, 4);
	TestEqual(TEXT("Fresh same type contributes all represented ammo once"), SameFresh.ReserveGrant, 51);
	const FBrWeaponPickupPlan SameDrop = FBrPickupRules::WeaponPlan(true, true, 4, 17, 3, true, 5);
	TestEqual(TEXT("Same dropped type contributes loaded payload only"), SameDrop.ReserveGrant, 5);
	TestEqual(TEXT("Empty dropped same type is not an ammo source"),
		FBrPickupRules::WeaponPlan(true, true, 4, 17, 3, true, 0).ReserveGrant, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPickupCountTest, "BrGame.Pickups.HostBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPickupCountTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Host cannot select immediate drop"), FBrMatchSettings::ClampCrateDropInterval(-9), 1.0f);
	TestEqual(TEXT("Host upper duration bound"), FBrMatchSettings::ClampCrateDropInterval(10000), 3600.0f);
	TestEqual(TEXT("Valid host value preserved"), FBrMatchSettings::ClampCrateDropInterval(12.5f), 12.5f);
	return true;
}
#endif
