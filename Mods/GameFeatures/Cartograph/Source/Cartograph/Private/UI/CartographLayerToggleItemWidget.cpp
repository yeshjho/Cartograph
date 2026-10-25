#include "UI/CartographLayerToggleItemWidget.h"

#include "CartographGameInstanceModule.h"
#include "CartographHelpers.h"
#include "UI/CartographMenuLayerItemWidget.h"
#include "UI/CartographMenuWidget.h"


void UCartographLayerToggleItemWidget::Initialize_Native(UCartographMenuWidget* InMenuWidget, const FName& InMainCategory, const FName& InSubCategory)
{
    MenuWidget = InMenuWidget;
    MainCategory = InMainCategory;
    SubCategory = InSubCategory;
}


void UCartographLayerToggleItemWidget::OnClicked(const bool IsChecked) const
{
    if (SubCategory.IsNone())
    {
        if (!IsChecked)
        {
            GameInstanceModule->RuntimeConfig.DisabledLayerMainCategory.Add(MainCategory);
        }
        else
        {
            GameInstanceModule->RuntimeConfig.DisabledLayerMainCategory.Remove(MainCategory);
        }
    }
    else
    {
	    TSet<FName>& SubCategoryData = GameInstanceModule->RuntimeConfig.DisabledLayerSubCategory.FindOrAdd(MainCategory);
        if (!IsChecked)
        {
            SubCategoryData.Add(SubCategory);
        }
        else
        {
            SubCategoryData.Remove(SubCategory);
        }
    }
    GameInstanceModule->OnLayerConfigChanged();
}


bool UCartographLayerToggleItemWidget::GetInitialStatus() const
{
    if (SubCategory.IsNone())
    {
        return !GameInstanceModule->RuntimeConfig.DisabledLayerMainCategory.Contains(MainCategory);
    }
    else
    {
        if (const TSet<FName>* SubCategories = GameInstanceModule->RuntimeConfig.DisabledLayerSubCategory.Find(MainCategory))
        {
            return !SubCategories->Contains(SubCategory);
        }
    }
    return true;
}


void UCartographLayerToggleItemWidget::SelectOrDeselectAll(const bool DoSelect)
{
    const auto LambdaProcessItems = 
    	[this, DoSelect](const TMap<FName, FMenuItem>& Items)
        {
            for (const auto& [_, MenuItem] : Items)
            {
                const auto* LayerItemWidget = Cast<UCartographMenuLayerItemWidget>(MenuItem.Widget);
                CARTO_LOG_ERROR_CONTINUE_IF_NULL(LayerItemWidget);

                if (LayerItemWidget->GetVisibility() == ESlateVisibility::Collapsed)
                {
                    continue;
                }

                if (DoSelect)
                {
                    GameInstanceModule->RuntimeConfig.DisabledLayerBuildable.Remove(LayerItemWidget->GetClassHash());
                }
                else
                {
                    GameInstanceModule->RuntimeConfig.DisabledLayerBuildable.Add(LayerItemWidget->GetClassHash());
                }
            }
        };

    const FHeadingItem* Layers = MenuWidget->GetMenuItemHierarchy().Find("Layers");
    CARTO_LOG_ERROR_RETURN_IF_NULL(Layers);

    const FMainCategoryItem* MainCategoryData = Layers->MainCategories.Find(MainCategory);
    CARTO_LOG_ERROR_RETURN_IF_NULL(MainCategoryData);

    if (SubCategory.IsNone())
    {
        LambdaProcessItems(MainCategoryData->Items);

        for (const auto& [_, SubCategoryData] : MainCategoryData->SubCategories)
        {
            LambdaProcessItems(SubCategoryData.Items);
        }
    }
    else
    {
        const FSubCategoryItem* SubCategoryData = MainCategoryData->SubCategories.Find(SubCategory);
        CARTO_LOG_ERROR_RETURN_IF_NULL(SubCategoryData);

        LambdaProcessItems(SubCategoryData->Items);
    }
    GameInstanceModule->OnLayerConfigChanged();
}
