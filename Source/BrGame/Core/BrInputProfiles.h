#pragma once

#include "CoreMinimal.h"

class UEnhancedInputUserSettings;
class UInputMappingContext;

/** Shared installation path for gameplay and menu-only settings; does not replace Enhanced Input saves. */
struct BRGAME_API FBrInputProfiles
{
	static bool Install(UEnhancedInputUserSettings* User, const UInputMappingContext* Context,
		bool bSaveChanges = true);
};
