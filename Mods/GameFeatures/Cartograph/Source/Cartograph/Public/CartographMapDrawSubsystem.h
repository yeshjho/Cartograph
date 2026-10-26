#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"

#include "CartographMapDrawSubsystem.generated.h"


class UUserWidget;


/**
 *
 */
UCLASS(Abstract, Blueprintable, Transient)
class CARTOGRAPH_API ACartographMapDrawSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ACartographMapDrawSubsystem();

	// Called when it's needed to recapture the map.
	void MarkMapTextureDirty();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void RedrawMap();

	UFUNCTION(BlueprintCallable)
	void OnZFilterUpdated(float Min, float Max);

	UFUNCTION(BlueprintCallable)
	void OnCartographMenuButtonClicked(UUserWidget* Widget, bool IsOpen);

	UFUNCTION(BlueprintCallable)
	void OnShowBuildingsCheckboxChanged(bool DoShow);

	UFUNCTION(BlueprintCallable)
	void OnVanillaMapMenuShown(const UUserWidget* Widget) const;


public:
	inline static ACartographMapDrawSubsystem* Instance = nullptr;

protected:
	bool IsMapTextureDirty = true;

	float MinZFilter = -std::numeric_limits<float>::max();
	float MaxZFilter = std::numeric_limits<float>::max();

	UPROPERTY(BlueprintReadWrite)
	bool IsMapBeingRendered = false;

	// For blueprint use only
protected:
	float MinCached = 0.f;
	float MaxCached = 1.f;

	UPROPERTY(BlueprintReadOnly)
	bool DoShowBuildings = true;
};
