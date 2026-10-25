#include "UI/CartographMenuLayerItemWidget.h"

#include "CartographBuildingDataSubsystem.h"
#include "CartographGameInstanceModule.h"


void UCartographMenuLayerItemWidget::Initialize_Native(const FName& InLayerCategory, const FName& InLayerSubCategory,
                                                       const uint32 InClassHash, UTexture2D* Icon, const FText& InBuildingName)
{
    LayerCategory = InLayerCategory;
    LayerSubCategory = InLayerSubCategory;
    ClassHash = InClassHash;
    BuildingName = InBuildingName;

    Initialize(Icon);
}


bool UCartographMenuLayerItemWidget::ShouldBeVisible() const
{
    const auto* BuildingDataSubsystem = ACartographBuildingDataSubsystem::Instance;
    return BuildingDataSubsystem && BuildingDataSubsystem->DoesBuildingExist(ClassHash);
}


void UCartographMenuLayerItemWidget::OnClicked(const bool IsChecked) const
{
    if (!IsChecked)
    {
        GameInstanceModule->RuntimeConfig.DisabledLayerBuildable.Add(ClassHash);
    }
    else
    {
        GameInstanceModule->RuntimeConfig.DisabledLayerBuildable.Remove(ClassHash);
    }
    GameInstanceModule->OnLayerConfigChanged();
}


bool UCartographMenuLayerItemWidget::GetInitialStatus() const
{
    return !GameInstanceModule->RuntimeConfig.DisabledLayerBuildable.Contains(ClassHash);
}
