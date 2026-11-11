#include "UI/CartographMainMapWidget.h"

#include "Components/Image.h"
#include "Components/Overlay.h"

#include "FGBlueprintFunctionLibrary.h"
#include "UI/FGMapWidget.h"

#include "CartographMapDrawSubsystem.h"


namespace
{
    FNativeFuncPtr OriginalSetRenderTranslation = nullptr;

    void HookedSetRenderTranslation(UObject* Context, FFrame& Stack, RESULT_DECL)
    {
        OriginalSetRenderTranslation(Context, Stack, Z_Param__Result);

        UWidget* Widget = Cast<UWidget>(Context);
        if (!Widget || !Cast<UOverlay>(Widget))
        {
            return;
        }

        UFGMapWidget* MapWidget = Widget->GetTypedOuter<UFGMapWidget>();
        if (!MapWidget)
        {
            return;
        }

        auto* MainMapWidget = UFGBlueprintFunctionLibrary::GetFirstChildWidgetOfClass(MapWidget, UCartographMainMapWidget::StaticClass());
        if (!MainMapWidget)
        {
            return;
        }

        MainMapWidget->SetRenderTranslation(-Widget->GetRenderTransform().Translation);
    }
}


void UCartographMainMapWidget::InstallSetRenderTranslationHook()
{
    // I don't know why, but hooking with SUBSCRIBE_UOBJECT_METHOD_AFTER(UWidget, SetRenderTranslation) gets triggered only if called from C++.
    // The translation is modified in various functions in the widget blueprint, so I needed a central point.

	UFunction* Function = UWidget::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UWidget, SetRenderTranslation));
    CARTO_LOG_ERROR_RETURN_IF_NULL(Function);

	OriginalSetRenderTranslation = Function->GetNativeFunc();
    CARTO_LOG_ERROR_RETURN_IF_NULL(OriginalSetRenderTranslation);

	Function->SetNativeFunc(&HookedSetRenderTranslation);

    CARTO_LOG("Installed SetRenderTranslation");
}


void UCartographMainMapWidget::NativeConstruct()
{
	Super::NativeConstruct();

    CARTO_LOG("CartographMainMapWidget::NativeConstruct");

	if (auto* MapDrawSubsystem = ACartographMapDrawSubsystem::Instance)
	{
		MapDrawSubsystem->OnMapOpen();
	}
}


void UCartographMainMapWidget::NativeDestruct()
{
    CARTO_LOG("CartographMainMapWidget::NativeDestruct");

	if (auto* MapDrawSubsystem = ACartographMapDrawSubsystem::Instance)
	{
		MapDrawSubsystem->OnMapClose();
	}

    Super::NativeDestruct();
}


void UCartographMainMapWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	auto* MapDrawSubsystem = ACartographMapDrawSubsystem::Instance;
	if (!MapDrawSubsystem)
	{
		return;
	}

	const FVector2D MapDisplaySize = BuildingLayerImage->GetCachedGeometry().GetAbsoluteSize();
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
