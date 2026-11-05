#include "BrPickupRules.h"
#include "BrMatchSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrPickup.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"

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
