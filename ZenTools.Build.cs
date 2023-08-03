// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ZenTools : ModuleRules
{
	public ZenTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Runtime/Launch/Public");

		PrivateDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject",
			"Projects", // Included by LaunchEngineLoop.cpp
			"Json"
		});

		PrivateIncludePaths.Add("Runtime/Launch/Private"); // For LaunchEngineLoop.cpp include

		PrivateIncludePathModuleNames.AddRange(new string[] {
			"Json"
		});
	}
}
