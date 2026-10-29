#include "CartographDataStructure.h"

#include "Engine/InheritableComponentHandler.h"

#include "FGBuildableBeam.h"
#include "FGSplineBuildableInterface.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"

#include "CartographGameInstanceModule.h"


FArchive& operator<<(FArchive& Ar, FBuildingData& BuildingData)
{
	bool _;
	BuildingData.NetSerialize(Ar, nullptr, _);
	return Ar;
}


FBuildingData::FBuildingData(FTransform Transform, const AFGBuildable* Buildable)
	: BuildableClassHash(GetHash(Buildable->GetClass()))
{
	FillTransformOrSplinePoints(std::move(Transform), Buildable);
}


FBuildingData::FBuildingData(FTransform Transform, const FFGDynamicStruct& TypeSpecificData, const TSoftClassPtr<AFGBuildable>& LightweightBuildableType)
	: BuildableClassHash(GetHash(LightweightBuildableType))
{
	FillTransformOrSplinePoints(std::move(Transform), TypeSpecificData);
}


bool FBuildingData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar.SerializeBits(&BuildableClassHash, 32);

	if (!Ar.IsLoading())  // Serialize
	{
		FVector Location = Transform.GetLocation();
		bOutSuccess &= SerializePackedVector<1, 24>(Location, Ar);
		Transform.Rotator().SerializeCompressedShort(Ar);

		// We really need to pack the data, so we'll ignore scales for the clients.
		//FVector Scale = Transform.GetScale3D();
		//bOutSuccess &= SerializePackedVector<1, 24>(Scale, Ar);
	}
	else  // Deserialize
	{
		FVector Location;
		bOutSuccess &= SerializePackedVector<1, 24>(Location, Ar);
		FRotator Rotation;
		Rotation.SerializeCompressedShort(Ar);
		Transform = FTransform{ Rotation, Location };

		//FVector Scale;
		//bOutSuccess &= SerializePackedVector<1, 24>(Scale, Ar);
		//Transform.SetScale3D(Scale);
	}

	//Ar << CustomizationData;

	int ExtraDataIndex = BuildableExtraData.index();
	Ar.SerializeBits(&ExtraDataIndex, 2);  // NOTE: Increase the bits if more extra data types are added
	if (!Ar.IsLoading())  // Serialize
	{
		std::visit([&Ar](auto&& Data)
			{
				Ar << Data;
			},
			BuildableExtraData
		);
	}
	else  // Deserialize
	{
		switch (ExtraDataIndex)
		{
		case 0:
			BuildableExtraData = std::monostate{};
			break;
		case 1:
		{
			FSplineExtraData SplineData{};
			Ar << SplineData;
			BuildableExtraData = std::move(SplineData);
			break;
		}
		case 2:
		{
			FWireExtraData WireData{};
			Ar << WireData;
			BuildableExtraData = std::move(WireData);
			break;
		}
		case 3:
		{
			FBeamExtraData BeamData{};
			Ar << BeamData;
			BuildableExtraData = std::move(BeamData);
			break;
		}
		default:
			break;
		}
	}

	bOutSuccess = true;
	return true;
}


void FBuildingData::FillTransformOrSplinePoints(FTransform Transform, const AFGBuildable* Buildable)
{
	CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);

	const TSoftClassPtr<AFGBuildable> Class = Buildable->GetClass();
	const TSoftClassPtr<AFGBuildable>* RedirectClass = UCartographGameInstanceModule::Instance->BuildableClassRedirectMap.Find(Class);
	const TSubclassOf<AFGBuildable> BuildableClass = RedirectClass ? RedirectClass->LoadSynchronous() : Class.LoadSynchronous();

	if (BuildableClass->ImplementsInterface(UFGSplineBuildableInterface::StaticClass()))
	{
		const auto* Spline = Cast<IFGSplineBuildableInterface>(Buildable);
		const USplineComponent* SplineComponent = Spline->GetSplineComponent();
		CARTO_LOG_ERROR_RETURN_IF_NULL(SplineComponent);

		TArray<FVector> Points;
		// TODO: Dynamic calculation so that sharp curves have more points
		//Points.Add(SplineComponent->GetLocationAtTime(i * Step, ESplineCoordinateSpace::World, true));

		TransformOrSplinePoints = std::move(Points);
	}
	else if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()))
	{
		const auto* Wire = Cast<AFGBuildableWire>(Buildable);

		TArray<FVector> Points;
		Points.Reserve(2);
		Points.Add(Wire->GetConnectionLocation(0));
		Points.Add(Wire->GetConnectionLocation(1));
		TransformOrSplinePoints = std::move(Points);
	}
	else
	{
		TransformOrSplinePoints = std::move(Transform);
	}
}


void FBuildingData::FillTransformOrSplinePoints(FTransform Transform, const FFGDynamicStruct& TypeSpecificData)
{
	if (const auto* BeamData = TypeSpecificData.GetValuePtr<FBuildableBeamLightweightData>())
	{
		TArray<FVector> Points;
		Points.Reserve(2);
		Points.Add(Transform.GetLocation());
		Points.Add(Points[0] + Transform.GetRotation().Vector() * BeamData->BeamLength);
		TransformOrSplinePoints = std::move(Points);
	}
	else
	{
		TransformOrSplinePoints = std::move(Transform);
	}
}



void FBuildingData::FillInVisualBoxCache(const TSubclassOf<AFGBuildable>& OriginalBuildableClass)
{
	VisualBoxCache = FBox2D{ ForceInit };

	CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);

	const auto& BuildableClassRedirectMap = UCartographGameInstanceModule::Instance->BuildableClassRedirectMap;
	const TSoftClassPtr<AFGBuildable>* RedirectClass = BuildableClassRedirectMap.Find(OriginalBuildableClass.Get());
	const TSubclassOf<AFGBuildable> BuildableClass = RedirectClass ? RedirectClass->LoadSynchronous() : OriginalBuildableClass.Get();

	if (BuildableClass->ImplementsInterface(UFGSplineBuildableInterface::StaticClass()))
	{
		FillInSplineVisualBoxCache();
	}
	else if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()))
	{
		const FWireExtraData* WireExtraData = std::get_if<FWireExtraData>(&BuildableExtraData);
		CARTO_LOG_ERROR_RETURN_IF_NULL(WireExtraData);

		VisualBoxCache += FVector2D{ Transform.GetLocation() };
		VisualBoxCache += WireExtraData->End;
	}
	else if (BuildableClass->IsChildOf(AFGBuildableBeam::StaticClass()))
	{
		const FBeamExtraData* BeamExtraData = std::get_if<FBeamExtraData>(&BuildableExtraData);
		CARTO_LOG_ERROR_RETURN_IF_NULL(BeamExtraData);

		const float Length = BeamExtraData->Length;
		const FVector Start = Transform.GetLocation();
		VisualBoxCache += FVector2D{ Start };
		VisualBoxCache += FVector2D{ Start + Transform.GetRotation().Vector() * Length };
	}
	else
	{
		FVector2D Size = GetBuildingSize(BuildableClass);
		if (Size.X == 0.f || Size.Y == 0.f)  // We don't need to draw, so don't even bother initializing the data cache.
		{
			return;
		}
		Size *= FVector2D{ Transform.GetScale3D() };

		FVector Corners[4];
		const float HalfWidth = Size.X / 2;
		const float HalfHeight = Size.Y / 2;
		Corners[0] = { -HalfWidth, -HalfHeight, 0 };
		Corners[1] = { HalfWidth, -HalfHeight, 0 };
		Corners[2] = { HalfWidth, HalfHeight, 0 };
		Corners[3] = { -HalfWidth, HalfHeight, 0 };

		FTransform TransformNoScale = Transform;
		TransformNoScale.SetScale3D(FVector::OneVector);
		for (FVector& Corner : Corners)
		{
			Corner = TransformNoScale.TransformPosition(Corner);
			VisualBoxCache += FVector2D{ Corner };
		}
	}

	if (VisualBoxCache.bIsValid)
	{
		VisualBoxCache = VisualBoxCache.ExpandBy(BoxExpansionCentimeters);
	}
}


uint32 FBuildingData::GetHash(const TSoftClassPtr<AFGBuildable>& OriginalBuildableClass)
{
	CARTO_LOG_ERROR_DO_IF_NULL(UCartographGameInstanceModule::Instance, return 0);

	const auto& BuildableClassRedirectMap = UCartographGameInstanceModule::Instance->BuildableClassRedirectMap;
	const TSoftClassPtr<AFGBuildable>* RedirectClass = BuildableClassRedirectMap.Find(OriginalBuildableClass.Get());
	const TSoftClassPtr<AFGBuildable> BuildableClass = RedirectClass ? *RedirectClass : OriginalBuildableClass.Get();

	if (const uint32* Hash = UCartographGameInstanceModule::Instance->ClassPtrToClassIDMap.Find(BuildableClass.Get()))
	{
		return *Hash;
	}
	else
	{
		CARTO_LOG_WARNING("Class %s not found in ClassPtrToClassIDMap", *BuildableClass.LoadSynchronous()->GetName());
		return 0;
	}
}


FVector FBuildingData::GetBuildingSize(const TSubclassOf<AFGBuildable>& BuildableClass)
{
	if (!UCartographGameInstanceModule::Instance)
	{
		return {};
	}

	const auto& BuildableSizeOverrideMap = UCartographGameInstanceModule::Instance->BuildableSizeOverrideMap;
	if (const FVector2D* Size = BuildableSizeOverrideMap.Find(BuildableClass.Get()))
	{
		return *Size;
	}

	const auto* CDO = GetDefault<AFGBuildable>(BuildableClass);
	if (!CDO)
	{
        CARTO_LOG_ERROR("Can't find CDO for %s", *BuildableClass->GetName());
        return {};
	}

	if (const FBox ClearanceBox = CDO->GetCombinedClearanceBox();
		ClearanceBox.IsValid)
	{
		return FVector2D{ ClearanceBox.GetSize() };
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
        CARTO_LOG_DEBUG("Calculated and cached building size from CDO: %s", *BuildableClass->GetName());
		const FVector2D Size2D{ Box.GetSize() };
        UCartographGameInstanceModule::Instance->BuildableSizeOverrideMap.Add(BuildableClass.Get(), Size2D);
		return Size2D;
	}

    CARTO_LOG_WARNING("Can't find size for %s", *BuildableClass->GetName());
    return {};
}
