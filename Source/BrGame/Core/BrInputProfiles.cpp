#include "BrInputProfiles.h"
#include "BrMenuRules.h"
#include "BrLog.h"
#include "InputMappingContext.h"
#include "UserSettings/EnhancedInputUserSettings.h"

bool FBrInputProfiles::Install(UEnhancedInputUserSettings* User, const UInputMappingContext* Context, bool bSaveChanges)
{
	if (!User || !Context || !User->GetActiveKeyProfile())
	{
		UE_LOG(LogBr, Error, TEXT("Input installation failed: missing settings, context or profile."));
		return false;
	}
	if (!User->IsMappingContextRegistered(Context) && !User->RegisterInputMappingContext(Context))
	{
		UE_LOG(LogBr, Error, TEXT("Input installation failed: mapping context registration failed."));
		return false;
	}
	TSet<FName> InstalledNames;
	for (const auto& Mapping : Context->GetMappings())
		if (Mapping.IsPlayerMappable()) InstalledNames.Add(Mapping.GetMappingName());

	TArray<FMapPlayerKeyArgs> Changes;
	// UE also registers new defaults while deserializing saved contexts. Reconcile even if already
	// registered, using IsCustomized rather than only detecting rows added by this particular call.
	for (const auto& Pair : User->GetAllAvailableKeyProfiles())
	{
		const auto* Profile = Pair.Value.Get();
		if (!Profile) continue;
		TMap<FName, FKey> Retained;
		for (const auto& Row : Profile->GetPlayerMappingRows())
		{
			if (Row.Key == TEXT("Br.Walk")) continue;
			for (const auto& Mapping : Row.Value.Mappings)
				if (Mapping.IsCustomized() && Mapping.GetCurrentKey().IsValid())
					Retained.Add(Row.Key, Mapping.GetCurrentKey());
		}
		for (const auto& Row : Profile->GetPlayerMappingRows())
		{
			if (!InstalledNames.Contains(Row.Key)) continue;
			for (const auto& Mapping : Row.Value.Mappings)
			{
				if (!FBrMenuRules::ShouldUnbindDefault(Row.Key, Mapping.GetCurrentKey(), Mapping.IsCustomized(), Retained))
					continue;
				FMapPlayerKeyArgs Args;
				Args.ProfileIdString = Pair.Key;
				Args.MappingName = Row.Key;
				Args.Slot = Mapping.GetSlot();
				Args.HardwareDeviceId = Mapping.GetHardwareDeviceId().HardwareDeviceIdentifier;
				Args.NewKey = EKeys::Invalid; // UnMapPlayerKey resets to F in UE 5.8; explicitly map Invalid instead.
				Args.bCreateMatchingSlotIfNeeded = false;
				Changes.Add(Args);
			}
		}
	}
	bool bSucceeded = true;
	for (const auto& Args : Changes)
	{
		FGameplayTagContainer Errors;
		User->MapPlayerKey(Args, Errors);
		if (!Errors.IsEmpty())
		{
			UE_LOG(LogBr, Error, TEXT("Input default reconciliation failed: %s: %s"), *Args.MappingName.ToString(), *Errors.ToString());
			bSucceeded = false;
		}
		else UE_LOG(LogBr, Display, TEXT("Input default left unbound to preserve custom keys: %s Profile=%s"),
			*Args.MappingName.ToString(), *Args.ProfileIdString);
	}
	if (!Changes.IsEmpty())
	{
		User->ApplySettings();
		if (bSaveChanges) User->SaveSettings();
	}
	return bSucceeded;
}
