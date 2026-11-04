#include "BrDeveloperSettings.h"
#include "BrGameState.h"
#include "BrGameInstance.h"
#include "BrSessionRules.h"
#include "BrZoneBounds.h"
#include "BrCrateRules.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Components/PrimitiveComponent.h"
#include "NavigationSystem.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMapsListTest, "BrGame.Maps.ListAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrMapsListTest::RunTest(const FString&)
{
	UBrDeveloperSettings* S = NewObject<UBrDeveloperSettings>();
	S->Maps = UBrDeveloperSettings::Get()->Maps;
	const int32 ValidCount = S->ValidMaps().Num();
	TestTrue(TEXT("Developer list contains valid maps"), ValidCount >= 2);
	for (const FBrMapEntry& Entry : S->Maps)
	{
		TestTrue(*(Entry.Id.ToString() + TEXT(": valid initial crate count")),
			Entry.InitialCrates == FBrCrateRules::InitialWave(Entry.InitialCrates));
		TestTrue(*(Entry.Id.ToString() + TEXT(": valid interval crate count")),
			Entry.CratesPerDrop == FBrCrateRules::IntervalWave(Entry.CratesPerDrop));
	}
	const FBrMapEntry Duplicate = S->Maps[0];
	S->Maps.Add(Duplicate);
	FBrMapEntry Invalid;
	Invalid.Id = TEXT("Missing");
	Invalid.DisplayName = FText::FromString(TEXT("Missing"));
	Invalid.Map = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/MissingMap.MissingMap")));
	S->Maps.Add(Invalid);
	Invalid.Id = TEXT("Random"); S->Maps.Add(Invalid);
	Invalid.Id = NAME_None; S->Maps.Add(Invalid);
	// One warning for each bad entry on every evaluation below.
	AddExpectedError(TEXT("Skipping invalid/duplicate map entry"), EAutomationExpectedErrorFlags::Contains, 0);
	TestEqual(TEXT("Invalid, missing, reserved and duplicate entries skipped"), S->ValidMaps().Num(), ValidCount);
	TestFalse(TEXT("Unknown ID rejected"), S->IsMapSelectionValid(TEXT("Unknown")));
	TestFalse(TEXT("Arbitrary path rejected"), S->IsMapSelectionValid(TEXT("/Game/TopDown/Lvl_TopDown")));
	TestTrue(TEXT("Random accepted"), S->IsMapSelectionValid(TEXT("Random")));
	FRandomStream Random(100);
	TSet<FName> Drawn;
	for (int32 I = 0; I < 128; ++I)
	{
		FBrMapEntry Chosen;
		TestTrue(TEXT("Random resolves"), S->ResolveMap(TEXT("Random"), Random, Chosen));
		TestNotNull(TEXT("Draw is allowlisted"), S->FindMap(Chosen.Id));
		Drawn.Add(Chosen.Id);
	}
	TestEqual(TEXT("Seeded draws cover the valid list"), Drawn.Num(), ValidCount);
	FBrMapEntry Chosen;
	TestFalse(TEXT("Unknown cannot travel"), S->ResolveMap(TEXT("Unknown"), Random, Chosen));
	S->Maps.Empty();
	TestFalse(TEXT("Empty list cannot select Random"), S->IsMapSelectionValid(TEXT("Random")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMapsStateTest, "BrGame.Maps.StateCarryAndAdvertisement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrMapsStateTest::RunTest(const FString&)
{
	FProperty* Settings = FindFProperty<FProperty>(ABrGameState::StaticClass(), TEXT("MatchSettings"));
	TestTrue(TEXT("Map selection is inside replicated settings with notification"), Settings &&
		Settings->HasAnyPropertyFlags(CPF_Net) && Settings->RepNotifyFunc == TEXT("OnRep_MatchSettings"));
	TestNotNull(TEXT("Selection reflected for RPC/replication"), FindFProperty<FNameProperty>(FBrMatchSettings::StaticStruct(), TEXT("MapId")));
	TestNotNull(TEXT("Outcome reflected for RPC/replication"), FindFProperty<FNameProperty>(FBrMatchSettings::StaticStruct(), TEXT("ResolvedMapId")));
	UBrGameInstance* GI = NewObject<UBrGameInstance>();
	GI->HostSettings.MapId = TEXT("Random");
	GI->HostSettings.ResolvedMapId = TEXT("Town");
	GI->bCarrySettings = true;
	const FBrMatchSettings Carried = GI->HostSettings;
	TestEqual(TEXT("Carry retains Random, not its previous outcome"), Carried.MapId, FName(TEXT("Random")));
	TestEqual(TEXT("Carry retains round announcement"), Carried.ResolvedMapId, FName(TEXT("Town")));
	FOnlineSessionSettings Advertised;
	FBrSessionRules::AdvertiseMap(Advertised, Carried.MapId, UBrDeveloperSettings::Get()->MapName(Carried.MapId));
	FString Id, Name;
	TestTrue(TEXT("Map ID key advertised"), Advertised.Get(FBrSessionRules::MapIdKey, Id));
	TestTrue(TEXT("Map name key advertised"), Advertised.Get(FBrSessionRules::MapNameKey, Name));
	TestEqual(TEXT("Browser keeps Random selection"), Name, FString(TEXT("Random")));
	TestEqual(TEXT("Stable ID"), Id, FString(TEXT("Random")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrMapsCookTest, "BrGame.Maps.CookSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrMapsCookTest::RunTest(const FString&)
{
	TArray<FString> Cook;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("MapsToCook"), Cook, GGameIni);
	for (const FBrMapEntry& Entry : UBrDeveloperSettings::Get()->Maps)
	{
		const FString Package = Entry.Map.ToSoftObjectPath().GetLongPackageName();
		TestTrue(*FString::Printf(TEXT("%s: add %s to MapsToCook"), *Entry.Id.ToString(), *Package),
			Cook.Contains(FString::Printf(TEXT("(FilePath=\"%s\")"), *Package)));
	}
	return true;
}

class FBrValidateMap : public IAutomationLatentCommand
{
public:
	FBrValidateMap(FAutomationTestBase* InTest, FBrMapEntry InEntry) : Test(InTest), Entry(MoveTemp(InEntry)), Started(FPlatformTime::Seconds()) {}
	virtual bool Update() override
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.WorldType == EWorldType::PIE && Context.World() &&
				Context.World()->GetMapName().Contains(Entry.Map.ToSoftObjectPath().GetAssetName())) World = Context.World();
		const FString Prefix = Entry.Id.ToString() + TEXT(": ");
		if (!World)
		{
			if (FPlatformTime::Seconds() - Started < 120) return false;
			Test->AddError(Prefix + TEXT("failed to load PIE world")); return true;
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!bRequestedNav && Nav)
		{
			Nav->RemoveNavigationBuildLock(ENavigationBuildLock::NoUpdateInPIE, UNavigationSystemV1::ELockRemovalRebuildAction::Rebuild);
			Nav->Build(); bRequestedNav = true;
			return false;
		}
		if (Nav && UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World) && FPlatformTime::Seconds() - Started < 120) return false;
		bool bGround = false, bClearStart = false;
		FBox Bounds(ForceInit);
		// Match the safe-zone source order, but never accept its emergency bounds.
		for (TActorIterator<ABrZoneBounds> It(World); It; ++It) Bounds += It->GetPlayableBounds();
		FBox Geometry(ForceInit);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UPrimitiveComponent*> Components(*It);
			for (UPrimitiveComponent* Component : Components)
				if ((It->ActorHasTag(TEXT("BrFogGround")) || Component->ComponentHasTag(TEXT("BrFogGround"))) &&
					Component->IsCollisionEnabled()) bGround = true;
			if (It->GetActorEnableCollision()) Geometry += It->GetComponentsBoundingBox(true);
		}
		if (!Bounds.IsValid) Bounds = Geometry; // BrZoneBounds geometry fallback.
		Test->TestTrue(*(Prefix + TEXT("missing safe-zone bounds/usable collision geometry")), Bounds.IsValid && Bounds.GetSize().X > 1 && Bounds.GetSize().Y > 1);
		Test->TestTrue(*(Prefix + TEXT("missing collision ground tagged BrFogGround")), bGround);
		const auto ClearFloor = [World](const FVector& Floor)
		{
			FHitResult Hit;
			return World->LineTraceSingleByChannel(Hit, Floor + FVector(0,0,50), Floor - FVector(0,0,300), ECC_WorldStatic) &&
				Hit.ImpactNormal.Z >= 0.7 &&
				!World->OverlapBlockingTestByProfile(Hit.ImpactPoint + FVector(0,0,100), FQuat::Identity, TEXT("Pawn"), FCollisionShape::MakeCapsule(42,96));
		};
		for (TActorIterator<APlayerStart> It(World); It; ++It) bClearStart |= ClearFloor(It->GetActorLocation() - FVector(0,0,96));
		FNavLocation Point;
		const bool bNav = Nav && Nav->GetRandomPoint(Point);
		bool bFallback = false;
		if (Bounds.IsValid)
			for (int32 I = 0; I <= 64; ++I)
			{
				const double R = FMath::Min(Bounds.GetExtent().X, Bounds.GetExtent().Y) * 0.95 * FMath::Sqrt(I / 64.0);
				const FVector Probe = Bounds.GetCenter() + FVector(FMath::Cos(I * 2.399963), FMath::Sin(I * 2.399963), 0) * R;
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, Probe + FVector(0,0,1000), Probe - FVector(0,0,2000), ECC_WorldStatic))
					bFallback |= ClearFloor(Hit.ImpactPoint);
			}
		Test->TestTrue(*(Prefix + TEXT("no capsule-clear PlayerStart, navigation spawn or grounded spiral fallback")), bClearStart || bNav || bFallback);
		bool bLanding = false;
		const float Radius = Bounds.IsValid ? Bounds.GetExtent().Size2D() : 0;
		for (int32 I = 0; Nav && I < 256; ++I)
		{
			FNavLocation Landing;
			if (Nav->GetRandomPoint(Landing) &&
				FBrCrateRules::Contains(Landing.Location, Bounds.GetCenter(), Radius, Bounds) &&
				ClearFloor(Landing.Location) &&
				!World->OverlapBlockingTestByProfile(Landing.Location + FVector(0,0,100), FQuat::Identity,
					TEXT("Pawn"), FCollisionShape::MakeCapsule(85,96))) { bLanding = true; break; }
		}
		Test->TestTrue(*(Prefix + TEXT("no navigable capsule-clear crate landing ground inside initial zone/playable bounds")), bLanding);
		return true;
	}
private:
	FAutomationTestBase* Test;
	FBrMapEntry Entry;
	double Started;
	bool bRequestedNav = false;
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBrMapsValidateTest, "BrGame.Maps.Validate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FBrMapsValidateTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const FBrMapEntry& Entry : UBrDeveloperSettings::Get()->Maps) { Names.Add(Entry.Id.ToString()); Commands.Add(Entry.Id.ToString()); }
}
bool FBrMapsValidateTest::RunTest(const FString& Parameters)
{
	const FBrMapEntry* Entry = UBrDeveloperSettings::Get()->FindMap(FName(*Parameters));
	if (!Entry) { AddError(Parameters + TEXT(": invalid map entry")); return false; }
	if (!AutomationOpenMap(Entry->Map.ToSoftObjectPath().GetLongPackageName())) { AddError(Parameters + TEXT(": could not open map")); return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FBrValidateMap(this, *Entry));
	return true;
}
#endif
