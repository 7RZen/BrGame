#include "BrDeveloperSettings.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"

TArray<FBrMapEntry> UBrDeveloperSettings::ValidMaps() const
{
	TArray<FBrMapEntry> Result;
	TSet<FName> Seen;
	for (const FBrMapEntry& Entry : Maps)
	{
		const FSoftObjectPath Path = Entry.Map.ToSoftObjectPath();
		// Read package metadata, not the world itself: safe during travel and in packaged builds.
		FString Filename;
		if (Entry.Id.IsNone() || Entry.Id == TEXT("Random") || Seen.Contains(Entry.Id) ||
			Entry.Id.ToString().Contains(TEXT("/")) || Entry.DisplayName.IsEmpty() ||
			!Path.IsValid() || !Path.GetSubPathString().IsEmpty() ||
			Path.GetAssetName() != FPackageName::GetShortName(Path.GetLongPackageName()) ||
			!FPackageName::DoesPackageExist(Path.GetLongPackageName(), &Filename) ||
			!Filename.EndsWith(FPackageName::GetMapPackageExtension()))
		{
			UE_LOG(LogBr, Warning, TEXT("Skipping invalid/duplicate map entry Id='%s' Map='%s'"), *Entry.Id.ToString(), *Path.ToString());
			continue;
		}
		Seen.Add(Entry.Id);
		Result.Add(Entry);
	}
	return Result;
}

const FBrMapEntry* UBrDeveloperSettings::FindMap(FName Id) const
{
	for (const FBrMapEntry& Valid : ValidMaps())
		if (Valid.Id == Id)
			return Maps.FindByPredicate([&Valid](const FBrMapEntry& Entry)
				{ return Entry.Id == Valid.Id && Entry.Map == Valid.Map && !Entry.DisplayName.IsEmpty(); });
	return nullptr;
}

bool UBrDeveloperSettings::IsMapSelectionValid(FName Id) const
{
	return Id == TEXT("Random") ? !ValidMaps().IsEmpty() : FindMap(Id) != nullptr;
}

FName UBrDeveloperSettings::InitialMapId() const
{
	if (FindMap(DefaultMapId)) return DefaultMapId;
	const TArray<FBrMapEntry> Valid = ValidMaps();
	UE_LOG(LogBr, Warning, TEXT("DefaultMapId '%s' is unavailable; using first valid map (or none)."), *DefaultMapId.ToString());
	return Valid.IsEmpty() ? NAME_None : Valid[0].Id;
}

bool UBrDeveloperSettings::ResolveMap(FName Selection, FRandomStream& Random, FBrMapEntry& Out) const
{
	const TArray<FBrMapEntry> Valid = ValidMaps();
	if (Valid.IsEmpty()) return false;
	if (Selection == TEXT("Random")) { Out = Valid[Random.RandRange(0, Valid.Num() - 1)]; return true; }
	for (const FBrMapEntry& Entry : Valid) if (Entry.Id == Selection) { Out = Entry; return true; }
	return false;
}

FText UBrDeveloperSettings::MapName(FName Id) const
{
	if (Id == TEXT("Random")) return NSLOCTEXT("BrMaps", "Random", "Random");
	if (const FBrMapEntry* Entry = FindMap(Id)) return Entry->DisplayName;
	return NSLOCTEXT("BrMaps", "Unavailable", "Map unavailable");
}
