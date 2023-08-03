// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ZenToolsTarget : TargetRules
{
	public ZenToolsTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		LaunchModuleName = "ZenTools";

		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bCompileWithPluginSupport = false;
		bIncludePluginsForTargetPlatforms = false;

		// Editor-only data, however, is needed
		bBuildWithEditorOnlyData = true;

		// Currently this app uses AssetRegistry and CoreUObject, but not Engine
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;

		// ICU is not needed
		bCompileICU = false;

		// ZenToolsTarget is a console application, not a Windows app (sets entry point to main(), instead of WinMain())
		bCompileAgainstApplicationCore = false;
		bIsBuildingConsoleApplication = true;

		GlobalDefinitions.Add("UE_TRACE_ENABLED=1");
	}
}
