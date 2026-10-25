#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"

#include "CartographDataStructure.h"
#include "CartographHelpers.h"

#include "CartographGameInstanceModule.generated.h"


class AFGBuildable;
class UCanvasRenderTarget2D;
class UFGBuildCategory;
class UFGBuildSubCategory;
class UFGBuildingDescriptor;
class UFGCategory;
class UFGFactoryCustomizationDescriptor_Material;
class UTexture2D;
class UUserWidget;


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
	TSoftClassPtr<UFGCategory> Category;

	UPROPERTY(EditDefaultsOnly)
	TSoftClassPtr<UFGBuildSubCategory> SubCategory;

	UPROPERTY(EditDefaultsOnly)
	TSoftObjectPtr<UTexture2D> Icon;
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

public:
	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;

	void OnLayerConfigChanged();

	const FBuildLayerData* GetBuildLayerData(uint32 ClassHash);

	template<typename T>
	const T* GetDataByBuildableClass(const TMap<TSoftClassPtr<AFGBuildable>, T>& ClassMap, const TMap<TSoftClassPtr<UFGBuildCategory>, T>& CategoryMap, TSoftClassPtr<AFGBuildable> BuildableClass) const;

private:
	void RegisterMenuButton() const;

	static TSet<FTopLevelAssetPath> GetDerivedClassPaths(UClass* ParentClass);
	void GatherBuildables();
	void GatherModOverrides();
	void FlattenMaterialData();
	void FillBuildLayerDataCache();

	static void InstallHooks();

	void LoadRuntimeConfig();
    void SaveRuntimeConfig();


	template<typename T>
	void FillInMatchingProperties(const FProperty* StructPropertyToCompare, TArray<std::pair<const FProperty*, const FProperty*>>& Out);

	template<typename KeyType, typename ValueType>
	void ProcessOverrideData(TMap<KeyType, ValueType>& MapToBeOverriden, UClass* OverrideDataClass, const FName& PropertyName);

	template<typename T>
	void ProcessOverrideData(TSet<T>& SetToBeOverriden, UClass* OverrideDataClass, const FName& PropertyName);

	void ProcessLayerCategoriesOverride(UClass* OverrideDataClass);


	UFUNCTION()
	TArray<FString> GetLayerCategoryOptions() const;


public:
	inline static UCartographGameInstanceModule* Instance = nullptr;

	FRuntimeConfig RuntimeConfig;

#pragma region Static Data
	UPROPERTY()  // Generated Data
    TMap<uint32, TSoftClassPtr<AFGBuildable>> ClassIDToClassPtrMap;
	UPROPERTY()  // Generated Data
    TMap<TSoftClassPtr<AFGBuildable>, uint32> ClassPtrToClassIDMap;

	UPROPERTY()  // Generated Data
	TMap<TSoftClassPtr<AFGBuildable>, FBuildingDescriptorData> ClassPtrToDescriptorDataMap;

	TMap<TSoftClassPtr<AFGBuildable>, FString /*ModName*/> ModdedBuildings;
    TMap<FString /*ModName*/, FBuildLayerData> ModdedBuildLayerData;


	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Category Data")
	TMap<TSoftClassPtr<UFGBuildCategory>, FCategoryData> BuildCategoryDataMap;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Category Data")
	TMap<TSoftClassPtr<AFGBuildable>, FCategoryData> BuildableBuildCategoryDataOverrideMap;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Category Data")
	TMap<TSoftClassPtr<UFGFactoryCustomizationDescriptor_Material>, FCategoryData> MaterialBuildCategoryDataOverrideMap;


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
	TMap<TSoftClassPtr<UFGFactoryCustomizationDescriptor_Material>, FBuildLayerData> MaterialBuildLayerDataOverrideMap;


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
	TSoftObjectPtr<UCanvasRenderTarget2D> RenderTarget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSoftClassPtr<UUserWidget> MapContainerWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSoftClassPtr<UUserWidget> MenuShowHideButtonWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSoftClassPtr<UUserWidget> MenuWidget;
#pragma endregion

	TMap<uint32, const FBuildLayerData*> BuildLayerDataMapCache;
};


template<typename T>
const T* UCartographGameInstanceModule::GetDataByBuildableClass(const TMap<TSoftClassPtr<AFGBuildable>, T>& ClassMap, const TMap<TSoftClassPtr<UFGBuildCategory>, T>& CategoryMap, TSoftClassPtr<AFGBuildable> BuildableClass) const
{
	const T* Data = ClassMap.Find(BuildableClass);
	if (!Data)
	{
		const FBuildingDescriptorData* DescriptorData = ClassPtrToDescriptorDataMap.Find(BuildableClass);
		if (!DescriptorData)
		{
			CARTO_LOG_WARNING("Can't find descriptor data for %s", *BuildableClass.LoadSynchronous()->GetName());
			return nullptr;
		}

		Data = CategoryMap.Find(DescriptorData->SubCategory);
		if (!Data)
		{
			Data = CategoryMap.Find(DescriptorData->Category);
		}
	}

	return Data;
}
