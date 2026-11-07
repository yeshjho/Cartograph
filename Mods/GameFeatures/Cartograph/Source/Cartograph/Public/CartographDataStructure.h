#pragma once

#include "CoreMinimal.h"

#include <variant>

#include "Net/Serialization/FastArraySerializer.h"

#include "CartographDataStructure.generated.h"


class AFGBuildable;
struct FFGDynamicStruct;


namespace Cartograph
{
	constexpr int MAX_SPLINE_POINT_COUNT = 24;
	constexpr int MAX_SPLINE_POINT_COUNT_DIVISION_FACTOR_FOR_REPLICATION = 3;
	constexpr int MAX_SPLINE_POINT_COUNT_FOR_REPLICATION = MAX_SPLINE_POINT_COUNT / MAX_SPLINE_POINT_COUNT_DIVISION_FACTOR_FOR_REPLICATION;
}


struct FCuboidBuildingData
{
	FTransform Transform;
};


struct FSplineBuildingData
{
	TArray<FVector> Points;
};


USTRUCT()
struct FBuildingData : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FBuildingData() = default;
	FBuildingData(FTransform Transform, const AFGBuildable* Buildable);
	FBuildingData(FTransform Transform, const FFGDynamicStruct& TypeSpecificData, const TSoftClassPtr<AFGBuildable>& LightweightBuildableType, int32 InstanceIndex);

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);


	uint32 BuildableClassHash = 0;  // Redirection is applied
	std::variant<FCuboidBuildingData, FSplineBuildingData> CuboidOrSplineData;

	std::variant<TObjectKey<AFGBuildable>, std::pair<TSoftClassPtr<AFGBuildable>, int32>> BuildingKey;  // Server-only

	static constexpr int MAX_REPLICATION_SIZE_BITS = 32 + 1 + std::max(20 * 3 + 16, 8 + Cartograph::MAX_SPLINE_POINT_COUNT_FOR_REPLICATION * (20 * 3));

private:
	void FillTransformOrSplinePoints(FTransform Transform, const AFGBuildable* Buildable);
	void FillTransformOrSplinePoints(FTransform Transform, const FFGDynamicStruct& TypeSpecificData);
};


template<>
struct TStructOpsTypeTraits<FBuildingData> : public TStructOpsTypeTraitsBase2<FBuildingData>
{
	enum
	{
		WithNetSerializer = true
	};
};
