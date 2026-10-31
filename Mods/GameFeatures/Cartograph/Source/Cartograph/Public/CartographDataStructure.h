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
	FBuildingData(FTransform Transform, const FFGDynamicStruct& TypeSpecificData, const TSoftClassPtr<AFGBuildable>& LightweightBuildableType);

	uint32 BuildableClassHash = 0;  // Redirection is applied
	std::variant<FCuboidBuildingData, FSplineBuildingData> CuboidOrSplineData;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

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
