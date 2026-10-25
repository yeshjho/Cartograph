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

	void RedrawMap();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
	float MinZFilter = -std::numeric_limits<float>::max();
	float MaxZFilter = std::numeric_limits<float>::max();

	// For blueprint use only
protected:
	UPROPERTY(BlueprintReadOnly)
	float MinHeight = -100;
	UPROPERTY(BlueprintReadOnly)
	float MaxHeight = 100;

	float MinCached = 0.f;
	float MaxCached = 1.f;

	UPROPERTY(BlueprintReadOnly)
	bool DoShowBuildings = true;
};
