#pragma once

#include "Subsystem/ModSubsystem.h"

#include "CoreMinimal.h"
#include "FGLightweightBuildableSubsystem.h"

#include "CartographDataStructure.h"

#include "CartographBuildingDataSubsystem.generated.h"


class ACartographBuildingDataHolder;


namespace Cartograph
{
	constexpr int MAX_RELIABLE_BUFFER_BITS = 65536 * 8;
	constexpr int MAX_REPLICATED_ARRAY_LENGTH = MAX_RELIABLE_BUFFER_BITS / FBuildingData::MAX_REPLICATION_SIZE_BITS;
}


/**
 *
 */
UCLASS(Abstract, Blueprintable, Transient)
class CARTOGRAPH_API ACartographBuildingDataSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ACartographBuildingDataSubsystem();

	virtual void Tick(float DeltaSeconds) override;

	void OnBuildableCountChangedFromReplication(uint32 ClassHash, int CountDelta);

	void OnAddBuildable(const AFGBuildable* Buildable);
	void OnAddFromBuildableInstanceData(const TSubclassOf<AFGBuildable>& BuildableClass, const FRuntimeBuildableInstanceData& BuildableInstanceData, bool FromSaveData, int32 InstanceIndex);
	void OnAddFromReplicatedData(const AFGLightweightBuildableSubsystem* ClassInstance, const TSubclassOf<AFGBuildable>& BuildableClass, const FLightweightBuildableReplicationItem& ReplicationData);
	void OnRemoveBuildable(const AFGBuildable* Buildable);
	void OnInvalidateRuntimeInstanceDataForIndex(const TSubclassOf<AFGBuildable>& BuildableClass, int32 Index);

	void OnBuildingDataInvalidated(const TSet<uint32>& InvalidatedTypes);
	void OnBuildingDataInvalidated(uint32 InvalidatedType);

	bool DoesBuildingExist(uint32 ClassHash) const;

	void OnBuildingDataHolderAddedFromReplication(ACartographBuildingDataHolder* Holder);
	void OnBuildingDataHolderRemovedFromReplication(ACartographBuildingDataHolder* Holder);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void GatherInitialBuildings();

private:
	void AddBuildable(const AFGBuildable* Buildable);
	void AddLightweightBuildable(const FFGDynamicStruct& TypeSpecificData, int32 InstanceIndex, const FTransform& Transform, const TSoftClassPtr<AFGBuildable>& BuildableClass);
	void RemoveBuildingDuringRuntime(const TSoftClassPtr<AFGBuildable>& BuildableClass, uint32 ArrayIndex);

	void SendDataToNiagara();

	void AddBuildingData(FBuildingData BuildingData);
	void RemoveBuildingData(uint32 Index);


public:
	inline static ACartographBuildingDataSubsystem* Instance = nullptr;


protected:
	UPROPERTY()
	TArray<TObjectPtr<ACartographBuildingDataHolder>> BuildingDataHolders;  // Server-only
	uint32 CurrentBuildingNum = 0;  // Server-only

	TMap<TObjectKey<AFGBuildable>, uint32> BuildableToArrayIndex;  // Server-only
	TMap<TSoftClassPtr<AFGBuildable>, TMap<int32 /*InstanceIndex*/, uint32>> LightweightBuildableToArrayIndex;  // Server-only

	TMap<uint32, uint32> BuildingCountPerType;
	TSet<uint32> DirtyTypes;

	bool HasDataAuthority = false;

	// Trade-off: Keeping as member variables not to allocate every time there's a modification.
	// More memory, better performance.
	TArray<FVector> PositionsParam;
	TArray<FQuat> RotationsParam;
	TArray<FVector> ScalesParam;
	TArray<int32> SplineGroupIdsParam;
	TArray<float> SplineOrdersParam;
};
