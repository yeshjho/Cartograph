#pragma once

#include "CoreMinimal.h"

#include <variant>

#include "Net/Serialization/FastArraySerializer.h"

#include "CartographDataStructure.generated.h"


class AFGBuildable;
struct FFGDynamicStruct;


USTRUCT()
struct FBuildingData : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FBuildingData() = default;
	FBuildingData(FTransform Transform, const AFGBuildable* Buildable);
	FBuildingData(FTransform Transform, const FFGDynamicStruct& TypeSpecificData, const TSoftClassPtr<AFGBuildable>& LightweightBuildableType);

#pragma region("Serialized")
	uint32 BuildableClassHash = 0;  // Redirection is applied
	std::variant<FTransform, TArray<FVector>> TransformOrSplinePoints;
#pragma endregion

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	static uint32 GetHash(const TSoftClassPtr<AFGBuildable>& OriginalBuildableClass);
	static FVector GetBuildingSize(const TSubclassOf<AFGBuildable>& BuildableClass);

private:
	void FillTransformOrSplinePoints(FTransform Transform, const AFGBuildable* Buildable);
	void FillTransformOrSplinePoints(FTransform Transform, const FFGDynamicStruct& TypeSpecificData);
};


FArchive& operator<<(FArchive& Ar, FBuildingData& BuildingData);


template<>
struct TStructOpsTypeTraits<FBuildingData> : public TStructOpsTypeTraitsBase2<FBuildingData>
{
	enum
	{
		WithNetSerializer = true
	};
};
