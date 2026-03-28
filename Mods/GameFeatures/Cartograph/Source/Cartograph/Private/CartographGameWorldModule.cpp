#include "CartographGameWorldModule.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/InheritableComponentHandler.h"
#include "Niagara/Public/NiagaraComponent.h"

#include "FGBuildable.h"
#include "FGBuildableBeam.h"
#include "FGBuildableSubsystem.h"
#include "FGBuildableWire.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGSplineBuildableInterface.h"

#include "CartographGameInstanceModule.h"
#include "CartographHelpers.h"


namespace 
{

FTransform get_line_transform(const FVector& Start, const FVector& End, const float Thickness)
{
	const FVector Vector = End - Start;
	const float Length = Vector.Length();
	return FTransform{ Vector.ToOrientationQuat(), (Start + End) / 2, FVector{ Length, Thickness, 1 } };
}

}


void UCartographGameWorldModule::DispatchLifecycleEvent(const ELifecyclePhase Phase)
{
    Super::DispatchLifecycleEvent(Phase);

    if (Phase != ELifecyclePhase::POST_INITIALIZATION)
    {
        return;
    }

    CARTO_LOG("UCartographGameWorldModule Init");
    CARTO_LOG_ERROR_RETURN_IF_NULL(MapMarkerActor);
    CARTO_LOG_ERROR_RETURN_IF_NULL(MapCapture);

    const TArray<AFGBuildable*>& Buildables = AFGBuildableSubsystem::Get(GetWorld())->GetAllBuildablesRef();
    const TMap<TSubclassOf<AFGBuildable>, TArray<FRuntimeBuildableInstanceData>>& Lightweights =
		    AFGLightweightBuildableSubsystem::Get(GetWorld())->GetAllLightweightBuildableInstances();

    for (const AFGBuildable* Buildable : Buildables)
    {
		AddBuildable(Buildable);
    }

    for (const auto& [OriginalBuildableClass, Instances] : Lightweights)
    {
		const TSoftClassPtr<AFGBuildable>* RedirectClass = UCartographGameInstanceModule::Instance->BuildableClassRedirectMap.Find(OriginalBuildableClass.Get());
		const TSubclassOf<AFGBuildable> BuildableClass = RedirectClass ? RedirectClass->LoadSynchronous() : OriginalBuildableClass;
		if (UCartographGameInstanceModule::Instance->BuildableToIgnore.Contains(BuildableClass.Get()))
		{
			continue;
		}

		UNiagaraComponent* NiagaraComponent = GetNiagaraComponentPerType(BuildableClass.Get());
		const FLineVisual& LineVisual = GetLineVisual(BuildableClass);

		const int32 InstanceNum = Instances.Num();

		TArray<FTransform> Transforms;
		Transforms.SetNumUninitialized(InstanceNum);

		const FBox& BoundingBox = GetBuildingBoundingBox(BuildableClass);
		const FVector Center = BoundingBox.GetCenter();
		const FVector Size = BoundingBox.GetSize();

        for (int32 i = 0; i < InstanceNum; i++)
        {
			const FRuntimeBuildableInstanceData& Data = Instances[i];

			if (const auto* BeamData = Data.TypeSpecificData.GetValuePtr<FBuildableBeamLightweightData>())
			{
				const FVector Start = Data.Transform.GetLocation();
				const FVector End = Start + Data.Transform.GetRotation().Vector() * BeamData->BeamLength;
				Transforms[i] = get_line_transform(Start, End, LineVisual.Thickness);
			}
			else
			{
				Transforms[i] = Data.Transform;
				Transforms[i].SetScale3D(Size);
				Transforms[i].AddToTranslation(Center);
			}
        }

		NiagaraComponent->AddInstances(Transforms, false, true);
    }

    MapCapture->ShowOnlyActorComponents(MapMarkerActor);
    MapCapture->CaptureSceneDeferred();
}

UNiagaraComponent* UCartographGameWorldModule::GetNiagaraComponentPerType(const TSubclassOf<AFGBuildable>& BuildableClass)
{
	if (const TObjectPtr<UNiagaraComponent>* NiagaraComponentPtr = NiagaraComponentPerType.Find(BuildableClass.Get()))
	{
		return *NiagaraComponentPtr;
	}

	const FBuildableVisual& Visual = GetBuildingVisual(BuildableClass);

	auto* Niagara = Cast<UNiagaraComponent>(MapMarkerActor->AddComponentByClass(UNiagaraComponent::StaticClass(), false, FTransform::Identity, true));
	Niagara->SetAsset()
	Niagara->SetAllowScalability(false);

	Ism->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	Ism->CastShadow = false;
	Ism->bAffectDynamicIndirectLighting = false;
	Ism->bVisibleInReflectionCaptures = false;
	Ism->bVisibleInRealTimeSkyCaptures = false;
	Ism->bVisibleInRayTracing = false;
	Ism->bReceivesDecals = false;
	Ism->bUseAsOccluder = false;
	Ism->bNeverDistanceCull = true;

	//Ism->bOnlyOwnerSee = true;
	Ism->SetIsReplicated(true);

	Ism->SetStaticMesh(Visual.Mesh.LoadSynchronous());
	const int32 MaterialNum = Visual.Materials.Num();
	for (int32 i = 0; i < MaterialNum; i++)
	{
		Ism->SetMaterial(i, Visual.Materials[i]);
	}

	MapMarkerActor->FinishAddComponent(Ism, false, FTransform::Identity);

	NiagaraComponentPerType.Add(BuildableClass.Get(), Ism);
	return Ism;
}

void UCartographGameWorldModule::AddBuildable(const AFGBuildable* Buildable)
{
	UClass* const OriginalBuildableClass = Buildable->GetClass();
	const TSoftClassPtr<AFGBuildable>* RedirectClass = UCartographGameInstanceModule::Instance->BuildableClassRedirectMap.Find(OriginalBuildableClass);
	const TSubclassOf<AFGBuildable> BuildableClass = RedirectClass ? RedirectClass->LoadSynchronous() : OriginalBuildableClass;
	if (UCartographGameInstanceModule::Instance->BuildableToIgnore.Contains(BuildableClass.Get()))
	{
		return;
	}

	UInstancedStaticMeshComponent* Ism = GetIsm(BuildableClass);
	const FLineVisual& LineVisual = GetLineVisual(BuildableClass);

	if (BuildableClass->ImplementsInterface(UFGSplineBuildableInterface::StaticClass()))
	{
		const auto* Spline = Cast<IFGSplineBuildableInterface>(Buildable);
		const USplineComponent* SplineComponent = Spline->GetSplineComponent();
		CARTO_LOG_ERROR_RETURN_IF_NULL(SplineComponent);

		static TArray<FTransform> Transforms = []()
			{
				TArray<FTransform> Arr;
				Arr.SetNumUninitialized(8);
				return Arr;
			}();
		const float Step = SplineComponent->Duration / 8;
		for (int i = 1; i < 8; i++)
		{
			const FVector DirVector = SplineComponent->GetWorldDirectionAtTime(i * Step, true);
			const FVector Prev = SplineComponent->GetWorldLocationAtTime((i - 1) * Step, true);
			const FVector Cur = SplineComponent->GetWorldLocationAtTime(i * Step, true);
			const FVector Next = SplineComponent->GetWorldLocationAtTime((i + 1) * Step, true);
			Transforms[i] = FTransform{ DirVector.ToOrientationQuat(), Cur, FVector{ (Next - Prev).Length(), LineVisual.Thickness, 1 } };
		}
		Ism->AddInstances(Transforms, false, true);
	}
	else if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()))
	{
		const auto* Wire = Cast<AFGBuildableWire>(Buildable);
		const FVector Start = Wire->GetConnectionLocation(0);
		const FVector End = Wire->GetConnectionLocation(1);

		Ism->AddInstance(get_line_transform(Start, End, LineVisual.Thickness), true);
	}
	else
	{
		const FBox& BoundingBox = GetBuildingBoundingBox(BuildableClass);
		FTransform Transform = Buildable->GetTransform();
		Transform.SetScale3D(BoundingBox.GetSize());
		Transform.AddToTranslation(BoundingBox.GetCenter());

		Ism->AddInstance(std::move(Transform), true);
	}
}

const FBox& UCartographGameWorldModule::GetBuildingBoundingBox(const TSubclassOf<AFGBuildable>& BuildableClass)
{
	static FBox Zero{ ForceInitToZero };

	if (const FBox* Box = BuildableBoundingBoxCache.Find(BuildableClass.Get()))
	{
		return *Box;
	}

	const AFGBuildable* CDO = Cast<AFGBuildable>(BuildableClass->ClassDefaultObject);
	if (!CDO)
	{
		CARTO_LOG_ERROR("Can't find CDO for %s", *BuildableClass->GetName());
		return Zero;
	}

	if (const FBox ClearanceBox = CDO->GetCombinedClearanceBox();
		ClearanceBox.IsValid)
	{
		CARTO_LOG_DEBUG("Fetched clearance box and cached building bounding box for %s: %s", *BuildableClass->GetName(), *ClearanceBox.ToString());
		return BuildableBoundingBoxCache.Add(BuildableClass.Get(), ClearanceBox);
	}

	FBox Box;

	const auto* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(BuildableClass.Get());
	while (BlueprintGeneratedClass)
	{
		if (const UInheritableComponentHandler* InheritableHandler = BlueprintGeneratedClass->InheritableComponentHandler)
		{
			TArray<UActorComponent*> Templates;
			InheritableHandler->GetAllTemplates(Templates, true);
			for (const UActorComponent* ComponentTemplate : Templates)
			{
				if (const auto* Component = Cast<USceneComponent>(ComponentTemplate))
				{
					Box += Component->GetLocalBounds().GetBox();
				}
			}
		}

		if (const USimpleConstructionScript* ConstructionScript = BlueprintGeneratedClass->SimpleConstructionScript)
		{
			for (const USCS_Node* Node : ConstructionScript->GetAllNodes())
			{
				if (!Node)
				{
					continue;
				}

				if (const auto* Component = Cast<USceneComponent>(Node->ComponentTemplate))
				{
					Box += Component->GetLocalBounds().GetBox();
				}
			}
		}

		BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(BlueprintGeneratedClass->GetSuperClass());
	}

	UClass* NativeClass = BuildableClass;
	while (NativeClass)
	{
		TArray<UObject*> DefaultObjectSubobjects;
		NativeClass->GetDefaultObjectSubobjects(DefaultObjectSubobjects);

		for (const UObject* DefaultSubObject : DefaultObjectSubobjects)
		{
			if (const auto* Component = Cast<USceneComponent>(DefaultSubObject))
			{
				Box += Component->GetLocalBounds().GetBox();
			}
		}

		NativeClass = NativeClass->GetSuperClass();
	}

	if (!Box.GetSize().IsZero())
	{
		CARTO_LOG_DEBUG("Calculated and cached building bounding box from CDO for %s: %s", *BuildableClass->GetName(), *Box.ToString());
		return BuildableBoundingBoxCache.Add(BuildableClass.Get(), Box);
	}

	CARTO_LOG_WARNING("Can't find bounding box for %s", *BuildableClass->GetName());
	return Zero;
}

const FBuildableVisual& UCartographGameWorldModule::GetBuildingVisual(const TSubclassOf<AFGBuildable>& BuildableClass) const
{
	const FBuildableVisual* Visual = VisualPerBuildable.Find(BuildableClass.Get());
	if (Visual)
	{
		return *Visual;
	}

	const FBuildingDescriptorData* DescriptorData = UCartographGameInstanceModule::Instance->ClassPtrToDescriptorDataMap.Find(BuildableClass);
	if (!DescriptorData)
	{
		CARTO_LOG_WARNING("Can't find descriptor data for %s", *BuildableClass->GetName());
		return FallbackVisual;
	}

	Visual = VisualPerCategory.Find(DescriptorData->SubCategory.Get());
	if (Visual)
	{
		return *Visual;
	}

	Visual = VisualPerCategory.Find(DescriptorData->Category.Get());
	if (Visual)
	{
		return *Visual;
	}

	return FallbackVisual;
}

const FLineVisual& UCartographGameWorldModule::GetLineVisual(const TSubclassOf<AFGBuildable>& BuildableClass) const
{
	const FLineVisual* LineVisual = LineVisualPerBuildable.Find(BuildableClass.Get());
	if (LineVisual)
	{
		return *LineVisual;
	}

	return FallbackLineVisual;
}
