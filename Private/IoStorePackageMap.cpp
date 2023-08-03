// Copyright Nikita Zolotukhin. All Rights Reserved.

#include "IoStorePackageMap.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/MemoryReader.h"
#include "IO/IoContainerHeader.h"

void FIoStorePackageMap::PopulateFromContainer(const TSharedPtr<FIoStoreReader>& Reader)
{
	// If this is a global container, read the Script Objects from it
	TIoStatusOr<FIoBuffer> ScriptObjectsBuffer = Reader->Read(CreateIoChunkId(0, 0, EIoChunkType::ScriptObjects), FIoReadOptions());
	if (ScriptObjectsBuffer.IsOk())
	{
		ReadScriptObjects( ScriptObjectsBuffer.ValueOrDie() );
	}

	TArray<FPackageId> PackageIdsInThisContainer;
	TArray<FPackageId> OptionalPackageIdsInThisContainer;
	
	// Read the Package Headers from the Container Header of the container.
	TIoStatusOr<FIoBuffer> ContainerHeaderBuffer = Reader->Read(CreateIoChunkId(Reader->GetContainerId().Value(), 0, EIoChunkType::ContainerHeader), FIoReadOptions());
	if (ContainerHeaderBuffer.IsOk())
	{
		FMemoryReaderView Ar(MakeArrayView(ContainerHeaderBuffer.ValueOrDie().Data(), ContainerHeaderBuffer.ValueOrDie().DataSize()));
		FIoContainerHeader ContainerHeader;
		Ar << ContainerHeader;

		TArrayView<FFilePackageStoreEntry> StoreEntries(reinterpret_cast<FFilePackageStoreEntry*>(ContainerHeader.StoreEntries.GetData()), ContainerHeader.PackageIds.Num());
		TArrayView<FFilePackageStoreEntry> OptionalStoreEntries(reinterpret_cast<FFilePackageStoreEntry*>(ContainerHeader.OptionalSegmentStoreEntries.GetData()), ContainerHeader.OptionalSegmentPackageIds.Num());

		int32 PackageIndex = 0;
		for (FFilePackageStoreEntry& ContainerEntry : StoreEntries)
		{
			const FPackageId& PackageId = ContainerHeader.PackageIds[PackageIndex++];
			FPackageHeaderData& PackageHeader = PackageHeaders.FindOrAdd(PackageId);
			
			PackageHeader.ImportedPackages = TArrayView<FPackageId>(ContainerEntry.ImportedPackages.Data(), ContainerEntry.ImportedPackages.Num());
			PackageHeader.ShaderMapHashes = TArrayView<FSHAHash>(ContainerEntry.ShaderMapHashes.Data(), ContainerEntry.ShaderMapHashes.Num());
			PackageHeader.ExportCount = ContainerEntry.ExportCount;
			PackageHeader.ExportBundleCount = ContainerEntry.ExportBundleCount;
			
			PackageIdsInThisContainer.Add(PackageId);
		}

		int32 OptionalPackageIndex = 0;
		for (FFilePackageStoreEntry& ContainerEntry : OptionalStoreEntries)
		{
			const FPackageId& PackageId = ContainerHeader.OptionalSegmentPackageIds[OptionalPackageIndex++];
			FPackageHeaderData& PackageHeader = PackageHeaders.FindOrAdd(PackageId);
			
			PackageHeader.ImportedPackages = TArrayView<FPackageId>(ContainerEntry.ImportedPackages.Data(), ContainerEntry.ImportedPackages.Num());
			PackageHeader.ShaderMapHashes = TArrayView<FSHAHash>(ContainerEntry.ShaderMapHashes.Data(), ContainerEntry.ShaderMapHashes.Num());
			PackageHeader.ExportCount = ContainerEntry.ExportCount;
			PackageHeader.ExportBundleCount = ContainerEntry.ExportBundleCount;
			
			OptionalPackageIdsInThisContainer.Add(PackageId);
		}
	}

	// Iterate package chunks from the header
	for ( const FPackageId& PackageId : PackageIdsInThisContainer )
	{
		// Optional chunk has index 1, required one has index 0
		const FIoChunkId ChunkId = CreateIoChunkId( PackageId.Value(), 0, EIoChunkType::ExportBundleData );
		
		TIoStatusOr<FIoStoreTocChunkInfo> ChunkInfo = Reader->GetChunkInfo( ChunkId );
		TIoStatusOr<FIoBuffer> PackageBuffer = Reader->Read( ChunkId, FIoReadOptions() );
		
		if ( PackageBuffer.IsOk() )
		{
			ReadExportBundleData( PackageId, ChunkInfo.ValueOrDie(), PackageBuffer.ValueOrDie() );
		}
	}

	// Iterate optional packages from the header
	for ( const FPackageId& PackageId : OptionalPackageIdsInThisContainer )
	{
		// Optional chunk has index 1, required one has index 0
		const FIoChunkId ChunkId = CreateIoChunkId( PackageId.Value(), 1, EIoChunkType::ExportBundleData );
		
		TIoStatusOr<FIoStoreTocChunkInfo> ChunkInfo = Reader->GetChunkInfo( ChunkId );
		TIoStatusOr<FIoBuffer> PackageBuffer = Reader->Read( ChunkId, FIoReadOptions() );
		
		if ( PackageBuffer.IsOk() )
		{
			ReadExportBundleData( PackageId, ChunkInfo.ValueOrDie(), PackageBuffer.ValueOrDie() );
		}
	}

	FPackageContainerMetadata& Metadata = ContainerMetadata.FindOrAdd( Reader->GetContainerId() );

	Metadata.PackagesInContainer = PackageIdsInThisContainer;
	Metadata.OptionalPackagesInContainer = OptionalPackageIdsInThisContainer;
}

bool FIoStorePackageMap::FindPackageContainerMetadata(FIoContainerId ContainerId, FPackageContainerMetadata& OutMetadata) const
{
	if ( const FPackageContainerMetadata* Metadata = ContainerMetadata.Find( ContainerId ) )
	{
		OutMetadata = *Metadata;
		return true;
	}
	return false;
}

bool FIoStorePackageMap::FindPackageHeader(const FPackageId& PackageId, FPackageHeaderData& OutPackageHeader) const
{
	if ( const FPackageHeaderData* HeaderData = PackageHeaders.Find( PackageId ) )
	{
		OutPackageHeader = *HeaderData;
		return true;
	}
	return false;
}

bool FIoStorePackageMap::FindScriptObject(const FPackageObjectIndex& Index, FPackageMapScriptObjectEntry& OutMapEntry) const
{
	check( Index.IsScriptImport() );
	if ( const FPackageMapScriptObjectEntry* Entry = ScriptObjectMap.Find( Index ) )
	{
		OutMapEntry = *Entry;
		return true;
	}
	return false;
}

bool FIoStorePackageMap::FindExportBundleData(const FPackageId& PackageId, FPackageMapExportBundleEntry& OutExportBundleEntry) const
{
	if ( const FPackageMapExportBundleEntry* Bundle = PackageMap.Find( PackageId ) )
	{
		OutExportBundleEntry = *Bundle;
		return true;
	}
	return false;
}

void FIoStorePackageMap::ReadScriptObjects(const FIoBuffer& ChunkBuffer)
{
	FLargeMemoryReader ScriptObjectsArchive(ChunkBuffer.Data(), ChunkBuffer.DataSize());
	TArray<FDisplayNameEntryId> GlobalNameMap = LoadNameBatch(ScriptObjectsArchive);

	int32 NumScriptObjects = 0;
	ScriptObjectsArchive << NumScriptObjects;

	const FScriptObjectEntry* ScriptObjectEntries = reinterpret_cast<const FScriptObjectEntry*>(ChunkBuffer.Data() + ScriptObjectsArchive.Tell());

	for (int32 ScriptObjectIndex = 0; ScriptObjectIndex < NumScriptObjects; ScriptObjectIndex++)
	{
		const FScriptObjectEntry& ScriptObjectEntry = ScriptObjectEntries[ScriptObjectIndex];
		FMappedName MappedName = ScriptObjectEntry.Mapped;
		check(MappedName.IsGlobal());
		
		FPackageMapScriptObjectEntry& ScriptObject = ScriptObjectMap.FindOrAdd( ScriptObjectEntry.GlobalIndex );
		ScriptObject.ScriptObjectIndex = ScriptObjectEntry.GlobalIndex;
		ScriptObject.ObjectName = MappedName.ResolveName(GlobalNameMap);
		ScriptObject.OuterIndex = ScriptObjectEntry.OuterIndex;
		ScriptObject.CDOClassIndex = ScriptObjectEntry.CDOClassIndex;
	}
}

FPackageLocalObjectRef FIoStorePackageMap::ResolvePackageLocalRef( const FPackageObjectIndex& PackageObjectIndex, const TArrayView<const FPackageId>& ImportedPackages, const TArrayView<const uint64>& ImportedPublicExportHashes )
{
	FPackageLocalObjectRef Result{};

	if ( PackageObjectIndex.IsExport() )
	{
		Result.bIsExportReference = true;
		Result.ExportIndex = PackageObjectIndex.ToExport();
	}
	else if ( PackageObjectIndex.IsImport() )
	{
		Result.bIsImport = true;

		if ( PackageObjectIndex.IsScriptImport() )
		{
			Result.Import.ScriptImportIndex = PackageObjectIndex;
			Result.Import.bIsScriptImport = true;
		}
		else if ( PackageObjectIndex.IsPackageImport() )
		{
			Result.Import.PackageExportKey = FPublicExportKey::FromPackageImport( PackageObjectIndex, ImportedPackages, ImportedPublicExportHashes );
			Result.Import.bIsPackageImport = true;
		}
		else
		{
			check( PackageObjectIndex.IsNull() );
			Result.Import.bIsNullImport = true;
		}
	}
	else
	{
		check( PackageObjectIndex.IsNull() );
		Result.bIsNull = true;
	}
	return Result;
}

void FIoStorePackageMap::ReadExportBundleData( const FPackageId& PackageId, const FIoStoreTocChunkInfo& ChunkInfo, const FIoBuffer& ChunkBuffer )
{
	const uint8* PackageSummaryData = ChunkBuffer.Data();
	const FZenPackageSummary* PackageSummary = reinterpret_cast<const FZenPackageSummary*>(PackageSummaryData);

	const TArrayView<const uint8> PackageHeaderDataView(PackageSummaryData + sizeof(FZenPackageSummary), PackageSummary->HeaderSize - sizeof(FZenPackageSummary));
	FMemoryReaderView PackageHeaderDataReader(PackageHeaderDataView);

	TOptional<FZenPackageVersioningInfo> VersioningInfo;
	if (PackageSummary->bHasVersioningInfo)
	{
		PackageHeaderDataReader << VersioningInfo.Emplace();
	}
	TArray<FDisplayNameEntryId> PackageNameMap = LoadNameBatch(PackageHeaderDataReader);

	const FName PackageName = PackageSummary->Name.ResolveName(PackageNameMap);

	// Find package header to resolve imported package IDs
	const FPackageHeaderData& PackageHeader = PackageHeaders.FindChecked( PackageId );
	
	// Construct package data
	FPackageMapExportBundleEntry& PackageData = PackageMap.FindOrAdd( PackageId );
	PackageData.PackageFilename = ChunkInfo.FileName;
	PackageData.PackageName = PackageName;
	PackageData.PackageFlags = PackageSummary->PackageFlags;
	PackageData.VersioningInfo = VersioningInfo;

	// get rid of standard filename prefix
	PackageData.PackageFilename.RemoveFromStart( TEXT("../../../") );

	// Save name map
	PackageData.NameMap.AddZeroed( PackageNameMap.Num() );
	for ( int32 i = 0; i < PackageNameMap.Num(); i++ )
	{
		PackageData.NameMap[i] = PackageNameMap[ i ].ToName( NAME_NO_NUMBER_INTERNAL );
	}

	/** Public export hashes for each import map entry in this package. */
	TArrayView<const uint64> ImportedPublicExportHashes = MakeArrayView<const uint64>(reinterpret_cast<const uint64*>(PackageSummaryData + PackageSummary->ImportedPublicExportHashesOffset), (PackageSummary->ImportMapOffset - PackageSummary->ImportedPublicExportHashesOffset) / sizeof(uint64));

	// Resolve import map now
	const FPackageObjectIndex* ImportMap = reinterpret_cast<const FPackageObjectIndex*>(PackageSummaryData + PackageSummary->ImportMapOffset);
	PackageData.ImportMap.SetNum((PackageSummary->ExportMapOffset - PackageSummary->ImportMapOffset) / sizeof(FPackageObjectIndex));
	
	for (int32 ImportIndex = 0; ImportIndex < PackageData.ImportMap.Num(); ++ImportIndex)
	{
		const FPackageObjectIndex& ImportMapEntry = ImportMap[ImportIndex];
		FPackageMapImportEntry& PackageMapImport = PackageData.ImportMap[ImportIndex];
		
		if ( ImportMapEntry.IsScriptImport() )
		{
			PackageMapImport.bIsScriptImport = true;
			PackageMapImport.ScriptImportIndex = ImportMapEntry;
		}
		else if ( ImportMapEntry.IsPackageImport() )
		{
			PackageMapImport.bIsPackageImport = true;
			PackageMapImport.PackageExportKey = FPublicExportKey::FromPackageImport( ImportMapEntry, PackageHeader.ImportedPackages, ImportedPublicExportHashes );
		}
		else
		{
			check( ImportMapEntry.IsNull() );
			PackageMapImport.bIsNullImport = true;
		}
	}
	
	const FExportMapEntry* ExportMap = reinterpret_cast<const FExportMapEntry*>(PackageSummaryData + PackageSummary->ExportMapOffset);
	PackageData.ExportMap.SetNum( PackageHeader.ExportCount );
	
	for (int32 ExportIndex = 0; ExportIndex < PackageData.ExportMap.Num(); ++ExportIndex)
	{
		const FExportMapEntry& ExportMapEntry = ExportMap[ ExportIndex ];
		FPackageMapExportEntry& ExportData = PackageData.ExportMap[ ExportIndex ];

		ExportData.ObjectName = ExportMapEntry.ObjectName.ResolveName( PackageNameMap );
		ExportData.FilterFlags = ExportMapEntry.FilterFlags;
		ExportData.ObjectFlags = ExportMapEntry.ObjectFlags;
		ExportData.PublicExportHash = ExportMapEntry.PublicExportHash;

		ExportData.OuterIndex = ResolvePackageLocalRef( ExportMapEntry.OuterIndex, PackageHeader.ImportedPackages, ImportedPublicExportHashes );
		ExportData.ClassIndex = ResolvePackageLocalRef( ExportMapEntry.ClassIndex, PackageHeader.ImportedPackages, ImportedPublicExportHashes );
		ExportData.SuperIndex = ResolvePackageLocalRef( ExportMapEntry.SuperIndex, PackageHeader.ImportedPackages, ImportedPublicExportHashes );
		ExportData.TemplateIndex = ResolvePackageLocalRef( ExportMapEntry.TemplateIndex, PackageHeader.ImportedPackages, ImportedPublicExportHashes );

		ExportData.CookedSerialData = MakeShared<TArray<uint8>>();
		ExportData.CookedSerialData->SetNumUninitialized(ExportMapEntry.CookedSerialSize);
	}

	// Read export bundles
	const FExportBundleHeader* ExportBundleHeaders = reinterpret_cast<const FExportBundleHeader*>(PackageSummaryData + PackageSummary->GraphDataOffset);
	const FExportBundleEntry* ExportBundleEntries = reinterpret_cast<const FExportBundleEntry*>(PackageSummaryData + PackageSummary->ExportBundleEntriesOffset);
	uint64 CurrentExportOffset = PackageSummary->HeaderSize;
	
	for ( int32 ExportBundleIndex = 0; ExportBundleIndex < PackageHeader.ExportBundleCount; ExportBundleIndex++ )
	{
		TArray<FExportBundleEntry>& ExportBundles = PackageData.ExportBundles.AddDefaulted_GetRef();
		const FExportBundleHeader* ExportBundle = ExportBundleHeaders + ExportBundleIndex;
		
		const FExportBundleEntry* BundleEntry = ExportBundleEntries + ExportBundle->FirstEntryIndex;
		const FExportBundleEntry* BundleEntryEnd = BundleEntry + ExportBundle->EntryCount;
		check(BundleEntry <= BundleEntryEnd);
		
		while (BundleEntry < BundleEntryEnd)
		{
			ExportBundles.Add( *BundleEntry );
			
			if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				const FPackageMapExportEntry& Export = PackageData.ExportMap[ BundleEntry->LocalExportIndex ];
				const int32 ExportSerialSize = Export.CookedSerialData->Num();

				// Copy the export data into the buffer that we have crated earlier
				FMemory::Memcpy( Export.CookedSerialData->GetData(), PackageSummaryData + CurrentExportOffset, ExportSerialSize );
				CurrentExportOffset += ExportSerialSize;
			}
			BundleEntry++;
		}
	}

	// Read arcs, they are needed to create a list of preload dependencies for this package
	const uint64 ExportBundleHeadersSize = sizeof(FExportBundleHeader) * PackageHeader.ExportBundleCount;
	const uint64 ArcsDataOffset = PackageSummary->GraphDataOffset + ExportBundleHeadersSize;
	const uint64 ArcsDataSize = PackageSummary->HeaderSize - ArcsDataOffset;

	FMemoryReaderView ArcsAr(MakeArrayView<const uint8>(PackageSummaryData + ArcsDataOffset, ArcsDataSize));

	int32 InternalArcsCount = 0;
	ArcsAr << InternalArcsCount;

	for ( int32 Idx = 0; Idx < InternalArcsCount; Idx++ )
	{
		FPackageMapInternalDependencyArc& InternalArc = PackageData.InternalArcs.AddDefaulted_GetRef();
		ArcsAr << InternalArc.FromExportBundleIndex;
		ArcsAr << InternalArc.ToExportBundleIndex;
	}

	for ( int32 ImportPackageIndex = 0; ImportPackageIndex < PackageHeader.ImportedPackages.Num(); ImportPackageIndex++ )
	{
		int32 ExternalArcsCount = 0;
		ArcsAr << ExternalArcsCount;

		for ( int32 Idx = 0; Idx < ExternalArcsCount; Idx++ )
		{
			FPackageMapExternalDependencyArc& ExternalArc = PackageData.ExternalArcs.AddDefaulted_GetRef();
			ArcsAr << ExternalArc.FromImportIndex;
			uint8 FromCommandType = 0;
			ArcsAr << FromCommandType;
			ExternalArc.FromCommandType = static_cast<FExportBundleEntry::EExportCommandType>(FromCommandType);
			ArcsAr << ExternalArc.ToExportBundleIndex;
		}
	}
}
