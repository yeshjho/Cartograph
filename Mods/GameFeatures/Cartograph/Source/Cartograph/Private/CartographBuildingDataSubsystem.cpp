#include "CartographBuildingDataSubsystem.h"

#include "NiagaraDataInterfaceArrayFunctionLibrary.h"

#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"

#include "CartographBuildingDataHolder.h"
#include "CartographGameInstanceModule.h"
#include "CartographHelpers.h"
#include "CartographMapDrawSubsystem.h"
#include "NiagaraComponent.h"
#include "Buildables/FGBuildable.h"


ACartographBuildingDataSubsystem::ACartographBuildingDataSubsystem()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnLocal;
    bReplicates = false;
	PrimaryActorTick.bCanEverTick = !FPlatformProperties::IsServerOnly();
}


void ACartographBuildingDataSubsystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SendDataToNiagara();
}


void ACartographBuildingDataSubsystem::OnBuildableCountChangedFromReplication(const uint32 ClassHash, const int CountDelta)
{
	BuildingCountPerType.FindOrAdd(ClassHash) += CountDelta;
}


void ACartographBuildingDataSubsystem::OnAddBuildable(const AFGBuildable* Buildable)
{
	if (!HasDataAuthority)
	{
		return;
	}

	CARTO_LOG_VERBOSE("AddBuildable: %s", *Buildable->GetClass()->GetName());

	AddBuildable(Buildable);
}


void ACartographBuildingDataSubsystem::OnAddFromBuildableInstanceData(const TSubclassOf<AFGBuildable>& BuildableClass, const FRuntimeBuildableInstanceData& BuildableInstanceData, const bool FromSaveData, const int32 InstanceIndex)
{
	if (!HasDataAuthority)
	{
		return;
	}

	if (FromSaveData)
	{
		return;
	}

	CARTO_LOG_VERBOSE("AddFromBuildableInstanceData: %s", *BuildableClass->GetName());

	if (Cartograph::IsBuildableIgnored(BuildableClass.Get()))
	{
		return;
	}
	
	AddLightweightBuildable(BuildableInstanceData.TypeSpecificData, InstanceIndex, BuildableInstanceData.Transform, BuildableClass.Get());
}


void ACartographBuildingDataSubsystem::OnAddFromReplicatedData(const AFGLightweightBuildableSubsystem* ClassInstance, const TSubclassOf<AFGBuildable>& BuildableClass, const FLightweightBuildableReplicationItem& ReplicationData)
{
	if (!HasDataAuthority)
	{
		return;
	}

	CARTO_LOG_VERBOSE("AddFromReplicatedData: %s", *BuildableClass->GetName());

	if (Cartograph::IsBuildableIgnored(BuildableClass.Get()))
	{
		return;
	}

	const int32 InstanceIndex = ClassInstance->GetAllLightweightBuildableInstances().FindRef(BuildableClass).Num() - 1;
	AddLightweightBuildable(ReplicationData.TypeSpecificData, InstanceIndex, ReplicationData.Transform, BuildableClass.Get());
}


void ACartographBuildingDataSubsystem::OnRemoveBuildable(const AFGBuildable* Buildable)
{
	if (!HasDataAuthority)
	{
		return;
	}

	CARTO_LOG_VERBOSE("RemoveBuildable: %s", *Buildable->GetClass()->GetName());

	if (Cartograph::IsBuildableIgnored(Buildable->GetClass()))
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
	if (!HasDataAuthority)
	{
		return;
	}

	CARTO_LOG_VERBOSE("InvalidateRuntimeInstanceDataForIndex: %s", *BuildableClass->GetName());

	if (Cartograph::IsBuildableIgnored(BuildableClass.Get()))
	{
		return;
	}

	TMap<int32, uint32>* TypeMap = LightweightBuildableToArrayIndex.Find(BuildableClass.Get());
	if (!TypeMap)
	{
		CARTO_LOG_ERROR("Lightweight Buildable not found");
		return;
	}

	uint32 ArrayIndex = 0;
	if (!TypeMap->RemoveAndCopyValue(Index, ArrayIndex))
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


void ACartographBuildingDataSubsystem::OnBuildingDataHolderAddedFromReplication(ACartographBuildingDataHolder* Holder)
{
	BuildingDataHolders.Add(Holder);
}


void ACartographBuildingDataSubsystem::OnBuildingDataHolderRemovedFromReplication(ACartographBuildingDataHolder* Holder)
{
	BuildingDataHolders.Remove(Holder);
}


void ACartographBuildingDataSubsystem::BeginPlay()
{
	Super::BeginPlay();

	CARTO_LOG("CartographBuildingDataSubsystem BeginPlay");

	Instance = this; 
	HasDataAuthority = GetWorld()->GetNetMode() != NM_Client;

	if (HasDataAuthority)
	{
		GatherInitialBuildings();
	}
}


void ACartographBuildingDataSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Instance = nullptr;

	CARTO_LOG("CartographBuildingDataSubsystem EndPlay");

	for (TObjectPtr<ACartographBuildingDataHolder>& DataHolder : BuildingDataHolders)
	{
		DataHolder->Destroy();
	}

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

	for (const AFGBuildable* Buildable : Buildables)
	{
		AddBuildable(Buildable);
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

	if constexpr (Cartograph::ENABLE_DEBUG_LOG)
	{
		for (const auto [ClassHash, Count] : BuildingCountPerType)
		{
			CARTO_LOG_DEBUG("Building: %u, Count: %u", ClassHash, Count);
		}
	}
}


void ACartographBuildingDataSubsystem::AddBuildable(const AFGBuildable* Buildable)
{
	if (Cartograph::IsBuildableIgnored(Buildable->GetClass()))
	{
		return;
	}

	FBuildingData Data{ Buildable->GetTransform(), Buildable };

	OnBuildingDataInvalidated(Data.BuildableClassHash);
	BuildingCountPerType.FindOrAdd(Data.BuildableClassHash)++;

	AddBuildingData(std::move(Data));
	BuildableToArrayIndex.Add(Buildable, CurrentBuildingNum - 1);
}


void ACartographBuildingDataSubsystem::AddLightweightBuildable(const FFGDynamicStruct& TypeSpecificData, const int32 InstanceIndex, const FTransform& Transform, const TSoftClassPtr<AFGBuildable>& BuildableClass)
{
	FBuildingData Data{ Transform, TypeSpecificData, BuildableClass, InstanceIndex };

	OnBuildingDataInvalidated(Data.BuildableClassHash);
	BuildingCountPerType.FindOrAdd(Data.BuildableClassHash)++;

	AddBuildingData(std::move(Data));
	LightweightBuildableToArrayIndex.FindOrAdd(BuildableClass.Get()).Add(InstanceIndex, CurrentBuildingNum - 1);
}


void ACartographBuildingDataSubsystem::RemoveBuildingDuringRuntime(const TSoftClassPtr<AFGBuildable>& BuildableClass, const uint32 ArrayIndex)
{
	const uint32 TypeHash = Cartograph::GetHashWithRedirection(BuildableClass);
	if (TypeHash == 0)
	{
		return;
	}

	OnBuildingDataInvalidated(TypeHash);

	RemoveBuildingData(ArrayIndex);

	BuildingCountPerType.FindOrAdd(TypeHash)--;
}


void ACartographBuildingDataSubsystem::SendDataToNiagara()
{
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

	const auto* GameInstanceModule = UCartographGameInstanceModule::Instance;
	CARTO_LOG_ERROR_RETURN_IF_NULL(GameInstanceModule);

	if (GameInstanceModule->SplineBuildables.Contains(DirtyType))
	{
		// Randomly chosen number; didn't measure
		constexpr int AveragePointCountPerSpline = Cartograph::MAX_SPLINE_POINT_COUNT / 3;
		PositionsParam.Reset(*Count * AveragePointCountPerSpline);
		SplineGroupIdsParam.Reset(*Count * AveragePointCountPerSpline);
		SplineOrdersParam.Reset(*Count * AveragePointCountPerSpline);

		int32 SplineIndex = 0;
		for (const TObjectPtr<ACartographBuildingDataHolder>& DataHolder : BuildingDataHolders)
		{
			for (const FBuildingData& BuildingData : DataHolder->BuildingDataArray.Items)
			{
				if (BuildingData.BuildableClassHash != DirtyType)
				{
					continue;
				}

				const FSplineBuildingData* SplineData = std::get_if<FSplineBuildingData>(&BuildingData.CuboidOrSplineData);
				CARTO_LOG_ERROR_CONTINUE_IF_NULL(SplineData);

				const int Size = SplineData->Points.Num();
				for (int i = 0; i < Size; i++)
				{
					PositionsParam.Add(SplineData->Points[i]);
					SplineGroupIdsParam.Add(SplineIndex);
					SplineOrdersParam.Add(static_cast<float>(i));
				}

				SplineIndex++;
			}
		}

		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(Niagara, "User.Positions", PositionsParam);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(Niagara, "User.SplineGroupIds", SplineGroupIdsParam);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(Niagara, "User.SplineOrders", SplineOrdersParam);

		if constexpr (Cartograph::ENABLE_DEBUG_LOG)
		{
			CARTO_LOG_DEBUG("Niagara Data: %s", *GameInstanceModule->ClassIDToClassPtrMap[DirtyType].LoadSynchronous()->GetName());
			for (const FVector& Position : PositionsParam)
			{
				CARTO_LOG_DEBUG("Position: %s", *Position.ToString());
			}
			for (const int32 GroupId : SplineGroupIdsParam)
			{
				CARTO_LOG_DEBUG("Spline Group Id: %d", GroupId);
			}
			for (const float Order : SplineOrdersParam)
			{
				CARTO_LOG_DEBUG("Spline Order: %f", Order);
			}
		}
	}
	else
	{
		PositionsParam.Reset(*Count);
		RotationsParam.Reset(*Count);
		ScalesParam.Reset(*Count);

		for (const TObjectPtr<ACartographBuildingDataHolder>& DataHolder : BuildingDataHolders)
		{
			for (const FBuildingData& BuildingData : DataHolder->BuildingDataArray.Items)
			{
				if (BuildingData.BuildableClassHash != DirtyType)
				{
					continue;
				}

				const FCuboidBuildingData* CuboidData = std::get_if<FCuboidBuildingData>(&BuildingData.CuboidOrSplineData);
				CARTO_LOG_ERROR_CONTINUE_IF_NULL(CuboidData);

				const FTransform& Transform = CuboidData->Transform;

				PositionsParam.Add(Transform.GetLocation());
				RotationsParam.Add(Transform.GetRotation());
				ScalesParam.Add(Transform.GetScale3D());
			}
		}

		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(Niagara, "User.Positions", PositionsParam);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(Niagara, "User.Rotations", RotationsParam);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Niagara, "User.Scales", ScalesParam);

		if constexpr (Cartograph::ENABLE_DEBUG_LOG)
		{
			CARTO_LOG_DEBUG("Niagara Data: %s", *GameInstanceModule->ClassIDToClassPtrMap[DirtyType].LoadSynchronous()->GetName());
			for (const FVector& Position : PositionsParam)
			{
				CARTO_LOG_DEBUG("Position: %s", *Position.ToString());
			}
			for (const FQuat& Rotation : RotationsParam)
			{
				CARTO_LOG_DEBUG("Rotation: %s", *Rotation.ToString());
			}
			for (const FVector& Scale : ScalesParam)
			{
				CARTO_LOG_DEBUG("Scale: %s", *Scale.ToString());
			}
		}
	}

	if (Niagara->IsActive())
	{
		Niagara->DeactivateImmediate();
	}
	Niagara->Activate(true);

	MapDrawSubsystem->MarkMapTextureDirty();
}


void ACartographBuildingDataSubsystem::AddBuildingData(FBuildingData BuildingData)
{
	if (BuildingDataHolders.IsEmpty() || BuildingDataHolders.Last()->BuildingDataArray.Items.Num() >= Cartograph::MAX_REPLICATED_ARRAY_LENGTH)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		BuildingDataHolders.Add(GetWorld()->SpawnActor<ACartographBuildingDataHolder>(SpawnParams));
	}

	FBuildingDataArray& BuildingDataArray = BuildingDataHolders.Last()->BuildingDataArray;
	BuildingDataArray.MarkItemDirty(BuildingDataArray.Items.Add_GetRef(std::move(BuildingData)));

	CurrentBuildingNum++;
}


void ACartographBuildingDataSubsystem::RemoveBuildingData(const uint32 Index)
{
	const std::div_t Div = std::div(Index, Cartograph::MAX_REPLICATED_ARRAY_LENGTH);
	const uint32 HolderIndex = Div.quot;
	const uint32 HolderArrayIndex = Div.rem;

	TObjectPtr<ACartographBuildingDataHolder>& LastBuildingDataHolder = BuildingDataHolders.Last();
	FBuildingDataArray& LastBuildingDataArray = LastBuildingDataHolder->BuildingDataArray;

	FBuildingDataArray& BuildingDataArray = BuildingDataHolders[HolderIndex]->BuildingDataArray;

	if (Index == CurrentBuildingNum - 1)
	{
		LastBuildingDataArray.Items.RemoveAt(LastBuildingDataArray.Items.Num() - 1);
		LastBuildingDataArray.MarkArrayDirty();
	}
	else
	{
		FBuildingData& LastBuildingData = LastBuildingDataArray.Items.Last();

		const auto& LastBuildingKey = LastBuildingData.BuildingKey;
		if (const auto* BuildableKey = std::get_if<TObjectKey<AFGBuildable>>(&LastBuildingKey))
		{
			BuildableToArrayIndex[*BuildableKey] = Index;
		}
		else
		{
			const auto& [Type, InstanceIndex] = std::get<std::pair<TSoftClassPtr<AFGBuildable>, int32>>(LastBuildingKey);
			LightweightBuildableToArrayIndex[Type][InstanceIndex] = Index;
		}

		if (HolderIndex == BuildingDataHolders.Num() - 1)
		{
			BuildingDataArray.Items.RemoveAtSwap(HolderArrayIndex);
			BuildingDataArray.MarkArrayDirty();
		}
		else
		{
			BuildingDataArray.Items[HolderArrayIndex] = std::move(LastBuildingData);
			BuildingDataArray.MarkItemDirty(BuildingDataArray.Items[HolderArrayIndex]);

			LastBuildingDataArray.Items.RemoveAt(LastBuildingDataArray.Items.Num() - 1);
			LastBuildingDataArray.MarkArrayDirty();
		}
	}

	if (LastBuildingDataHolder->BuildingDataArray.Items.IsEmpty())
	{
		BuildingDataHolders.Pop()->Destroy();
	}

	CurrentBuildingNum--;
}
