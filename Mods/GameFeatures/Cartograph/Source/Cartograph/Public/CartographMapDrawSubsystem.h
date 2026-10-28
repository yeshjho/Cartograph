#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"

#include "CartographMapDrawSubsystem.generated.h"


class UCartographSceneCaptureComponent2D;
class UNiagaraComponent;
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

	UNiagaraComponent* GetNiagaraComponentPerType(uint32 ClassHash);

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

	UFUNCTION(BlueprintCallable)
	void OnMapOpen();

	UFUNCTION(BlueprintCallable)
	void OnMapClose();


public:
	inline static ACartographMapDrawSubsystem* Instance = nullptr;

protected:
	bool IsMapTextureDirty = true;

	float MinZFilter = -std::numeric_limits<float>::max();
	float MaxZFilter = std::numeric_limits<float>::max();

	bool IsMapBeingRendered = false;

	UPROPERTY(BlueprintRead)
	TObjectPtr<UCartographSceneCaptureComponent2D> SceneCaptureComponent;

	UPROPERTY(BlueprintRead)
	TMap<uint32, TObjectPtr<UNiagaraComponent>> NiagaraComponentPerType;

	// For blueprint use only
protected:
	float MinCached = 0.f;
	float MaxCached = 1.f;

	UPROPERTY(BlueprintReadOnly)
	bool DoShowBuildings = true;
};
