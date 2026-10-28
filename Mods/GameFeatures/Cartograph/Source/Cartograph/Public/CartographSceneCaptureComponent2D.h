#pragma once

#include "Components/SceneCaptureComponent2D.h"

#include "CoreMinimal.h"


UCLASS()
class UCartographSceneCaptureComponent2D : public USceneCaptureComponent2D
{
    GENERATED_BODY()

public:
    virtual const AActor* GetViewOwner() const override { return GetOwner(); }
};
