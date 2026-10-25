#include "UI/CartographMenuWidget.h"

#include "Components/PanelWidget.h"

#include "Buildables/FGBuildable.h"
#include "Resources/FGBuildingDescriptor.h"

#include "CartographGameInstanceModule.h"
#include "UI/CartographLayerToggleItemWidget.h"
#include "UI/CartographMenuCategoryWidget.h"
#include "UI/CartographMenuLayerItemWidget.h"


void UCartographMenuWidget::InitializeHeadings(UPanelWidget* Panel)
{
    Algo::SortBy(HeadingData, &FHeadingData::Priority);
    for (const auto& [Name, DisplayName, _] : HeadingData)
    {
        auto* CategoryWidget = CreateWidget<UCartographMenuCategoryWidget>(this, CategoryWidgetType);
        CategoryWidget->Initialize(ECategoryType::Heading, DisplayName);
        Panel->AddChild(CategoryWidget);
        MenuItemHierarchy.Add(Name, FHeadingItem{ 
            FSubCategoryItem{
            	.DisplayName = DisplayName.ToString(),
	            .Items = {},
	            .CategoryWidget = CategoryWidget,
            }
        });
    }
}


void UCartographMenuWidget::InitializeLayers()
{
    FHeadingItem& LayerHeading = *MenuItemHierarchy.Find("Layers");

    Algo::SortBy(GameInstanceModule->LayerCategories, &FLayerCategoryData::Priority);
    for (FLayerCategoryData& CategoryData : GameInstanceModule->LayerCategories)
    {
        Algo::SortBy(CategoryData.SubCategories, &FLayerSubCategoryData::Priority);

        auto* CategoryWidget = CreateWidget<UCartographMenuCategoryWidget>(this, CategoryWidgetType);
        CategoryWidget->Initialize(ECategoryType::MainCategory, CategoryData.DisplayName);
        LayerHeading.CategoryWidget->AddCategory(CategoryWidget);
        auto* ToggleItemWidget = CreateWidget<UCartographLayerToggleItemWidget>(this, CategoryLayerToggleItemWidgetType);
        ToggleItemWidget->Initialize_Native(this, CategoryData.Name, FName{});
        CategoryWidget->AddItem(ToggleItemWidget, true);
        FMainCategoryItem MainCategoryItem{
            FSubCategoryItem{
                .DisplayName = CategoryData.DisplayName.ToString(),
                .Items = {},
                .CategoryWidget = CategoryWidget,
            }
        };

        for (FLayerSubCategoryData& SubCategoryData : CategoryData.SubCategories)
        {
            auto* SubCategoryWidget = CreateWidget<UCartographMenuCategoryWidget>(this, CategoryWidgetType);
            SubCategoryWidget->Initialize(ECategoryType::SubCategory, SubCategoryData.DisplayName);
            CategoryWidget->AddCategory(SubCategoryWidget);
            auto* ToggleItemWidgetSub = CreateWidget<UCartographLayerToggleItemWidget>(this, CategoryLayerToggleItemWidgetType);
            ToggleItemWidgetSub->Initialize_Native(this, CategoryData.Name, SubCategoryData.Name);
            SubCategoryWidget->AddItem(ToggleItemWidgetSub, true);
            MainCategoryItem.SubCategories.Add(SubCategoryData.Name, 
            	FSubCategoryItem{
	                .DisplayName = SubCategoryData.DisplayName.ToString(),
	                .CategoryWidget = SubCategoryWidget,
            });
        }

        LayerHeading.MainCategories.Add(CategoryData.Name, std::move(MainCategoryItem));
    }

    const auto& BuildableClassRedirectMap = UCartographGameInstanceModule::Instance->BuildableClassRedirectMap;
    for (const auto& [BuildableClass, _] : UCartographGameInstanceModule::Instance->ClassPtrToClassIDMap)
    {
	    if (!BuildableClass || UCartographGameInstanceModule::Instance->BuildableToIgnore.Contains(BuildableClass))
	    {
            continue;
	    }

        if (BuildableClassRedirectMap.Contains(BuildableClass.Get()))
	    {
		    continue;
	    }

        const uint32* ClassHash = UCartographGameInstanceModule::Instance->ClassPtrToClassIDMap.Find(BuildableClass);
        if (!ClassHash)
        {
            CARTO_LOG_ERROR("Can't find hash for %s", *BuildableClass->GetName());
            continue;
        }

        const FBuildLayerData* LayerData = UCartographGameInstanceModule::Instance->GetBuildLayerData(*ClassHash);
        if (!LayerData)
        {
            CARTO_LOG_WARNING("Can't find layer data for %s", *BuildableClass->GetName());
            continue;
        }

        const auto* DescriptorData = UCartographGameInstanceModule::Instance->ClassPtrToDescriptorDataMap.Find(BuildableClass);
        if (!DescriptorData)
        {
            CARTO_LOG_WARNING("Can't find descriptor data for %s", *BuildableClass->GetName());
            continue;
        }
        UTexture2D* Icon = DescriptorData->Icon.LoadSynchronous();
        if (!Icon)
        {
            continue;
        }

        const FText BuildingName = GetDefault<AFGBuildable>(BuildableClass.LoadSynchronous())->mDisplayName;

        FMainCategoryItem* MainCategoryItem = LayerHeading.MainCategories.Find(LayerData->MainCategoryCache);
        if (!MainCategoryItem)
        {
            CARTO_LOG_WARNING("Can't find main category %s", *LayerData->MainCategoryCache.ToString());
            continue;
        }

        if (LayerData->SubCategoryCache.IsNone())
        {
            auto* ItemWidget = CreateWidget<UCartographMenuLayerItemWidget>(this, CategoryLayerItemWidgetType);
            ItemWidget->Initialize_Native(LayerData->MainCategoryCache, NAME_None, *ClassHash, Icon, BuildingName);
            MainCategoryItem->CategoryWidget->AddItem(ItemWidget, false);
            MainCategoryItem->Items.Add(FName{ FString::FromInt(*ClassHash) }, FMenuItem{ .DisplayName = BuildingName.ToString(), .Widget = ItemWidget });
        }
        else
        {
            FSubCategoryItem* SubCategoryItem = MainCategoryItem->SubCategories.Find(LayerData->SubCategoryCache);
            if (!SubCategoryItem)
            {
                CARTO_LOG_WARNING("Can't find sub category %s", *LayerData->SubCategoryCache.ToString());
                continue;
            }

            auto* ItemWidget = CreateWidget<UCartographMenuLayerItemWidget>(this, CategoryLayerItemWidgetType);
            ItemWidget->Initialize_Native(LayerData->MainCategoryCache, LayerData->SubCategoryCache, *ClassHash, Icon, BuildingName);
            SubCategoryItem->CategoryWidget->AddItem(ItemWidget, false);
            SubCategoryItem->Items.Add(FName{ FString::FromInt(*ClassHash) }, FMenuItem{ .DisplayName = BuildingName.ToString(), .Widget = ItemWidget });
        }
    }

    CARTO_LOG("Initialized layers");
}


void UCartographMenuWidget::PostInitialize()
{
    for (const auto& [_, HeadingItem] : MenuItemHierarchy)
    {
        HeadingItem.CategoryWidget->PostInitialize();
        for (const auto& [__, MainCategoryItem] : HeadingItem.MainCategories)
        {
            MainCategoryItem.CategoryWidget->PostInitialize();
            for (const auto& [___, SubCategoryItem] : MainCategoryItem.SubCategories)
            {
                SubCategoryItem.CategoryWidget->PostInitialize();
            }
        }
    }
}


void UCartographMenuWidget::UpdateVisibilities(const FText SearchText)
{
    const bool IsSearchTextEmpty = SearchText.IsEmptyOrWhitespace();
    const FString SearchString = SearchText.ToString();

    for (const auto& [_, HeadingItem] : MenuItemHierarchy)
    {
        const bool DoesHeadingMatch = !IsSearchTextEmpty && HeadingItem.DisplayName.Contains(SearchString);
        bool ShouldHeadingVisible = false;
        for (const auto& [__, MainCategoryItem] : HeadingItem.MainCategories)
        {
            const bool DoesMainCategoryMatch = !IsSearchTextEmpty && MainCategoryItem.DisplayName.Contains(SearchString);
            bool ShouldMainCategoryVisible = false;
            for (const auto& [___, SubCategoryItem] : MainCategoryItem.SubCategories)
            {
                const bool DoesSubCategoryMatch = !IsSearchTextEmpty && SubCategoryItem.DisplayName.Contains(SearchString);
                bool ShouldSubCategoryVisible = false;
                for (const auto& [____, Item] : SubCategoryItem.Items)
                {
                    const bool ShouldBeVisible = 
                        (IsSearchTextEmpty || DoesHeadingMatch || DoesMainCategoryMatch || DoesSubCategoryMatch || Item.DisplayName.Contains(SearchString)) &&
                        Item.Widget->ShouldBeVisible();

                    Item.Widget->SetVisibility(ShouldBeVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
                    ShouldSubCategoryVisible |= ShouldBeVisible;
                }

                SubCategoryItem.CategoryWidget->SetVisibility(ShouldSubCategoryVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
            	ShouldMainCategoryVisible |= ShouldSubCategoryVisible;
            }

            MainCategoryItem.CategoryWidget->SetVisibility(ShouldMainCategoryVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
            ShouldHeadingVisible |= ShouldMainCategoryVisible;
        }

        HeadingItem.CategoryWidget->SetVisibility(ShouldHeadingVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
}
