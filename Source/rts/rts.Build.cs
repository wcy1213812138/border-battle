// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class rts : ModuleRules
{
	public rts(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		RuntimeDependencies.Add("$(ProjectDir)/Content/RTS/Data/Balance/RTSBalance.csv", StagedFileType.NonUFS);
		RuntimeDependencies.Add("$(EngineDir)/Content/Slate/Fonts/DroidSansFallback.ttf", StagedFileType.NonUFS);
		RuntimeDependencies.Add("$(ProjectDir)/Content/RTS/Data/Balance/RTSAIDifficulty.csv", StagedFileType.NonUFS);

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Niagara",
			"UMG",
			"Slate",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd"); // editor-only movement regression tests
			PrivateDependencyModuleNames.Add("RenderCore"); // windowless projection viewport in selection tests
		}

		PublicIncludePaths.AddRange(new string[] {
			"rts",
			"rts/RTS",
			"rts/Variant_Strategy",
			"rts/Variant_Strategy/UI",
			"rts/Variant_TwinStick",
			"rts/Variant_TwinStick/AI",
			"rts/Variant_TwinStick/Gameplay",
			"rts/Variant_TwinStick/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
