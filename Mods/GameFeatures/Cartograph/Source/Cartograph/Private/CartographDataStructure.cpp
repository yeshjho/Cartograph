#include "CartographDataStructure.h"

#include "Engine/InheritableComponentHandler.h"

#include "FGBuildableBeam.h"
#include "FGSplineBuildableInterface.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"

#include "CartographGameInstanceModule.h"
#include "Util/QuantizedVector2DSerialization.h"


// This is used for actual equality check while removing
bool FBuildingData::operator==(const FBuildingData& Other) const
{
	return 
		BuildableClassHash == Other.BuildableClassHash && 
		Transform.Equals(Other.Transform) && 
		BuildableExtraData == Other.BuildableExtraData;
}


FArchive& operator<<(FArchive& Ar, TArray<FVector2D>& A)
{
	A.CountBytes(Ar);

	using SizeType = int32;
	SizeType SerializeNum = Ar.IsLoading() ? 0 : A.Num();

	Ar << SerializeNum;

	if (SerializeNum == 0)
	{
		// if we are loading, then we have to reset the size to 0, in case it isn't currently 0
		if (Ar.IsLoading())
		{
			A.Empty();
		}
		return Ar;
	}

	if (Ar.IsLoading())
	{
		// Required for resetting ArrayNum
		A.Empty(SerializeNum);

		for (SizeType i = 0; i < SerializeNum; i++)
		{
			SerializeQuantizedVector2D<1>(A.AddDefaulted_GetRef(), Ar);
		}
	}
	else
	{
		for (SizeType i = 0; i < SerializeNum; i++)
		{
			SerializeQuantizedVector2D<1>(A[i], Ar);
		}
	}

	return Ar;
}


FArchive& operator<<(FArchive& Ar, std::monostate&)
{
	return Ar;
}


FArchive& operator<<(FArchive& Ar, FSplineExtraData& SplineData)
{
	Ar << SplineData.Points;
	return Ar;
}


FArchive& operator<<(FArchive& Ar, FWireExtraData& WireData)
{
	SerializeQuantizedVector2D<1>(WireData.End, Ar);
	return Ar;
}


FArchive& operator<<(FArchive& Ar, FBeamExtraData& BeamData)
{
	if (!Ar.IsLoading())  // Serialize
	{
		WriteFixedCompressedFloat<16384, 16>(BeamData.Length, Ar);
	}
	else  // Deserialize
	{
		ReadFixedCompressedFloat<16384, 16>(BeamData.Length, Ar);
	}
	return Ar;
}


FArchive& operator<<(FArchive& Ar, FBuildingData& BuildingData)
{
	bool _;
	BuildingData.NetSerialize(Ar, nullptr, _);
	return Ar;
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

	if (Ar.IsLoading())
	{
		VisualBoxCache = FBox2D{ ForceInit };
		if (const TSoftClassPtr<AFGBuildable>* Class = UCartographGameInstanceModule::Instance->ClassIDToClassPtrMap.Find(BuildableClassHash))
		{
			FillInCache(Class->LoadSynchronous());
		}
		else
		{
			CARTO_LOG_WARNING("Class ID %u not found in ClassIDToClassPtrMap", BuildableClassHash);
		}
	}

	bOutSuccess = true;
	return true;
}


void FBuildingData::AddExtraData(AFGBuildable* Buildable)
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
		Transform = SplineComponent->GetComponentTransform();

		FSplineExtraData ExtraData;
        ExtraData.Points.Reserve(SPLINE_SEGMENTS);
        const float Step = SplineComponent->Duration / SPLINE_SEGMENTS;
		for (int i = 0; i < SPLINE_SEGMENTS + 1; i++)
		{
			const FVector Pos = SplineComponent->GetLocationAtTime(i * Step, ESplineCoordinateSpace::World, true);
            ExtraData.Points.Add(FVector2D{ Pos });
		}

		BuildableExtraData = std::move(ExtraData);
		return;
	}

	if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()))
	{
		const auto* Wire = Cast<AFGBuildableWire>(Buildable);
		Transform.SetLocation(Wire->GetConnectionLocation(0));

		BuildableExtraData = FWireExtraData{
			.End = FVector2D{ Wire->GetConnectionLocation(1) },
		};

		return;
	}
}


void FBuildingData::AddExtraData(const FFGDynamicStruct& TypeSpecificData)
{
	if (const auto* BeamData = TypeSpecificData.GetValuePtr<FBuildableBeamLightweightData>())
	{
		BuildableExtraData = FBeamExtraData{
			.Length = BeamData->BeamLength,
		};

		return;
	}
}


void FBuildingData::FillInCache(const TSubclassOf<AFGBuildable>& OriginalBuildableClass)
{
	DataType = EBuildingDataType::Invalid;

	CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);

	const auto& BuildableClassRedirectMap = UCartographGameInstanceModule::Instance->BuildableClassRedirectMap;
	const TSoftClassPtr<AFGBuildable>* RedirectClass = BuildableClassRedirectMap.Find(OriginalBuildableClass.Get());
	const TSubclassOf<AFGBuildable> BuildableClass = RedirectClass ? RedirectClass->LoadSynchronous() : OriginalBuildableClass.Get();

	const uint32* ClassID = UCartographGameInstanceModule::Instance->ClassPtrToClassIDMap.Find(BuildableClass.Get());
	if (!ClassID)
	{
		CARTO_LOG_ERROR("Can't find hash for %s", *BuildableClass->GetName());
		return;
	}

	const FBuildLayerData* LayerData = UCartographGameInstanceModule::Instance->GetBuildLayerData(*ClassID);
	if (!LayerData)
	{
		return;
	}
	LayerDataCache = LayerData;


	if (BuildableClass->ImplementsInterface(UFGSplineBuildableInterface::StaticClass()))
	{
		const auto& BuildableSplineDataMap = UCartographGameInstanceModule::Instance->BuildableSplineDataMap;

		const FSplineData* SplineData = BuildableSplineDataMap.Find(BuildableClass.Get());
		if (!SplineData && UCartographGameInstanceModule::Instance->ModdedBuildings.Contains(BuildableClass.Get()))
		{
            SplineData = &UCartographGameInstanceModule::Instance->UnspecifiedSplineData;
		}
		if (!SplineData)
		{
			CARTO_LOG_WARNING("Can't find spline data for %s", *BuildableClass->GetName());
			return;
		}

		if (SplineData->Thickness <= 0)  // We don't need to draw, so don't even bother initializing the data cache.
		{
			return;
		}

		const FSplineExtraData* SplineExtraData = std::get_if<FSplineExtraData>(&BuildableExtraData);
		if (!SplineExtraData)
		{
			CARTO_LOG_ERROR("Can't find spline extra data");
			return;
		}

		DataType = EBuildingDataType::Spline;
		DataCache = FSplineDataCache{
			.SplineData = SplineData,
		};

		FillInSplineVisualBoxCache();

        return;
	}
	else if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()))
	{
		const auto& BuildableWireDataMap = UCartographGameInstanceModule::Instance->BuildableWireDataMap;

		const FWireData* WireData = BuildableWireDataMap.Find(BuildableClass.Get());
        if (!WireData && UCartographGameInstanceModule::Instance->ModdedBuildings.Contains(BuildableClass.Get()))
        {
            WireData = &UCartographGameInstanceModule::Instance->UnspecifiedWireData;
        }
		if (!WireData)
		{
			CARTO_LOG_WARNING("Can't find wire data for %s", *BuildableClass->GetName());
			return;
		}

		if (WireData->Thickness <= 0)  // We don't need to draw, so don't even bother initializing the data cache.
		{
			return;
		}

		DataType = EBuildingDataType::Wire;
		DataCache = WireData;
	}
	else if (BuildableClass->IsChildOf(AFGBuildableBeam::StaticClass()))
	{
		const auto& BuildableWireDataMap = UCartographGameInstanceModule::Instance->BuildableWireDataMap;

		const FWireData* BeamData = BuildableWireDataMap.Find(BuildableClass.Get());
        if (!BeamData && UCartographGameInstanceModule::Instance->ModdedBuildings.Contains(BuildableClass.Get()))
        {
            BeamData = &UCartographGameInstanceModule::Instance->UnspecifiedWireData;
        }
		if (!BeamData)
		{
			CARTO_LOG_WARNING("Can't find beam data for %s", *BuildableClass->GetName());
			return;
		}

		if (BeamData->Thickness <= 0)  // We don't need to draw, so don't even bother initializing the data cache.
		{
			return;
		}

		DataType = EBuildingDataType::Beam;
		DataCache = BeamData;
	}
	else
	{
		FVector2D Size = GetBuildingSize(BuildableClass);
		if (Size.X == 0.f || Size.Y == 0.f)  // We don't need to draw, so don't even bother initializing the data cache.
		{
			return;
		}
		Size *= FVector2D{ Transform.GetScale3D() };

		FRotator Rotation = Transform.GetRotation().Rotator();
		auto& BuildableExtraRotationMap = UCartographGameInstanceModule::Instance->BuildableExtraRotationMap;
		if (const FRotator* ExtraRotation = BuildableExtraRotationMap.Find(BuildableClass.Get()))
		{
			Rotation += *ExtraRotation;
		}

		const FVector2D ScreenPosition = world_position_to_screen_position(Transform.GetLocation(), Size);

		auto& BuildableIconOverrideMap = UCartographGameInstanceModule::Instance->BuildableIconOverrideMap;
		if (const TSoftObjectPtr<UTexture2D>* Texture = BuildableIconOverrideMap.Find(BuildableClass.Get());
			Texture && !Texture->IsNull())
		{
			DataType = EBuildingDataType::Icon;
			DataCache = FNormalDataCache{
				.ScreenPosition = ScreenPosition,
				.Size = Size,
				.Rotation = Rotation,
				.IconOrRectangleData = *Texture,
			};
		}
		else
		{
			const FCategoryData* CategoryData = UCartographGameInstanceModule::Instance->GetDataByBuildableClass(
				UCartographGameInstanceModule::Instance->BuildableBuildCategoryDataOverrideMap,
				UCartographGameInstanceModule::Instance->BuildCategoryDataMap,
				BuildableClass.Get());
			if (!CategoryData && UCartographGameInstanceModule::Instance->ModdedBuildings.Contains(BuildableClass.Get()))
			{
				CategoryData = &UCartographGameInstanceModule::Instance->UnspecifiedCategoryData;
			}
			if (!CategoryData)
			{
				CARTO_LOG_WARNING("Can't find category data for %s", *BuildableClass->GetName());
				return;
			}

			FRectangleDataCache RectangleData{
				.CategoryData = CategoryData,
			};

			const float HalfWidth = Size.X / 2;
			const float HalfHeight = Size.Y / 2;
			RectangleData.Corners[0] = { -HalfWidth, -HalfHeight, 0 };
			RectangleData.Corners[1] = { HalfWidth, -HalfHeight, 0 };
			RectangleData.Corners[2] = { HalfWidth, HalfHeight, 0 };
			RectangleData.Corners[3] = { -HalfWidth, HalfHeight, 0 };

			FTransform TransformNoScale = Transform;
			TransformNoScale.SetScale3D(FVector::OneVector);
			for (FVector& Corner : RectangleData.Corners)
			{
				Corner = TransformNoScale.TransformPosition(Corner);
			}

			DataType = EBuildingDataType::Rectangle;
			DataCache = FNormalDataCache{
				.ScreenPosition = ScreenPosition,
				.Size = Size,
				.Rotation = Rotation,
				.IconOrRectangleData = std::move(RectangleData),
			};
		}
	}

	FillInVisualBoxCache(OriginalBuildableClass);
}


void FBuildingData::FillInHash(const TSubclassOf<AFGBuildable>& OriginalBuildableClass)
{
	CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);

	const auto& BuildableClassRedirectMap = UCartographGameInstanceModule::Instance->BuildableClassRedirectMap;
	const TSoftClassPtr<AFGBuildable>* RedirectClass = BuildableClassRedirectMap.Find(OriginalBuildableClass.Get());
	const TSubclassOf<AFGBuildable> BuildableClass = RedirectClass ? RedirectClass->LoadSynchronous() : OriginalBuildableClass.Get();

	if (const uint32* Hash = UCartographGameInstanceModule::Instance->ClassPtrToClassIDMap.Find(BuildableClass.Get()))
	{
		BuildableClassHash = *Hash;
	}
	else
	{
		CARTO_LOG_WARNING("Class %s not found in ClassPtrToClassIDMap", *BuildableClass->GetName());
	}
}


void FBuildingData::FillInHashAndCache(const TSubclassOf<AFGBuildable>& BuildableClass)
{
	FillInHash(BuildableClass);
	if (BuildableClassHash != 0)
	{
		FillInCache(BuildableClass);
	}
	else
	{
		DataType = EBuildingDataType::Invalid;
	}
}


constexpr float BoxExpansionCentimeters = 300;


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


void FBuildingData::FillInSplineVisualBoxCache()
{
	VisualBoxCache = FBox2D{ ForceInit };

	FSplineExtraData* SplineDataCachePtr = std::get_if<FSplineExtraData>(&BuildableExtraData);
	CARTO_LOG_ERROR_RETURN_IF_NULL(SplineDataCachePtr);

	for (const FVector2D& Point : SplineDataCachePtr->Points)
	{
		VisualBoxCache += Point;
	}
	VisualBoxCache = VisualBoxCache.ExpandBy(BoxExpansionCentimeters);
}


FVector2D FBuildingData::GetBuildingSize(const TSubclassOf<AFGBuildable>& BuildableClass)
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
