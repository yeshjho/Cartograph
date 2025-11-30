#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "MapCaptureComponent2D.generated.h"


/**
 * 
 */
UCLASS(hidecategories = (Collision, Object, Physics, SceneComponent), ClassGroup = Rendering, editinlinenew, meta = (BlueprintSpawnableComponent))
class CARTOGRAPH_API UMapCaptureComponent2D : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	virtual const AActor* GetViewOwner() const override;
};
