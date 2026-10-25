#pragma once

#include "Subsystem/ModSubsystem.h"

#include "CoreMinimal.h"
#include "FGLightweightBuildableSubsystem.h"

#include "CartographDataStructure.h"

#include "CartographBuildingDataSubsystem.generated.h"


class ACartographBuildingDataSubsystem;


USTRUCT()
struct FBuildingDataArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FBuildingData> Items;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams);

	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);
	void PostReplicatedReceive(const FPostReplicatedReceiveParameters& Parameters);

	TSet<uint32> DirtyTypes;
};


template<>
struct TStructOpsTypeTraits<FBuildingDataArray> : public TStructOpsTypeTraitsBase2<FBuildingDataArray>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};


/**
 *
 */
UCLASS(Abstract, Blueprintable, Transient)
class CARTOGRAPH_API ACartographBuildingDataSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ACartographBuildingDataSubsystem();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	void OnAddBuildable(const AFGBuildable* Buildable);
	void OnAddFromBuildableInstanceData(const TSubclassOf<AFGBuildable>& BuildableClass, const FRuntimeBuildableInstanceData& BuildableInstanceData, bool FromSaveData);
	void OnAddFromReplicatedData(const TSubclassOf<AFGBuildable>& BuildableClass, const FLightweightBuildableReplicationItem& ReplicationData);
	void OnRemoveBuildable(const AFGBuildable* Buildable);
	void OnInvalidateRuntimeInstanceDataForIndex(const AFGLightweightBuildableSubsystem* ClassInstance, const TSubclassOf<AFGBuildable>& BuildableClass, int32 Index);

	void OnBuildingDataInvalidated(const TSet<uint32>& InvalidatedTypes);
	void OnBuildingDataInvalidated(uint32 InvalidatedType);

	bool DoesBuildingExist(uint32 ClassHash) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void GatherInitialBuildings();

private:
	template<typename T>
	void AddBuilding(const T& Building, FTransform Transform, const TSubclassOf<AFGBuildable>& BuildableClass);


public:
	inline static ACartographBuildingDataSubsystem* Instance = nullptr;

protected:
	void OnBuildingDataAdd(const FBuildingData& AddedBuildingData, int32 Pos);
	void OnBuildingDataRemove(const FBuildingData& RemovedBuildingData, int32 Pos);


protected:
	UPROPERTY(Replicated)
	FBuildingDataArray BuildingDataArray;

	TSet<uint32> DirtyTypes;
};
