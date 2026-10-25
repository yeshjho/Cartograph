#include "CartographBuildingDataSubsystem.h"


ACartographBuildingDataSubsystem::ACartographBuildingDataSubsystem()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
    bReplicates = true;
}

void ACartographBuildingDataSubsystem::OnAddBuildable(AFGBuildable* Buildable)
{
	const bool ShouldSkip = ShouldInitialize || IsClient || !GIsRunning;

	CARTO_LOG_VERBOSE("AddBuildable: %s, Skip: %d", *Buildable->GetClass()->GetName(), ShouldSkip);

	if (ShouldSkip)
	{
		return;
	}

	if (BuildableToIgnore.Contains(Buildable->GetClass()))
	{
		return;
	}

	FBuildingData Data{
			.Transform = Buildable->GetTransform(),
			//.CustomizationData = Buildable->GetCustomizationData_Native(),
	};
	Data.AddExtraData(Buildable);
	Data.FillInHashAndCache(Buildable->GetClass());
	PendingAddBuildingData.Add(std::move(Data));

	RedrawMap(false);
}

void ACartographBuildingDataSubsystem::OnAddFromBuildableInstanceData(TSubclassOf<AFGBuildable> BuildableClass, const FRuntimeBuildableInstanceData& BuildableInstanceData, bool FromSaveData)
{
	const bool ShouldSkip = ShouldInitialize || FromSaveData || IsClient || !GIsRunning;

	CARTO_LOG_VERBOSE("AddFromBuildableInstanceData: %s, Skip: %d", *BuildableClass->GetName(), ShouldSkip);

	if (ShouldSkip)
	{
		return;
	}

	if (BuildableToIgnore.Contains(BuildableClass.Get()))
	{
		return;
	}

	FBuildingData Data{
			.Transform = BuildableInstanceData.Transform,
			//.CustomizationData = BuildableInstanceData.CustomizationData,
	};
	Data.AddExtraData(BuildableInstanceData.TypeSpecificData);
	Data.FillInHashAndCache(BuildableClass);
	PendingAddBuildingData.Add(std::move(Data));

	RedrawMap(false);
}

void ACartographBuildingDataSubsystem::OnAddFromReplicatedData(TSubclassOf<AFGBuildable> BuildableClass, const FLightweightBuildableReplicationItem& ReplicationData)
{
	const bool ShouldSkip = ShouldInitialize || IsClient || !GIsRunning;

	CARTO_LOG_VERBOSE("AddFromReplicatedData: %s, Skip: %d", *BuildableClass->GetName(), ShouldSkip);

	if (ShouldSkip)
	{
		return;
	}

	if (BuildableToIgnore.Contains(BuildableClass.Get()))
	{
		return;
	}

	FBuildingData Data{
			.Transform = ReplicationData.Transform,
			//.CustomizationData = ReplicationData.CustomizationData,
	};
	Data.AddExtraData(ReplicationData.TypeSpecificData);
	Data.FillInHashAndCache(BuildableClass);
	PendingAddBuildingData.Add(std::move(Data));

	RedrawMap(false);
}

void ACartographBuildingDataSubsystem::OnRemoveBuildable(AFGBuildable* Buildable)
{
	const bool ShouldSkip = ShouldInitialize || IsClient || !GIsRunning;

	CARTO_LOG_VERBOSE("RemoveBuildable: %s, Skip: %d", *Buildable->GetClass()->GetName(), ShouldSkip);

	if (ShouldSkip)
	{
		return;
	}

	if (BuildableToIgnore.Contains(Buildable->GetClass()))
	{
		return;
	}

	FBuildingData Data{
			.Transform = Buildable->GetTransform(),
			//.CustomizationData = Buildable->GetCustomizationData_Native(),
	};
	Data.AddExtraData(Buildable);
	//Data.FillInHashAndCache(Buildable->GetClass());  // Cache are not used in comparison (==, <=>) so we don't need to fill it
	Data.FillInHash(Buildable->GetClass());
	PendingRemoveBuildingData.Add(std::move(Data));

	RedrawMap(false);
}

void ACartographBuildingDataSubsystem::OnInvalidateRuntimeInstanceDataForIndex(AFGLightweightBuildableSubsystem* ClassInstance, TSubclassOf<AFGBuildable> BuildableClass, int32 Index)
{
	const bool ShouldSkip = ShouldInitialize || IsClient || !GIsRunning;

	CARTO_LOG_VERBOSE("InvalidateRuntimeInstanceDataForIndex: %s, Skip: %d", *BuildableClass->GetName(), ShouldSkip);

	if (ShouldSkip)
	{
		return;
	}

	if (BuildableToIgnore.Contains(BuildableClass.Get()))
	{
		return;
	}

	const FRuntimeBuildableInstanceData* LightweightData = ClassInstance->GetRuntimeDataForBuildableClassAndIndex(BuildableClass, Index);

	FBuildingData Data{
			.Transform = LightweightData->Transform,
			//.CustomizationData = Data->CustomizationData,
	};
	Data.AddExtraData(LightweightData->TypeSpecificData);
	//Data.FillInHashAndCache(BuildableClass);  // Cache are not used in comparison (==, <=>) so we don't need to fill it
	Data.FillInHash(BuildableClass);
	PendingRemoveBuildingData.Add(std::move(Data));

	RedrawMap(false);
}

bool ACartographBuildingDataSubsystem::DoesBuildingExist(const uint32 ClassHash) const
{
	return BuildingCountMap.FindRef(ClassHash) > 0;
}

void ACartographBuildingDataSubsystem::BeginPlay()
{
	Super::BeginPlay();

	Instance = this;
}

void ACartographBuildingDataSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Instance = nullptr;

	Super::EndPlay(EndPlayReason);
}

void ACartographBuildingDataSubsystem::OnBuildingDataAdd(const FBuildingData& AddedBuildingData, int32 Pos)
{
	if (AddedBuildingData.VisualBoxCache.bIsValid)
	{
		CurrentBuildingQuadTree.Insert(BuildingDataIndexRedirector.Num(), AddedBuildingData.VisualBoxCache);
		for (int32& Index : BuildingDataIndexRedirector)
		{
			if (Index >= Pos)
			{
				Index++;
			}
		}
		BuildingDataIndexRedirector.Add(Pos);
	}

	BuildingCountMap.FindOrAdd(AddedBuildingData.BuildableClassHash)++;
}


void ACartographBuildingDataSubsystem::OnBuildingDataRemove(const FBuildingData& RemovedBuildingData, int32 Pos)
{
	if (RemovedBuildingData.VisualBoxCache.bIsValid)
	{
		const int32 Num = BuildingDataIndexRedirector.Num();
		for (int32 i = 0; i < Num; i++)
		{
			int32& BuildingDataArrayIndex = BuildingDataIndexRedirector[i];
			if (BuildingDataArrayIndex == Pos)
			{
				CurrentBuildingQuadTree.Remove(i, RemovedBuildingData.VisualBoxCache);
				BuildingDataArrayIndex = -1;
			}
			else if (BuildingDataArrayIndex > Pos)
			{
				BuildingDataArrayIndex--;
			}
		}
	}

	BuildingCountMap.FindChecked(RemovedBuildingData.BuildableClassHash)--;
}
