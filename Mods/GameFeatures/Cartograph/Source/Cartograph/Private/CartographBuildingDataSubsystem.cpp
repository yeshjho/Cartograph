#include "CartographBuildingDataSubsystem.h"

#include "Net/UnrealNetwork.h"

#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"

#include "CartographHelpers.h"
#include "CartographMapDrawSubsystem.h"
#include "Buildables/FGBuildable.h"


bool FBuildingDataArray::NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
{
	return FastArrayDeltaSerialize<FBuildingData, FBuildingDataArray>(Items, DeltaParams, *this);
}


void FBuildingDataArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	for (const int32 Index : AddedIndices)
	{
		DirtyTypes.Add(Items[Index].BuildableClassHash);
	}
}


void FBuildingDataArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
	for (const int32 Index : RemovedIndices)
	{
		DirtyTypes.Add(Items[Index].BuildableClassHash);
	}
}


void FBuildingDataArray::PostReplicatedReceive(const FPostReplicatedReceiveParameters& Parameters)
{
	CARTO_LOG_ERROR_RETURN_IF_NULL(ACartographBuildingDataSubsystem::Instance);
	ACartographBuildingDataSubsystem::Instance->OnBuildingDataInvalidated(DirtyTypes);
	DirtyTypes.Reset();
}


ACartographBuildingDataSubsystem::ACartographBuildingDataSubsystem()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
    bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
}


void ACartographBuildingDataSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACartographBuildingDataSubsystem, BuildingDataArray);
}


void ACartographBuildingDataSubsystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}


void ACartographBuildingDataSubsystem::OnAddBuildable(const AFGBuildable* Buildable)
{
	CARTO_LOG_VERBOSE("AddBuildable: %s", *Buildable->GetClass()->GetName());

	if (!Cartograph::IsBuildableIgnored(Buildable->GetClass()))
	{
		AddBuilding(Buildable, Buildable->GetTransform(), Buildable->GetClass());
	}
}


void ACartographBuildingDataSubsystem::OnAddFromBuildableInstanceData(const TSubclassOf<AFGBuildable>& BuildableClass, const FRuntimeBuildableInstanceData& BuildableInstanceData, const bool FromSaveData)
{
	if (FromSaveData)
	{
		return;
	}

	CARTO_LOG_VERBOSE("AddFromBuildableInstanceData: %s", *BuildableClass->GetName());

	if (!Cartograph::IsBuildableIgnored(BuildableClass.Get()))
	{
		AddBuilding(BuildableInstanceData.TypeSpecificData, BuildableInstanceData.Transform, BuildableClass);
	}
}


void ACartographBuildingDataSubsystem::OnAddFromReplicatedData(const TSubclassOf<AFGBuildable>& BuildableClass, const FLightweightBuildableReplicationItem& ReplicationData)
{
	CARTO_LOG_VERBOSE("AddFromReplicatedData: %s", *BuildableClass->GetName());

	if (!Cartograph::IsBuildableIgnored(BuildableClass.Get()))
	{
		AddBuilding(ReplicationData.TypeSpecificData, ReplicationData.Transform, BuildableClass);
	}
}


void ACartographBuildingDataSubsystem::OnRemoveBuildable(const AFGBuildable* Buildable)
{
	CARTO_LOG_VERBOSE("RemoveBuildable: %s", *Buildable->GetClass()->GetName());

	if (!Cartograph::IsBuildableIgnored(Buildable->GetClass()))
	{
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


void ACartographBuildingDataSubsystem::OnInvalidateRuntimeInstanceDataForIndex(const AFGLightweightBuildableSubsystem* ClassInstance, const TSubclassOf<AFGBuildable>& BuildableClass, const int32 Index)
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


void ACartographBuildingDataSubsystem::OnBuildingDataInvalidated(const TSet<uint32>& InvalidatedTypes)
{
	DirtyTypes.Append(InvalidatedTypes);
}


void ACartographBuildingDataSubsystem::OnBuildingDataInvalidated(const uint32 InvalidatedType)
{
	DirtyTypes.Add(InvalidatedType);
}


bool ACartographBuildingDataSubsystem::DoesBuildingExist(const uint32 ClassHash) const
{
	return BuildingCountMap.FindRef(ClassHash) > 0;
}


void ACartographBuildingDataSubsystem::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		GatherInitialBuildings();
	}

	Instance = this;
}

void ACartographBuildingDataSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Instance = nullptr;

	Super::EndPlay(EndPlayReason);
}

void ACartographBuildingDataSubsystem::GatherInitialBuildings()
{
	const TArray<AFGBuildable*>& Buildables = AFGBuildableSubsystem::Get(GetWorld())->GetAllBuildablesRef();
	const TMap<TSubclassOf<AFGBuildable>, TArray<FRuntimeBuildableInstanceData>>& LightweightBuildables = AFGLightweightBuildableSubsystem::Get(GetWorld())->GetAllLightweightBuildableInstances();

	int LightweightBuildablesCount = 0;
	for (const auto& [Type, Arr] : LightweightBuildables)
	{
		LightweightBuildablesCount += Arr.Num();
	}

	CARTO_LOG("InitialBuildableGather Started. Buildables: %d, LightweightBuildables: %d", Buildables.Num(), LightweightBuildablesCount);

	const int Total = Buildables.Num() + LightweightBuildablesCount;
	CurrentBuildingData.Empty(Total);
	BuildingCountMap.Empty();

	for (const AFGBuildable* Buildable : Buildables)
	{
		if (BuildableToIgnore.Contains(Buildable->GetClass()))
		{
			continue;
		}

		FBuildingData NewBuildingData{
			.Transform = Buildable->GetTransform(),
			//.CustomizationData = Factory->GetCustomizationData_Native(),
		};
		NewBuildingData.AddExtraData(Buildable);
		NewBuildingData.FillInHashAndCache(Buildable->GetClass());

		CurrentBuildingData.Add(std::move(NewBuildingData));
	}

	for (const auto& [Type, Arr] : LightweightBuildables)
	{
		if (BuildableToIgnore.Contains(Type.Get()))
		{
			continue;
		}

		for (const FRuntimeBuildableInstanceData& InstanceData : Arr)
		{
			FBuildingData NewBuildingData{
				.Transform = InstanceData.Transform,
				//.CustomizationData = InstanceData.CustomizationData,
			};
			NewBuildingData.AddExtraData(InstanceData.TypeSpecificData);
			NewBuildingData.FillInHashAndCache(Type);

			CurrentBuildingData.Add(std::move(NewBuildingData));
		}
	}

	Algo::Sort(CurrentBuildingData);
	const int Size = CurrentBuildingData.Num();
	for (int i = 0; i < Size; i++)
	{
		const FBuildingData& BuildingData = CurrentBuildingData[i];
		OnBuildingDataAdd(BuildingData, i);
	}

	MinHeight = !CurrentBuildingData.IsEmpty() ? CurrentBuildingData[0].Transform.GetLocation().Z : -100;
	MaxHeight = !CurrentBuildingData.IsEmpty() ? CurrentBuildingData.Last().Transform.GetLocation().Z : 100;
	OnZFilterUpdated(0, 1);

	CARTO_LOG("InitialBuildableGather Finished");

	for (const auto& [ClassHash, Count] : BuildingCountMap)
	{
		CARTO_LOG_DEBUG("Building: %u, Count: %d", ClassHash, Count);
	}
}


template<typename T>
void ACartographBuildingDataSubsystem::AddBuilding(const T& Building, FTransform Transform, const TSubclassOf<AFGBuildable>& BuildableClass) 
{
	FBuildingData Data{
		.Transform = std::move(Transform),
	};
	Data.AddExtraData(Building);
	Data.FillInHashAndCache(BuildableClass);

	BuildingDataArray.MarkItemDirty(BuildingDataArray.Items.Add_GetRef(std::move(Data)));
	OnBuildingDataInvalidated(Data.BuildableClassHash);
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
