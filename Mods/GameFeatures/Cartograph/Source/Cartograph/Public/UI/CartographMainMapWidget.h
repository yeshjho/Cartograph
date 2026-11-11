#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CartographMainMapWidget.generated.h"


class UImage;
class UScaleBox;


/**
 *
 */
UCLASS()
class CARTOGRAPH_API UCartographMainMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
    static void InstallSetRenderTranslationHook();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;


protected:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UImage> BuildingLayerImage;

private:
    FVector2D LastMapDisplaySize{ EForceInit{} };
	FVector2D LastBrushSize{ EForceInit{} };
    TWeakObjectPtr<UTextureRenderTarget2D> LastKnownRenderTarget;
};
