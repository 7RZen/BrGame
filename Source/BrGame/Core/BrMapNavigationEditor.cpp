// Narrow editor-only content-pipeline command. Engine Exec RebuildNavigation is
// not reliably routed by UEditorEngine during a Python commandlet; this console
// command invokes the same synchronous nav-only Build after registration.
#if WITH_EDITOR
#include "BrLog.h"
#include "AssetCompilingManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "UObject/UObjectGlobals.h"

static FAutoConsoleCommandWithWorld GBrBuildMapNavigation(
	TEXT("BrGame.Editor.BuildNavigation"),
	TEXT("Build navigation only in the current editor world; waits for completion. Never runs in game worlds."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		UNavigationSystemV1* Nav = World && World->WorldType == EWorldType::Editor ?
			FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!Nav)
		{
			UE_LOG(LogBr, Error, TEXT("Editor navigation build requires an editor world with navigation enabled"));
			return;
		}
		if (IsRunningCommandlet())
		{
			// Editor nav's AsyncLoadLock normally releases after 16 idle ticker
			// frames. A synchronous Python commandlet has no editor idle frames.
			// Finish the actual loading/compilation first, then release ONLY this
			// transient editor lock, never initial/custom/PIE locks.
			FlushAsyncLoading();
			FAssetCompilingManager::Get().FinishAllCompilation();
			Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,
				UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
		}
		Nav->Build();
	}));
#endif
