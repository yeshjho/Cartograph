#pragma once
#include "CoreMinimal.h"

#include <variant>

#include "CartographDataStructure.generated.h"


class AFGBuildable;
struct FFGDynamicStruct;


struct FSplineExtraData
{
	TArray<FVector2D> Points;

	bool operator==(const FSplineExtraData& Other) const noexcept = default;
};


struct FWireExtraData
{
	FVector2D End;

	bool operator==(const FWireExtraData& Other) const noexcept = default;
};


struct FBeamExtraData
{
	float Length;

	bool operator==(const FBeamExtraData& Other) const noexcept = default;
};


enum class EBuildingDataType
{
	Invalid,
	Icon,
	Rectangle,
	Spline,
	Wire,
	Beam,
};


struct FRectangleDataCache
{
	const struct FCategoryData* CategoryData;
	FVector Corners[4];
};


struct FNormalDataCache
{
	FVector2D ScreenPosition;
	FVector2D Size;
	FRotator Rotation;
	std::variant<TSoftObjectPtr<UTexture2D>, FRectangleDataCache> IconOrRectangleData;
};


struct FSplineDataCache
{
	const struct FSplineData* SplineData;
};


USTRUCT()
struct FBuildingData
{
	GENERATED_BODY()

	uint32 BuildableClassHash = 0;
	FTransform Transform;
	std::variant<std::monostate, FSplineExtraData, FWireExtraData, FBeamExtraData> BuildableExtraData;


	EBuildingDataType DataType = EBuildingDataType::Invalid;
	std::variant<FNormalDataCache, FSplineDataCache, const struct FWireData*> DataCache;
	const struct FBuildLayerData* LayerDataCache = nullptr;
	FBox2D VisualBoxCache{ ForceInit };


	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	bool operator==(const FBuildingData& Other) const noexcept;
	std::partial_ordering operator<=>(const FBuildingData& Other) const noexcept;
	std::partial_ordering operator<=>(float Z) const noexcept;

	void AddExtraData(AFGBuildable* Buildable);
	void AddExtraData(const FFGDynamicStruct& TypeSpecificData);

	void FillInCache(const TSubclassOf<AFGBuildable>& OriginalBuildableClass);  // Call it after filling in the extra data
	void FillInHash(const TSubclassOf<AFGBuildable>& OriginalBuildableClass);  // Call it after filling in the extra data
	void FillInHashAndCache(const TSubclassOf<AFGBuildable>& BuildableClass);  // Call it after filling in the extra data

	static FVector2D GetBuildingSize(const TSubclassOf<AFGBuildable>& BuildableClass);
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
