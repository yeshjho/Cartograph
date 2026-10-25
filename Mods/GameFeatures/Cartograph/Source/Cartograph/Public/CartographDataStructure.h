#pragma once
#include "CoreMinimal.h"

#include <variant>

#include "CartographDataStructure.generated.h"


class AFGBuildable;
class UTexture2D;
struct FFGDynamicStruct;


struct FSplineExtraData
{
	TArray<FVector2D> Points;

	bool operator==(const FSplineExtraData& Other) const = default;
};


struct FWireExtraData
{
	FVector2D End;

	bool operator==(const FWireExtraData& Other) const = default;
};


struct FBeamExtraData
{
	float Length = 0.f;

	bool operator==(const FBeamExtraData& Other) const = default;
};


enum class EBuildingDataType : uint8
{
	Invalid = 0,

	Icon,
	Rectangle,
	Spline,
	Wire,
	Beam,
};


struct FRectangleDataCache
{
	const struct FCategoryData* CategoryData = nullptr;
	FVector Corners[4]{};
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
	const struct FSplineData* SplineData = nullptr;
};


USTRUCT()
struct FBuildingData
{
	GENERATED_BODY()

#pragma region("Serialized")
	uint32 BuildableClassHash = 0;
	FTransform Transform;
	//FFactoryCustomizationData CustomizationData;
	std::variant<std::monostate, FSplineExtraData, FWireExtraData, FBeamExtraData> BuildableExtraData;
#pragma endregion


	EBuildingDataType DataType = EBuildingDataType::Invalid;
	std::variant<FNormalDataCache, FSplineDataCache, const struct FWireData*> DataCache;
	const struct FBuildLayerData* LayerDataCache = nullptr;
	FBox2D VisualBoxCache{ ForceInit };


	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	bool operator==(const FBuildingData& Other) const;

	void AddExtraData(AFGBuildable* Buildable);
	void AddExtraData(const FFGDynamicStruct& TypeSpecificData);

	void FillInCache(const TSubclassOf<AFGBuildable>& OriginalBuildableClass);  // Call it after filling in the extra data
	void FillInHash(const TSubclassOf<AFGBuildable>& OriginalBuildableClass);  // Call it after filling in the extra data
	void FillInHashAndCache(const TSubclassOf<AFGBuildable>& BuildableClass);  // Call it after filling in the extra data
    void FillInVisualBoxCache(const TSubclassOf<AFGBuildable>& OriginalBuildableClass);  // Call it after filling in the extra data

    void FillInSplineVisualBoxCache();  // Call it after filling in the extra data & cache

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
