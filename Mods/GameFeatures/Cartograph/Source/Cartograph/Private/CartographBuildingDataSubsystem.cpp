#include "CartographBuildingDataSubsystem.h"

#include "Net/UnrealNetwork.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"

#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"

#include "CartographGameInstanceModule.h"
#include "CartographHelpers.h"
#include "CartographMapDrawSubsystem.h"
#include "Buildables/FGBuildable.h"


namespace
{
	template<typename T>
	FBuildingData ConstructBuildingData(const T& Building, FTransform Transform, const TSoftClassPtr<AFGBuildable>& BuildableClass)
	{
		FBuildingData Data{
			.Transform = std::move(Transform),
		};
		Data.AddExtraData(Building);
		Data.FillInHashAndCache(BuildableClass.Get());

		return Data;
	}
}


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
	PrimaryActorTick.bCanEverTick = !FPlatformProperties::IsServerOnly();
}


void ACartographBuildingDataSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACartographBuildingDataSubsystem, BuildingDataArray);
}


void ACartographBuildingDataSubsystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (DirtyTypes.IsEmpty())
	{
		return;
	}

	auto* MapDrawSubsystem = ACartographMapDrawSubsystem::Instance;
	if (!MapDrawSubsystem)
	{
		return;
	}

	// Process only one type per tick to prevent lag spike
	const uint32 DirtyType = *DirtyTypes.CreateConstIterator();
	DirtyTypes.Remove(DirtyType);
	
	UNiagaraComponent* Niagara = MapDrawSubsystem->GetNiagaraComponentPerType(DirtyType);
	if (!Niagara)
	{
		return;
	}

	const uint32* Count = BuildingCountPerType.Find(DirtyType);
	CARTO_LOG_ERROR_RETURN_IF_NULL(Count);

	PositionsParam.Reset(*Count);
	RotationsParam.Reset(*Count);
	ScalesParam.Reset(*Count);

	for (const FBuildingData& BuildingData : BuildingDataArray.Items)
	{
		PositionsParam.Add(BuildingData.Transform.GetLocation());
		RotationsParam.Add(BuildingData.Transform.GetRotation());
		ScalesParam.Add(BuildingData.Transform.GetScale3D());
	}

	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(Niagara, "User.Positions", PositionsParam);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(Niagara, "User.Rotations", RotationsParam);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Niagara, "User.Scales", ScalesParam);

	MapDrawSubsystem->MarkMapTextureDirty();
}


void ACartographBuildingDataSubsystem::OnAddBuildable(const AFGBuildable* Buildable)
{
	CARTO_LOG_VERBOSE("AddBuildable: %s", *Buildable->GetClass()->GetName());

	if (!Cartograph::IsBuildableIgnored(Buildable->GetClass()))
	{
		AddBuildable(Buildable);
	}
}


void ACartographBuildingDataSubsystem::OnAddFromBuildableInstanceData(const TSubclassOf<AFGBuildable>& BuildableClass, const FRuntimeBuildableInstanceData& BuildableInstanceData, const bool FromSaveData, const int32 InstanceIndex)
{
	if (FromSaveData)
	{
		return;
	}

	CARTO_LOG_VERBOSE("AddFromBuildableInstanceData: %s", *BuildableClass->GetName());

	if (!Cartograph::IsBuildableIgnored(BuildableClass.Get()))
	{
		AddLightweightBuildable(BuildableInstanceData.TypeSpecificData, InstanceIndex, BuildableInstanceData.Transform, BuildableClass.Get());
	}
}


void ACartographBuildingDataSubsystem::OnAddFromReplicatedData(const AFGLightweightBuildableSubsystem* ClassInstance, const TSubclassOf<AFGBuildable>& BuildableClass, const FLightweightBuildableReplicationItem& ReplicationData)
{
	CARTO_LOG_VERBOSE("AddFromReplicatedData: %s", *BuildableClass->GetName());

	if (!Cartograph::IsBuildableIgnored(BuildableClass.Get()))
	{
		// TODO: Verify
		const int32 InstanceIndex = ClassInstance->GetAllLightweightBuildableInstances().FindRef(BuildableClass).Num() - 1;
		AddLightweightBuildable(ReplicationData.TypeSpecificData, InstanceIndex, ReplicationData.Transform, BuildableClass.Get());
	}
}


void ACartographBuildingDataSubsystem::OnRemoveBuildable(const AFGBuildable* Buildable)
{
	CARTO_LOG_VERBOSE("RemoveBuildable: %s", *Buildable->GetClass()->GetName());

	if (!Cartograph::IsBuildableIgnored(Buildable->GetClass()))
	{
		return;
	}

	uint32 ArrayIndex = 0;
	if (!BuildableToArrayIndex.RemoveAndCopyValue(Buildable, ArrayIndex))
	{
		CARTO_LOG_ERROR("Buildable not found");
		return;
	}

	RemoveBuildingDuringRuntime(Buildable->GetClass(), ArrayIndex);
}


void ACartographBuildingDataSubsystem::OnInvalidateRuntimeInstanceDataForIndex(const TSubclassOf<AFGBuildable>& BuildableClass, const int32 Index)
{
	CARTO_LOG_VERBOSE("InvalidateRuntimeInstanceDataForIndex: %s", *BuildableClass->GetName());

	if (!Cartograph::IsBuildableIgnored(BuildableClass.Get()))
	{
		return;
	}

	uint32 ArrayIndex = 0;
	if (!LightweightBuildableToArrayIndex[BuildableClass.Get()].RemoveAndCopyValue(Index, ArrayIndex))
	{
		CARTO_LOG_ERROR("Lightweight Buildable not found");
		return;
	}

	RemoveBuildingDuringRuntime(BuildableClass.Get(), ArrayIndex);
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
	return BuildingCountPerType.FindRef(ClassHash, 0) > 0;
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
	// TODO: Measure with a maxed-out save and optimize if too slow, don't want to time-slice it

	const TArray<AFGBuildable*>& Buildables = AFGBuildableSubsystem::Get(GetWorld())->GetAllBuildablesRef();
	const TMap<TSubclassOf<AFGBuildable>, TArray<FRuntimeBuildableInstanceData>>& LightweightBuildables = AFGLightweightBuildableSubsystem::Get(GetWorld())->GetAllLightweightBuildableInstances();

	int LightweightBuildablesCount = 0;
	for (const auto& [Type, Arr] : LightweightBuildables)
	{
		LightweightBuildablesCount += Arr.Num();
	}

	CARTO_LOG("InitialBuildableGather Started. Buildables: %d, LightweightBuildables: %d", Buildables.Num(), LightweightBuildablesCount);

	const int Total = Buildables.Num() + LightweightBuildablesCount;
	BuildingDataArray.Items.Empty(Total);
	BuildableToArrayIndex.Empty();
	LightweightBuildableToArrayIndex.Empty();
	DirtyTypes.Empty();

	for (const AFGBuildable* Buildable : Buildables)
	{
		if (!Cartograph::IsBuildableIgnored(Buildable->GetClass()))
		{
			AddBuildable(Buildable);
		}
	}

	for (const auto& [Type, Arr] : LightweightBuildables)
	{
		TSoftClassPtr<AFGBuildable> SoftType = Type.Get();
		if (Cartograph::IsBuildableIgnored(SoftType))
		{
			continue;
		}

		const int32 Num = Arr.Num();
		for (int i = 0; i < Num; i++)
		{
			const FRuntimeBuildableInstanceData& InstanceData = Arr[i];
			AddLightweightBuildable(InstanceData.TypeSpecificData, i, InstanceData.Transform, SoftType);
		}
	}

	CARTO_LOG("InitialBuildableGather Finished");

	for (const auto [ClassHash, Count] : BuildingCountPerType)
	{
		CARTO_LOG_DEBUG("Building: %u, Count: %u", ClassHash, Count);
	}
}


void ACartographBuildingDataSubsystem::AddBuildable(const AFGBuildable* Buildable)
{
	FBuildingData Data = ConstructBuildingData(Buildable, Buildable->GetTransform(), Buildable->GetClass());
	OnBuildingDataInvalidated(Data.BuildableClassHash);
	BuildingDataArray.MarkItemDirty(BuildingDataArray.Items.Add_GetRef(std::move(Data)));

	BuildableToArrayIndex[Buildable] = BuildingDataArray.Items.Num() - 1;

	BuildingCountPerType[Data.BuildableClassHash]++;
}


void ACartographBuildingDataSubsystem::AddLightweightBuildable(const FFGDynamicStruct& TypeSpecificData, const int32 InstanceIndex, const FTransform& Transform, const TSoftClassPtr<AFGBuildable>& BuildableClass)
{
	FBuildingData Data = ConstructBuildingData(TypeSpecificData, Transform, BuildableClass);
	OnBuildingDataInvalidated(Data.BuildableClassHash);
	BuildingDataArray.MarkItemDirty(BuildingDataArray.Items.Add_GetRef(std::move(Data)));

	LightweightBuildableToArrayIndex[BuildableClass.Get()][InstanceIndex] = BuildingDataArray.Items.Num() - 1;

	BuildingCountPerType[Data.BuildableClassHash]++;
}


void ACartographBuildingDataSubsystem::RemoveBuildingDuringRuntime(const TSoftClassPtr<AFGBuildable>& BuildableClass, const uint32 ArrayIndex)
{
	const uint32 TypeHash = FBuildingData::GetHash(BuildableClass);
	if (TypeHash == 0)
	{
		return;
	}

	OnBuildingDataInvalidated(TypeHash);
	BuildingDataArray.Items.RemoveAt(ArrayIndex);
	BuildingDataArray.MarkArrayDirty();

	for (auto& It : BuildableToArrayIndex)
	{
		if (It.Value > ArrayIndex)
		{
			It.Value--;
		}
	}

	BuildingCountPerType[TypeHash]--;
}
