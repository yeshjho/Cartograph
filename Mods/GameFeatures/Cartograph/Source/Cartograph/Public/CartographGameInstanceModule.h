#pragma once

#include <array>

#include "CoreMinimal.h"
#include "GenericQuadTree.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Module/GameInstanceModule.h"

#include "FGBuildSubCategory.h"
#include "FGLightweightBuildableSubsystem.h"

#include "UE5Coro/UE5Coro.h"

#include "CartographDataStructure.h"

#include "CartographGameInstanceModule.generated.h"


class AFGBuildable;
class AFGLightweightBuildableSubsystem;
class UCanvasRenderTarget2D;
class UFGBuildCategory;
class UFGBuildSubCategory;
class UFGBuildingDescriptor;


constexpr double WEST_BOUND_CENTIMETERS = -324698.832031;
constexpr double EAST_BOUND_CENTIMETERS = 425301.832031;
constexpr double NORTH_BOUND_CENTIMETERS = -375000;
constexpr double SOUTH_BOUND_CENTIMETERS = 375000;
constexpr double MAP_WIDTH_CENTIMETERS = EAST_BOUND_CENTIMETERS - WEST_BOUND_CENTIMETERS;
constexpr double MAP_HEIGHT_CENTIMETERS = SOUTH_BOUND_CENTIMETERS - NORTH_BOUND_CENTIMETERS;

constexpr int RENDER_TEXTURE_SIZE = 1024 * 8;

constexpr double ORIGIN_UV[] = { -WEST_BOUND_CENTIMETERS / MAP_WIDTH_CENTIMETERS, -NORTH_BOUND_CENTIMETERS / MAP_HEIGHT_CENTIMETERS };
constexpr double PIXEL_PER_CENTIMETER[] = { RENDER_TEXTURE_SIZE / MAP_WIDTH_CENTIMETERS, RENDER_TEXTURE_SIZE / MAP_HEIGHT_CENTIMETERS };

constexpr int SPLINE_SEGMENTS = 8;


DECLARE_LOG_CATEGORY_EXTERN(LogCartograph, Display, All);


constexpr bool ENABLE_DEBUG_LOG = false;
constexpr bool ENABLE_VERBOSE_LOG = false;
constexpr bool ENABLE_VERY_VERBOSE_LOG = false;

constexpr bool DRAW_BOUNDARIES = false;

#define CARTO_LOG(format, ...) UE_LOG(LogCartograph, Display, TEXT("(%u)") TEXT(format), __LINE__ __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_WARNING(format, ...) UE_LOG(LogCartograph, Warning, TEXT("(%u)") TEXT(format), __LINE__ __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_ERROR(format, ...) UE_LOG(LogCartograph, Error, TEXT("(%u)") TEXT(format), __LINE__ __VA_OPT__(, __VA_ARGS__))

#define CARTO_LOG_DEBUG(format, ...) if constexpr (ENABLE_DEBUG_LOG) UE_LOG(LogCartograph, Display, TEXT(format) __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_VERBOSE(format, ...) if constexpr (ENABLE_VERBOSE_LOG) UE_LOG(LogCartograph, Display, TEXT(format) __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_VERY_VERBOSE(format, ...) if constexpr (ENABLE_VERY_VERBOSE_LOG) UE_LOG(LogCartograph, Display, TEXT(format) __VA_OPT__(, __VA_ARGS__))

#define CARTO_LOG_ERROR_DO_IF_NULL(ptr, action) if (!ptr) { CARTO_LOG_ERROR("'%s' is null", TEXT(#ptr)); action; }
#define CARTO_LOG_ERROR_RETURN_IF_NULL(ptr) CARTO_LOG_ERROR_DO_IF_NULL(ptr, return)
#define CARTO_LOG_ERROR_BREAK_IF_NULL(ptr) CARTO_LOG_ERROR_DO_IF_NULL(ptr, break)


USTRUCT()
struct FCategoryData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	FLinearColor MainColor;

	UPROPERTY(EditDefaultsOnly)
	FLinearColor OutlineColor;

	UPROPERTY(EditDefaultsOnly)
	float OutlineThickness;
};


USTRUCT()
struct FSplineData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	FLinearColor Color;

	UPROPERTY(EditDefaultsOnly)
    float Thickness;
};


USTRUCT()
struct FWireData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
    FLinearColor Color;

	UPROPERTY(EditDefaultsOnly)
	float Thickness;
};


USTRUCT()
struct FLayerSubCategoryData
{
	GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly)
    FName Name;

	UPROPERTY(EditDefaultsOnly)
	FText DisplayName;

	/** Lower = Earlier in the list **/
	UPROPERTY(EditDefaultsOnly)
	int Priority;
};


USTRUCT()
struct FLayerCategoryData : public FLayerSubCategoryData
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly)
	TArray<FLayerSubCategoryData> SubCategories;
};


USTRUCT()
struct FBuildLayerData
{
	GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, meta = (GetOptions = "GetLayerCategoryOptions"))
	FString Category;

	FName MainCategoryCache;
    FName SubCategoryCache;
};


// RecipeManager::Get doesn't work for clients
USTRUCT()
struct FBuildingDescriptorData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
    TSubclassOf<UFGCategory> Category;

	UPROPERTY(EditDefaultsOnly)
    TSubclassOf<UFGBuildSubCategory> SubCategory;

	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<UTexture2D> Icon;
};


struct FRuntimeConfig
{
	TSet<FName> DisabledLayerMainCategory;
	TMap<FName, TSet<FName>> DisabledLayerSubCategory;
	TSet<uint32> DisabledLayerBuildable;
};


/**
 * 
 */
UCLASS(PrioritizeCategories=("Draw Data", "Layer Data", "UI", "Advanced", "Default", "Generated Data"))
class CARTOGRAPH_API UCartographGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

    friend class ACartographModSubsystem;
	friend class FCartographCanvasRenderItem;
	friend class UCartographRemoteCallObject;

public:
	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;

	void OnWorldLoaded(UWorld* World);

	void OnLayerConfigChanged();

	const FBuildLayerData* GetBuildLayerData(uint32 ClassHash);

	bool DoesBuildingExist(uint32 ClassHash) const;

	// Applies BuildableClassRedirectMap
	TSubclassOf<AFGBuildable> ResolveBuildableClass(UClass* BuildableClass) const;

	template<typename T>
	const T* GetDataByBuildableClass(const TMap<TSoftClassPtr<AFGBuildable>, T>& ClassMap, const TMap<TSoftClassPtr<UFGBuildCategory>, T>& CategoryMap, UClass* BuildableClass) const;

private:
	void RedrawMap(bool bRedrawEntirely);
	UE5Coro::TCoroutine<> InitialBuildableGather(TArray<TWeakObjectPtr<AFGBuildable>> Factories, TMap<TSubclassOf<AFGBuildable>, TArray<FRuntimeBuildableInstanceData>> Buildings, FForceLatentCoroutine = {});
	UE5Coro::TCoroutine<> RedrawMapCoroutine(TArray<FBuildingData> AddedBuildings, TArray<FBuildingData> RemovedBuildings, bool bRedrawEntirely, FForceLatentCoroutine = {});

	void OnCoroutineFinishedOrCancelled();

	void ExecuteRedrawMapCoroutine(bool bRedrawEntirely);

	void UpdateZFilter();
	void ResetBuildingData();

	void RegisterMenuButton() const;

	void LoadRuntimeConfig();
    void SaveRuntimeConfig();

	void FillBuildLayerDataCache();

	void OnBuildingDataAdd(const FBuildingData& AddedBuildingData, int32 Pos);
    void OnBuildingDataRemove(const FBuildingData& RemovedBuildingData, int32 Pos);

	void GatherBuildables();
	void GatherModOverrides();

	static TSet<FTopLevelAssetPath> GetDerivedClassPaths(UClass* ParentClass);

	// For blueprint use only
private:
	UFUNCTION(BlueprintCallable)
	void OnZFilterUpdated(float Min, float Max);

	UFUNCTION(BlueprintCallable)
	void OnCartographMenuButtonClicked(UUserWidget* Widget, bool IsOpen);

	UFUNCTION(BlueprintCallable)
	void OnShowBuildingsCheckboxChanged(bool DoShow);

	UFUNCTION()
	TArray<FString> GetLayerCategoryOptions() const;

	UFUNCTION(BlueprintCallable)
	void OnVanillaMapMenuShown(const UUserWidget* Widget) const;


	template<typename T>
	void FillInMatchingProperties(const FProperty* StructPropertyToCompare, TArray<std::pair<const FProperty*, const FProperty*>>& Out);

	template<typename KeyType, typename ValueType>
	void ProcessOverrideData(TMap<KeyType, ValueType>& MapToBeOverriden, UClass* OverrideDataClass, FName PropertyName);

	template<typename T>
	void ProcessOverrideData(TSet<T>& MapToBeOverriden, UClass* OverrideDataClass, FName PropertyName);

	void ProcessLayerCategoriesOverride(UClass* OverrideDataClass);


public:
	inline static UCartographGameInstanceModule* Instance = nullptr;

	FRuntimeConfig RuntimeConfig;

#pragma region Static Data
	UPROPERTY()  // Generated Data
    TMap<uint32, TSubclassOf<AFGBuildable>> ClassIDToClassPtrMap;
	UPROPERTY()  // Generated Data
    TMap<TSubclassOf<AFGBuildable>, uint32> ClassPtrToClassIDMap;

	UPROPERTY()  // Generated Data
	TMap<TSubclassOf<AFGBuildable>, FBuildingDescriptorData> ClassPtrToDescriptorDataMap;

	TMap<TSoftClassPtr<AFGBuildable>, FString> ModdedBuildings;
    TMap<FString, FBuildLayerData> ModdedBuildLayerData;


	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Category Data")
	TMap<TSoftClassPtr<UFGBuildCategory>, FCategoryData> BuildCategoryDataMap;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Category Data")
	TMap<TSoftClassPtr<AFGBuildable>, FCategoryData> BuildableBuildCategoryDataOverrideMap;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Category Data")
	TMap<TSubclassOf<UFGFactoryCustomizationDescriptor_Material>, FCategoryData> MaterialBuildCategoryDataOverrideMap;


	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Override")
	TMap<TSoftClassPtr<AFGBuildable>, TSoftObjectPtr<UTexture2D>> BuildableIconOverrideMap;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Override")
	TMap<TSoftClassPtr<AFGBuildable>, FVector2D> BuildableSizeOverrideMap;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Override")
	TMap<TSoftClassPtr<AFGBuildable>, FRotator> BuildableExtraRotationMap;


	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Special Data")
	TMap<TSoftClassPtr<AFGBuildable>, FSplineData> BuildableSplineDataMap;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Special Data")
    TMap<TSoftClassPtr<AFGBuildable>, FWireData> BuildableWireDataMap;


	UPROPERTY(EditDefaultsOnly, Category = "Default")
	TMap<TSoftClassPtr<AFGBuildable>, TSoftClassPtr<AFGBuildable>> BuildableClassRedirectMap;

    UPROPERTY(EditDefaultsOnly, Category = "Default")
	TSet<TSoftClassPtr<AFGBuildable>> BuildableToIgnore;


	UPROPERTY(EditDefaultsOnly, Category = "Layer Data")
	TArray<FLayerCategoryData> LayerCategories;

	UPROPERTY(EditDefaultsOnly, Category = "Layer Data")
	TMap<TSoftClassPtr<UFGBuildCategory>, FBuildLayerData> BuildLayerDataMap;

	UPROPERTY(EditDefaultsOnly, Category = "Layer Data")
	TMap<TSoftClassPtr<AFGBuildable>, FBuildLayerData> BuildableBuildLayerDataOverrideMap;

	UPROPERTY(EditDefaultsOnly, Category = "Layer Data")
	TMap<TSubclassOf<UFGFactoryCustomizationDescriptor_Material>, FBuildLayerData> MaterialBuildLayerDataOverrideMap;


	static constexpr const char* UnspecifiedMainCategory = "Modded";

	UPROPERTY(EditDefaultsOnly, Category = "Unspecified Default Data")
	FCategoryData UnspecifiedCategoryData;

	// SegmentsConfigName should be None.
	UPROPERTY(EditDefaultsOnly, Category = "Unspecified Default Data")
	FSplineData UnspecifiedSplineData;

	UPROPERTY(EditDefaultsOnly, Category = "Unspecified Default Data")
    int UnspecifiedSplineSegments;

    UPROPERTY(EditDefaultsOnly, Category = "Unspecified Default Data")
    FWireData UnspecifiedWireData;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TObjectPtr<UCanvasRenderTarget2D> RenderTarget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSoftClassPtr<UUserWidget> MapContainerWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> MenuShowHideButtonWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> MenuWidget;
#pragma endregion


	TMap<uint32, const FBuildLayerData*> BuildLayerDataMapCache;
	TMap<uint32, uint32> BuildingCountMap;


	bool ShouldInitialize = false;
	UPROPERTY(BlueprintReadOnly)
	bool IsInitializing = false;

	UE5Coro::TCoroutine<> Coroutine = UE5Coro::TCoroutine<>::CompletedCoroutine;
	FDrawToRenderTargetContext RenderContext;
	FCanvas* CurrentCanvas = nullptr;
	TArray<FBuildingData> CurrentBuildingData;

	/// To get the building data from the quad tree,
	/// CurrentBuildingData[BuildingDataIndexRedirector[CurrentBuildingQuadTree]]
    ///	The redirector array doesn't get .Remove()'d, when a building is removed, since we can't update the quad tree's elements.

	TArray<int32> BuildingDataIndexRedirector;
	TQuadTree<int32> CurrentBuildingQuadTree{ FBox2D{ { WEST_BOUND_CENTIMETERS, NORTH_BOUND_CENTIMETERS }, { EAST_BOUND_CENTIMETERS, SOUTH_BOUND_CENTIMETERS } } };

	bool IsPendingRedraw = false;
	bool IsPendingRedrawEntire = false;
	TArray<FBuildingData> PendingAddBuildingData;
	TArray<FBuildingData> PendingRemoveBuildingData;

	bool IsRedrawingEntirely = false;
	FBox2D RedrawArea;
	std::array<uint32, 4> ScissorArea;

    bool IsClient = false;

	float MinZFilter = -std::numeric_limits<float>::max();
    float MaxZFilter = std::numeric_limits<float>::max();

	// For blueprint use only
protected:
	UPROPERTY(BlueprintReadOnly)
	float InitializeProgress = 0;

	UPROPERTY(BlueprintReadOnly)
	float MinHeight = -100;
	UPROPERTY(BlueprintReadOnly)
	float MaxHeight = 100;

	float MinCached = 0;
    float MaxCached = 1;

    UPROPERTY(BlueprintReadOnly)
    bool DoShowBuildings = true;
};



template<typename T>
const T* UCartographGameInstanceModule::GetDataByBuildableClass(const TMap<TSoftClassPtr<AFGBuildable>, T>& ClassMap, const TMap<TSoftClassPtr<UFGBuildCategory>, T>& CategoryMap, UClass* BuildableClass) const
{
	const T* Data = ClassMap.Find(BuildableClass);
	if (!Data)
	{
		const FBuildingDescriptorData* DescriptorData = ClassPtrToDescriptorDataMap.Find(BuildableClass);
		if (!DescriptorData)
		{
			CARTO_LOG_WARNING("Can't find descriptor data for %s", *BuildableClass->GetName());
			return nullptr;
		}

		Data = CategoryMap.Find(DescriptorData->SubCategory.Get());
		if (!Data)
		{
			Data = CategoryMap.Find(DescriptorData->Category.Get());
		}
	}

	return Data;
}


template<typename T>
concept IsFVector = std::is_same_v<T, FVector> || std::is_same_v<T, FVector2D>;


template<IsFVector T, IsFVector U>
FVector2D world_position_to_screen_position(const T& WorldPosition, const U& Size)
{
	return FVector2D{
		ORIGIN_UV[0] + (WorldPosition.X - Size.X / 2) / MAP_WIDTH_CENTIMETERS,
		ORIGIN_UV[1] + (WorldPosition.Y - Size.Y / 2) / MAP_HEIGHT_CENTIMETERS
	} * RENDER_TEXTURE_SIZE;
}


template<IsFVector T>
FVector2D screen_position_to_world_position(const T& ScreenPosition)
{
    return FVector2D{
        (ScreenPosition.X / RENDER_TEXTURE_SIZE - ORIGIN_UV[0]) * MAP_WIDTH_CENTIMETERS,
        (ScreenPosition.Y / RENDER_TEXTURE_SIZE - ORIGIN_UV[1]) * MAP_HEIGHT_CENTIMETERS
    };
}