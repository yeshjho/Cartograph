#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Subsystem/ModSubsystem.h"

#include "CartographHelpers.h"

#include "CartographMapDrawSubsystem.generated.h"


class UCartographSceneCaptureComponent2D;
class UNiagaraComponent;
class UUserWidget;


namespace Cartograph
{
	constexpr float MAP_CENTER_WORLD_COORD_X = (WEST_BOUND_CENTIMETERS + EAST_BOUND_CENTIMETERS) / 2.0;
	constexpr float MAP_CENTER_WORLD_COORD_Y = (NORTH_BOUND_CENTIMETERS + SOUTH_BOUND_CENTIMETERS) / 2.0;
}


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

	UNiagaraComponent* GetNiagaraComponentPerType(uint32 ClassHash);
	UTextureRenderTarget2D* GetRenderTarget() { return RenderTarget; }

	void OnMapOpen();
	void OnMapClose();

	void OnMapDisplaySizedChanged(const FVector2D& DisplaySizePixels);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void RedrawMap();

	UFUNCTION(BlueprintCallable)
	void OnZFilterUpdated(float Min, float Max);

	UFUNCTION(BlueprintCallable)
	void OnCartographMenuButtonClicked(UUserWidget* Widget, bool IsOpen);

	UFUNCTION(BlueprintCallable)
	void OnVanillaMapMenuShown(const UUserWidget* Widget) const;

	UFUNCTION(BlueprintCallable)
	void OnZoomScaleChanged(float NewScale);

	UFUNCTION(BlueprintCallable)
	void OnPanChanged(FVector2D NewMapCenterWorldPosition);


public:
	inline static ACartographMapDrawSubsystem* Instance = nullptr;

protected:
	bool IsMapTextureDirty = true;

	bool IsMapBeingRendered = false;

	float MinZFilter = Cartograph::MIN_HEIGHT_METERS;
	float MaxZFilter = Cartograph::MAX_HEIGHT_METERS;
	float ZoomScale = 1.f;
	FVector2D MapCenterWorldPosition = { Cartograph::MAP_CENTER_WORLD_COORD_X, Cartograph::MAP_CENTER_WORLD_COORD_Y };

	UPROPERTY()
	TObjectPtr<UCartographSceneCaptureComponent2D> SceneCaptureComponent;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY()
	TMap<uint32, TObjectPtr<UNiagaraComponent>> NiagaraComponentPerType;
};
