// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EZenPackageVersion : uint32;

DECLARE_LOG_CATEGORY_EXTERN( LogIoStoreTools, All, All );

class ZENTOOLS_API FIOStoreTools
{
public:
	static bool ExecuteIOStoreTools( const TCHAR* Cmd );
	static bool ExtractPackagesFromContainers( const FString& ContainerDirPath, const FString& OutputDirPath, const FString& EncryptionKeysFile, EZenPackageVersion DefaultZenPackageVersion, const FString& PackageFiler );
};