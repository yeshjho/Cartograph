#pragma once

#include "Subsystem/ModSubsystem.h"

#include "CoreMinimal.h"
#include "FGLightweightBuildableSubsystem.h"

#include "CartographBuildingDataSubsystem.generated.h"


/**
 *
 */
UCLASS(Abstract, Blueprintable, Transient)
class CARTOGRAPH_API ACartographBuildingDataSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ACartographBuildingDataSubsystem();

	void OnAddBuildable(AFGBuildable* Buildable);
	void OnAddFromBuildableInstanceData(TSubclassOf<AFGBuildable> BuildableClass, const FRuntimeBuildableInstanceData& BuildableInstanceData, bool FromSaveData);
	void OnAddFromReplicatedData(TSubclassOf<AFGBuildable> BuildableClass, const FLightweightBuildableReplicationItem& ReplicationData);
	void OnRemoveBuildable(AFGBuildable* Buildable);
	void OnInvalidateRuntimeInstanceDataForIndex(AFGLightweightBuildableSubsystem* ClassInstance, TSubclassOf<AFGBuildable> BuildableClass, int32 Index);

	bool DoesBuildingExist(uint32 ClassHash) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


public:
	inline static ACartographBuildingDataSubsystem* Instance = nullptr;

protected:
	void OnBuildingDataAdd(const FBuildingData& AddedBuildingData, int32 Pos);
	void OnBuildingDataRemove(const FBuildingData& RemovedBuildingData, int32 Pos);


protected:
	TMap<uint32, uint32> BuildingCountMap;
	TArray<FBuildingData> CurrentBuildingData;
};
