#pragma once

#include "CoreMinimal.h"

#include "Net/Serialization/FastArraySerializer.h"

#include "CartographDataStructure.h"

#include "BuildingDataArray.generated.h"


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
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	void PostReplicatedReceive(const FPostReplicatedReceiveParameters& Parameters);

	TSet<uint32> DirtyTypes;

	TSoftObjectPtr<ACartographBuildingDataSubsystem> Owner;

	TMap<int32, uint32> ClassHashPerReplicationId;
};


template<>
struct TStructOpsTypeTraits<FBuildingDataArray> : public TStructOpsTypeTraitsBase2<FBuildingDataArray>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};
