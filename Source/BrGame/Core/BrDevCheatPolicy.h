#pragma once

#include "Engine/EngineBaseTypes.h"

/** Evaluated with server-owned controller state, never values supplied by an RPC. */
struct FBrDevCheatPolicy
{
	static bool CanExecute(bool bAuthority, bool bLocalController, ENetMode NetMode)
	{
#if !UE_BUILD_SHIPPING
		return bAuthority && bLocalController && (NetMode == NM_Standalone || NetMode == NM_ListenServer);
#else
		return false;
#endif
	}
};
