#include "BrInputProfiles.h"
#include "BrMenuRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrDeveloperSettings.h"
#include "BrInputConfig.h"
#include "InputMappingContext.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrDefaultConflictRulesTest, "BrGame.Menu.NewDefaultPreservesCustomBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrDefaultConflictRulesTest::RunTest(const FString&)
{
	TMap<FName, FKey> Retained = {{TEXT("Br.Fire"), EKeys::F}};
	TestTrue(TEXT("New Plant F loses to retained Fire F"), FBrMenuRules::ShouldUnbindDefault(TEXT("Br.Plant"), EKeys::F, false, Retained));
	TestFalse(TEXT("Never rewrite a customized action"), FBrMenuRules::ShouldUnbindDefault(TEXT("Br.Fire"), EKeys::F, true, Retained));
	TestFalse(TEXT("Unbound stays unbound"), FBrMenuRules::ShouldUnbindDefault(TEXT("Br.Plant"), EKeys::Invalid, true, Retained));
	TestFalse(TEXT("Unoccupied default stays available"), FBrMenuRules::ShouldUnbindDefault(TEXT("Br.Plant"), EKeys::F, false, {}));
	TestTrue(TEXT("Reserved new default rejected"), FBrMenuRules::ShouldUnbindDefault(TEXT("Br.Plant"), EKeys::Tilde, false, {}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSavedProfileDefaultTest, "BrGame.Input.PreFeatureProfilePlantConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSavedProfileDefaultTest::RunTest(const FString&)
{
	const auto* Config = UBrDeveloperSettings::Get()->InputConfig.LoadSynchronous();
	if (!TestNotNull(TEXT("Current input config"), Config) ||
		!TestNotNull(TEXT("Plant action"), Config->PlantAction.Get())) return false;
	auto* PreFeature = DuplicateObject<UInputMappingContext>(Config->DefaultMappingContext, GetTransientPackage());
	PreFeature->UnmapAllKeysFromAction(Config->PlantAction);
	auto* User = NewObject<UEnhancedInputUserSettings>();
	FPlayerMappableKeyProfileCreationArgs Create;
	Create.ProfileType = UEnhancedPlayerMappableKeyProfile::StaticClass();
	Create.ProfileStringIdentifier = TEXT("InputUserSettings.Profiles.Default");
	Create.bSetAsCurrentProfile = true;
	User->CreateNewKeyProfile(Create);
	if (!TestTrue(TEXT("Register pre-feature context"), User->RegisterInputMappingContext(PreFeature))) return false;
	TestFalse(TEXT("Pre-feature profile has no Plant"), User->GetActiveKeyProfile()->GetPlayerMappingRows().Contains(TEXT("Br.Plant")));
	FMapPlayerKeyArgs Fire;
	Fire.MappingName = TEXT("Br.Fire");
	Fire.Slot = EPlayerMappableKeySlot::First;
	Fire.NewKey = EKeys::F;
	FGameplayTagContainer Errors;
	User->MapPlayerKey(Fire, Errors);
	if (!TestTrue(TEXT("Pre-feature Fire customizes F"), Errors.IsEmpty())) return false;

	// Use real Enhanced Input serialization without touching the user's on-disk saves.
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("Serialize pre-feature save in memory"), UGameplayStatics::SaveGameToMemory(User, Bytes))) return false;
	auto* Loaded = Cast<UEnhancedInputUserSettings>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Load retained profile"), Loaded)) return false;
	if (!TestTrue(TEXT("Shared installer reconciles new defaults"), FBrInputProfiles::Install(Loaded, Config->DefaultMappingContext, false))) return false;
	const auto Key = [](const UEnhancedInputUserSettings* Settings, FName Name)
	{
		const FKeyMappingRow* Row = Settings->GetActiveKeyProfile()->GetPlayerMappingRows().Find(Name);
		if (Row)
			for (const auto& Mapping : Row->Mappings)
				if (Mapping.GetSlot() == EPlayerMappableKeySlot::First) return Mapping.GetCurrentKey();
		return EKeys::Invalid;
	};
	TestEqual(TEXT("Retained custom Fire is still F"), Key(Loaded, TEXT("Br.Fire")), EKeys::F);
	TestEqual(TEXT("New conflicting Plant is explicitly unbound"), Key(Loaded, TEXT("Br.Plant")), EKeys::Invalid);
	const auto& PlantRow = Loaded->GetActiveKeyProfile()->GetPlayerMappingRows().FindChecked(TEXT("Br.Plant"));
	TestFalse(TEXT("Unbound Plant row is retained for settings"), PlantRow.Mappings.IsEmpty());
	for (const auto& Mapping : PlantRow.Mappings)
		TestFalse(TEXT("Plant display label remains available"), Mapping.GetDisplayName().IsEmpty());
	for (const auto& Mapping : Config->DefaultMappingContext->GetMappings())
	{
		if (Mapping.GetMappingName() != TEXT("Br.Plant")) continue;
		TArray<FKey> RuntimeKeys;
		Loaded->GetActiveKeyProfile()->GetPlayerMappedKeysForRebuildControlMappings(Mapping, RuntimeKeys);
		TestEqual(TEXT("Nonempty override prevents fallback to default F"), RuntimeKeys.Num(), 1);
		TestFalse(TEXT("Runtime rebuild cannot bind Plant to F"), RuntimeKeys.Contains(EKeys::F));
	}
	TestTrue(TEXT("Already-registered install is idempotent"), FBrInputProfiles::Install(Loaded, Config->DefaultMappingContext, false));
	TestEqual(TEXT("Second install keeps Plant unbound"), Key(Loaded, TEXT("Br.Plant")), EKeys::Invalid);
	Bytes.Reset();
	if (!TestTrue(TEXT("Serialize reconciled profile"), UGameplayStatics::SaveGameToMemory(Loaded, Bytes))) return false;
	auto* Reloaded = Cast<UEnhancedInputUserSettings>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Reload reconciled profile"), Reloaded)) return false;
	TestTrue(TEXT("Deserialize registration also reconciles safely"), FBrInputProfiles::Install(Reloaded, Config->DefaultMappingContext, false));
	TestEqual(TEXT("Unbound state survives save/restore"), Key(Reloaded, TEXT("Br.Plant")), EKeys::Invalid);
	TestEqual(TEXT("Custom key survives save/restore"), Key(Reloaded, TEXT("Br.Fire")), EKeys::F);
	FMapPlayerKeyArgs Plant;
	Plant.MappingName = TEXT("Br.Plant"); Plant.Slot = EPlayerMappableKeySlot::First; Plant.NewKey = EKeys::H;
	Reloaded->MapPlayerKey(Plant, Errors);
	TestTrue(TEXT("User can bind unbound Plant later"), Errors.IsEmpty());
	FBrInputProfiles::Install(Reloaded, Config->DefaultMappingContext, false);
	TestEqual(TEXT("Later user choice is not overwritten"), Key(Reloaded, TEXT("Br.Plant")), EKeys::H);
	return true;
}
#endif
