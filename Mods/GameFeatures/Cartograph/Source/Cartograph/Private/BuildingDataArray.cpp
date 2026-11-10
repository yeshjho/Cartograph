#include "BuildingDataArray.h"

#include "CartographBuildingDataSubsystem.h"
#include "CartographHelpers.h"


bool FBuildingDataArray::NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
{
	return FastArrayDeltaSerialize<FBuildingData, FBuildingDataArray>(Items, DeltaParams, *this);
}


void FBuildingDataArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	for (const int32 Index : AddedIndices)
	{
		const FBuildingData& AddedItem = Items[Index];
		const uint32 ClassHash = AddedItem.BuildableClassHash;

		DirtyTypes.Add(ClassHash);
		Owner->OnBuildableCountChangedFromReplication(ClassHash, 1);

		ClassHashPerReplicationId.Add(AddedItem.ReplicationID, ClassHash);
	}
}


void FBuildingDataArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
	for (const int32 Index : RemovedIndices)
	{
		const FBuildingData& ToBeRemovedItem = Items[Index];
		const uint32 ClassHash = ToBeRemovedItem.BuildableClassHash;

		DirtyTypes.Add(ClassHash);
		Owner->OnBuildableCountChangedFromReplication(ClassHash, -1);

		ClassHashPerReplicationId.Remove(ToBeRemovedItem.ReplicationID);
	}
}


void FBuildingDataArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	for (const int32 Index : ChangedIndices)
	{
		const FBuildingData& ChangedItem = Items[Index];
		const uint32 ClassHash = ChangedItem.BuildableClassHash;
		const int32 ReplicationId = ChangedItem.ReplicationID;

		if (const uint32* OldClassHash = ClassHashPerReplicationId.Find(ReplicationId))
		{
			DirtyTypes.Add(*OldClassHash);
			Owner->OnBuildableCountChangedFromReplication(*OldClassHash, -1);
		}

		DirtyTypes.Add(ClassHash);
		Owner->OnBuildableCountChangedFromReplication(ClassHash, 1);

		ClassHashPerReplicationId.Add(ReplicationId, ClassHash);
	}
}


void FBuildingDataArray::PostReplicatedReceive(const FPostReplicatedReceiveParameters& Parameters)
{
	Owner->OnBuildingDataInvalidated(DirtyTypes);

	CARTO_LOG_VERBOSE("PostReplicatedReceive");
	if constexpr (Cartograph::ENABLE_DEBUG_LOG)
	{
		for (const uint32 DirtyType : DirtyTypes)
		{
			CARTO_LOG_DEBUG("%u", DirtyType);
		}
	}
	DirtyTypes.Reset();
}
