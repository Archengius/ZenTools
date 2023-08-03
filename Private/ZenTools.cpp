// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "ZenTools.h"
#include "CookedAssetWriter.h"
#include "IoStorePackageMap.h"
#include "RequiredProgramMainCPPInclude.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_APPLICATION(ZenTools, "ZenTools");

DEFINE_LOG_CATEGORY( LogIoStoreTools );

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	FTaskTagScope Scope(ETaskTag::EGameThread);

	// start up the main loop
	GEngineLoop.PreInit(ArgC, ArgV);

	double StartTime = FPlatformTime::Seconds();

	int32 Result = FIOStoreTools::ExecuteIOStoreTools( FCommandLine::Get() ) ? 0 : 1;

	UE_LOG( LogIoStoreTools, Display, TEXT("ZenTools executed in %f seconds"), FPlatformTime::Seconds() - StartTime);

	GLog->Flush();

	RequestEngineExit(TEXT("ZenTools Exiting"));

	FEngineLoop::AppPreExit();
	FModuleManager::Get().UnloadModulesAtShutdown();
	FEngineLoop::AppExit();

	return Result;
}

bool FIOStoreTools::ExtractPackagesFromContainers( const FString& ContainerDirPath, const FString& OutputDirPath, const FString& EncryptionKeysFile )
{
	TMap<FGuid, FAES::FAESKey> EncryptionKeys;
	if ( !EncryptionKeysFile.IsEmpty() )
	{
		if ( !IFileManager::Get().FileExists( *EncryptionKeysFile ) )
		{
			UE_LOG( LogIoStoreTools, Display, TEXT("Encryption keys file '%s' does not exist."), *EncryptionKeysFile );
			return false;
		}

		FString EncryptionKeysJsonString;
		if ( !FFileHelper::LoadFileToString( EncryptionKeysJsonString, *EncryptionKeysFile ) )
		{
			UE_LOG( LogIoStoreTools, Display, TEXT("Failed to read encryption keys file '%s'."), *EncryptionKeysFile );
			return false;
		}

		TSharedPtr<FJsonObject> EncryptionKeysObject;
		if ( !FJsonSerializer::Deserialize( TJsonReaderFactory<>::Create( EncryptionKeysJsonString ), EncryptionKeysObject ) )
		{
			UE_LOG( LogIoStoreTools, Display, TEXT("Failed to deserialize encryption keys file contents as Json: '%s'"), *EncryptionKeysJsonString );
			return false;
		}

		for ( const TPair<FString, TSharedPtr<FJsonValue>>& EntryPair : EncryptionKeysObject->Values )
		{
			FGuid KeyGuid;
			if ( FGuid::Parse( EntryPair.Key, KeyGuid ) )
			{
				const FString EncryptionKeyHex = EntryPair.Value->AsString();

				TArray<uint8> HexToBytesBuffer;
				HexToBytesBuffer.AddZeroed( EncryptionKeyHex.Len() + 1 );
				HexToBytesBuffer.SetNumZeroed( HexToBytes( EncryptionKeyHex, HexToBytesBuffer.GetData() ) );

				if ( HexToBytesBuffer.Num() != FAES::FAESKey::KeySize )
				{
					UE_LOG( LogIoStoreTools, Warning, TEXT("Ignoring Encryption Key '%s' because it has invalid size (%d bytes vs %d expected)"), *KeyGuid.ToString(), HexToBytesBuffer.Num(), FAES::FAESKey::KeySize );
					continue;
				}
				FMemory::Memcpy( EncryptionKeys.FindOrAdd( KeyGuid ).Key, HexToBytesBuffer.GetData(), HexToBytesBuffer.Num() );
			}
			else
			{
				UE_LOG( LogIoStoreTools, Warning, TEXT("Failed to parse string '%s' as a valid Guid for encryption key"), *EntryPair.Key );
			}
		}
	}
	
	TArray<FString> ContainerTableOfContentsFiles;
	IFileManager::Get().FindFiles( ContainerTableOfContentsFiles, *ContainerDirPath, TEXT(".utoc") );

	if ( ContainerTableOfContentsFiles.IsEmpty() )
	{
		UE_LOG( LogIoStoreTools, Display, TEXT("Didn't find any container files in folder '%s'"), *ContainerDirPath );
		return false;
	}

	TArray<TSharedPtr<FIoStoreReader>> ContainerReaders;
	for ( const FString& ContainerFilename : ContainerTableOfContentsFiles )
	{
		const TSharedPtr<FIoStoreReader> IoStoreReader = MakeShared<FIoStoreReader>();
		const FString FullFilePath = FPaths::Combine( ContainerDirPath, ContainerFilename );
		
		const FIoStatus OpenStatus = IoStoreReader->Initialize(  *FPaths::ChangeExtension( FullFilePath, TEXT("") ), EncryptionKeys );
		if ( !OpenStatus.IsOk() )
		{
			UE_LOG( LogIoStoreTools, Error, TEXT("Failed to open Container file '%s': %s"), *FullFilePath, *OpenStatus.ToString() );
			return false;
		}
		ContainerReaders.Add( IoStoreReader );
	}

	UE_LOG( LogIoStoreTools, Display, TEXT("Successfully opened %d Container files"), ContainerReaders.Num() );

	UE_LOG( LogIoStoreTools, Display, TEXT("Building Package Map from Containers") );
	const TSharedPtr<FIoStorePackageMap> PackageMap = MakeShared<FIoStorePackageMap>();

	for ( const TSharedPtr<FIoStoreReader>& Reader : ContainerReaders )
	{
		PackageMap->PopulateFromContainer( Reader );
	}
	UE_LOG( LogIoStoreTools, Display, TEXT("Populated Package Map with %d Packages"), PackageMap->GetTotalPackageCount() );

	UE_LOG( LogIoStoreTools, Display, TEXT("Begin writing Cooked Packages to '%s'"), *OutputDirPath );
	const TSharedPtr<FCookedAssetWriter> PackageWriter = MakeShared<FCookedAssetWriter>( PackageMap, OutputDirPath );

	for ( const TSharedPtr<FIoStoreReader>& Reader : ContainerReaders )
	{
		PackageWriter->WritePackagesFromContainer( Reader->GetContainerId() );
	}
	
	UE_LOG( LogIoStoreTools, Display, TEXT("Done writing %d packages."), PackageWriter->GetTotalNumPackagesWritten() );
	return true;
}

bool FIOStoreTools::ExecuteIOStoreTools(const TCHAR* Cmd)
{
	if ( FParse::Command( &Cmd, TEXT("ExtractPackages") ) )
	{
		FString ContainerFolderPath;
		if ( !FParse::Token( Cmd, ContainerFolderPath, true ) )
		{
			UE_LOG( LogIoStoreTools, Display, TEXT("Usage: ZenTools ExtractPackages <ContainerFolderPath> <ExtractionDir> [--EncryptionKeys=<KeyFile>]") );
			return false;
		}

		FString ExtractFolderRootPath;
		if ( !FParse::Token( Cmd, ExtractFolderRootPath, true ) )
		{
			UE_LOG( LogIoStoreTools, Display, TEXT("Usage: ZenTools ExtractPackages <ContainerFolderPath> <ExtractionDir> [-EncryptionKeys=<KeyFile>]") );
			return false;
		}

		FString EncryptionKeysFile;
		if ( FParse::Value( Cmd, TEXT("-EncryptionKeys="), EncryptionKeysFile ) )
		{
			EncryptionKeysFile = FPaths::ConvertRelativePathToFull( EncryptionKeysFile );
		}
		
		ContainerFolderPath = FPaths::ConvertRelativePathToFull( ContainerFolderPath );
		ExtractFolderRootPath = FPaths::ConvertRelativePathToFull( ExtractFolderRootPath );
		
		UE_LOG( LogIoStoreTools, Display, TEXT("Extracting packages from IoStore containers at '%s' to directory '%s'"), *ContainerFolderPath, *ExtractFolderRootPath );

		return ExtractPackagesFromContainers( ContainerFolderPath, ExtractFolderRootPath, EncryptionKeysFile );
	}

	UE_LOG( LogIoStoreTools, Display, TEXT("Unknown command. Available commands: ") );
	UE_LOG( LogIoStoreTools, Display, TEXT("ZenTools ExtractPackages <ContainerFolderPath> <ExtractionDir> [-EncryptionKeys=<KeyFile>] -- Extract packages from the IoStore containers in the provided folder") );
	return false;
}
