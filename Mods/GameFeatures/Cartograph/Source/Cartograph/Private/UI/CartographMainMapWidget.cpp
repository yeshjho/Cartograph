#include "UI/CartographMainMapWidget.h"

#include "Components/ScaleBox.h"

#include "CartographMapDrawSubsystem.h"
#include "Components/Image.h"


void UCartographMainMapWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (auto* MapDrawSubsystem = ACartographMapDrawSubsystem::Instance)
	{
		MapDrawSubsystem->OnMapOpen();
	}
}


void UCartographMainMapWidget::NativeDestruct()
{
	Super::NativeDestruct();

	if (auto* MapDrawSubsystem = ACartographMapDrawSubsystem::Instance)
	{
		MapDrawSubsystem->OnMapClose();
	}
}


void UCartographMainMapWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	auto* MapDrawSubsystem = ACartographMapDrawSubsystem::Instance;
	if (!MapDrawSubsystem)
	{
		return;
	}

	const FVector2D MapDisplaySize = BuildingLayerScaleBox->GetCachedGeometry().GetAbsoluteSize();
	if (MapDisplaySize != LastMapDisplaySize)
	{
		LastMapDisplaySize = MapDisplaySize;
		MapDrawSubsystem->OnMapDisplaySizedChanged(MapDisplaySize);
	}


    UTextureRenderTarget2D* RenderTarget = MapDrawSubsystem->GetRenderTarget();
    if (!RenderTarget)
    {
        return;
    }

	const FVector2D NewSize{ static_cast<double>(RenderTarget->SizeX), static_cast<double>(RenderTarget->SizeY) };

    if (LastKnownRenderTarget.Get() == RenderTarget && LastBrushSize == NewSize)
    {
        return;
    }

    FSlateBrush Brush = BuildingLayerImage->GetBrush();

    Brush.SetResourceObject(RenderTarget);
	Brush.SetImageSize(NewSize);
    Brush.DrawAs = ESlateBrushDrawType::Image;

    BuildingLayerImage->SetBrush(Brush);

	LastKnownRenderTarget = RenderTarget;
    LastBrushSize = NewSize;
}
