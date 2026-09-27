#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CartographMenuItemWidget.generated.h"


/**
 * 
 */
UCLASS(Abstract)
class CARTOGRAPH_API UCartographMenuItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
    virtual bool ShouldBeVisible() const { return true; }
};
