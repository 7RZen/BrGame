// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BrGame : ModuleRules
{
	public BrGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"DeveloperSettings",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Niagara",
			"UMG",
			"Slate"
		});

		// Native settings/menu input replies and Enhanced Input's mapping-failure tags.
		PrivateDependencyModuleNames.AddRange(new string[] { "SteamSockets", "Sockets", "SlateCore", "GameplayTags" });
		AddEngineThirdPartyPrivateStaticDependencies(Target, "Steamworks");

		PublicIncludePaths.AddRange(new string[] {
			"BrGame",
			"BrGame/Core",
			"BrGame/Combat",
			"BrGame/Life",
			"BrGame/Pickups",
			"BrGame/Tests",
			"BrGame/Variant_Strategy",
			"BrGame/Variant_Strategy/UI",
			"BrGame/Variant_TwinStick",
			"BrGame/Variant_TwinStick/AI",
			"BrGame/Variant_TwinStick/Gameplay",
			"BrGame/Variant_TwinStick/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
