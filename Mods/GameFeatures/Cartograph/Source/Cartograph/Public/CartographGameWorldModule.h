#pragma once

#include "CoreMinimal.h"
#include "Module/GameWorldModule.h"
#include "CartographGameWorldModule.generated.h"


class UNiagaraComponent;

class AFGBuildable;
class UFGBuildCategory;
class UFGFactoryCustomizationDescriptor_Material;


USTRUCT()
struct FBuildableVisual
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	FLinearColor FillColor;

	UPROPERTY(EditDefaultsOnly)
	FLinearColor BorderColor;

	UPROPERTY(EditDefaultsOnly)
	float Opacity;

	UPROPERTY(EditDefaultsOnly)
	float BorderThickness;
};

USTRUCT()
struct FLineVisual
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	float Thickness;
};


/**
 * 
 */
UCLASS()
class CARTOGRAPH_API UCartographGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;

private:
	UNiagaraComponent* GetNiagaraComponentPerType(const TSubclassOf<AFGBuildable>& BuildableClass);

	void AddBuildable(const AFGBuildable* Buildable);

	static const FBox& GetBuildingBoundingBox(const TSubclassOf<AFGBuildable>& BuildableClass);


protected:
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> MapMarkerActor;

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<USceneCaptureComponent2D> MapCapture;

private:
	TMap<TSoftClassPtr<AFGBuildable>, TObjectPtr<UNiagaraComponent>> NiagaraComponentPerType;
	TMap<TSoftClassPtr<AFGBuildable>, TMap<FTransform, int>> MarkedBuildingIndices;

	inline static TMap<TSoftClassPtr<AFGBuildable>, FBox> BuildableBoundingBoxCache;


	// move to game instance module
protected:
	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Visual")
	TMap<TSoftClassPtr<UFGBuildCategory>, FBuildableVisual> VisualPerCategory;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Visual")
	TMap<TSubclassOf<UFGFactoryCustomizationDescriptor_Material>, FBuildableVisual> VisualPerMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Visual")
	TMap<TSoftClassPtr<AFGBuildable>, FBuildableVisual> VisualPerBuildable;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Visual")
	FBuildableVisual FallbackVisual;


	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Line Visual")
	TMap<TSoftClassPtr<AFGBuildable>, FLineVisual> LineVisualPerBuildable;

	UPROPERTY(EditDefaultsOnly, Category = "Draw Data/Line Visual")
	FLineVisual FallbackLineVisual;

	const FBuildableVisual& GetBuildingVisual(const TSubclassOf<AFGBuildable>& BuildableClass) const;
	const FLineVisual& GetLineVisual(const TSubclassOf<AFGBuildable>& BuildableClass) const;
};
