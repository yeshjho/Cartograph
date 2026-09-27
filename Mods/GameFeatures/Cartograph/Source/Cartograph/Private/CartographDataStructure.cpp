#include "CartographDataStructure.h"

#include "Engine/InheritableComponentHandler.h"

#include "Buildables/FGBuildable.h"
#include "FGBuildableBeam.h"
#include "Buildables/FGBuildableWire.h"
#include "FGSplineBuildableInterface.h"

#include "CartographGameInstanceModule.h"
#include "Util/QuantizedVector2DSerialization.h"


// This is used for actual equality check while removing
bool FBuildingData::operator==(const FBuildingData& Other) const noexcept
{
	return BuildableClassHash == Other.BuildableClassHash && Transform.Equals(Other.Transform) && BuildableExtraData == Other.BuildableExtraData;
}


// This is used for sorting, so we only compare Z values
std::partial_ordering FBuildingData::operator<=>(const FBuildingData& Other) const noexcept
{
	return Transform.GetLocation().Z <=> Other.Transform.GetLocation().Z;
}


std::partial_ordering FBuildingData::operator<=>(float Z) const noexcept
{
	return Transform.GetLocation().Z <=> Z;
}


// Same layout as TArray's own serialization, but with quantized elements
FArchive& operator<<(FArchive& Ar, TArray<FVector2D>& A)
{
	int32 Num = A.Num();
	Ar << Num;
	if (Ar.IsLoading())
	{
		// Splines have SPLINE_SEGMENTS + 1 points, anything way bigger means a corrupted stream
		if (Num < 0 || Num > 1024)
		{
			Ar.SetError();
			return Ar;
		}
		A.SetNum(Num);
	}

	for (FVector2D& Point : A)
	{
		SerializeQuantizedVector2D<1>(Point, Ar);
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
		// Scale is ignored on purpose, we really need to pack the data.
	}
	else
	{
		FVector Location;
		bOutSuccess &= SerializePackedVector<1, 24>(Location, Ar);
		FRotator Rotation;
		Rotation.SerializeCompressedShort(Ar);
		Transform = FTransform{ Rotation, Location };
	}

	int ExtraDataIndex = BuildableExtraData.index();
	Ar.SerializeBits(&ExtraDataIndex, 2);  // NOTE: Increase the bits if more extra data types are added
	if (Ar.IsLoading())
	{
		switch (ExtraDataIndex)
		{
		case 1: BuildableExtraData.emplace<FSplineExtraData>(); break;
		case 2: BuildableExtraData.emplace<FWireExtraData>(); break;
		case 3: BuildableExtraData.emplace<FBeamExtraData>(); break;
		default: BuildableExtraData.emplace<std::monostate>(); break;
		}
	}
	std::visit([&Ar](auto& Data) { Ar << Data; }, BuildableExtraData);

	if (Ar.IsLoading())
	{
		VisualBoxCache = FBox2D{ ForceInit };
		if (const TSubclassOf<AFGBuildable>* Class = UCartographGameInstanceModule::Instance->ClassIDToClassPtrMap.Find(BuildableClassHash))
		{
			FillInCache(*Class);
		}
		else
		{
			CARTO_LOG_WARNING("Class ID %u not found in ClassIDToClassPtrMap", BuildableClassHash);
		}
	}

	bOutSuccess = !Ar.IsError();
	return bOutSuccess;
}


void FBuildingData::AddExtraData(AFGBuildable* Buildable)
{
	CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);

	const TSubclassOf<AFGBuildable> BuildableClass = UCartographGameInstanceModule::Instance->ResolveBuildableClass(Buildable->GetClass());

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


constexpr float BoxExpansionCentimeters = 300;


void FBuildingData::FillInCache(const TSubclassOf<AFGBuildable>& OriginalBuildableClass)
{
	// The visual box stays invalid for anything we don't draw, so it doesn't get into the quad tree
	DataType = EBuildingDataType::Invalid;
	VisualBoxCache = FBox2D{ ForceInit };

	CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);
	UCartographGameInstanceModule& Module = *UCartographGameInstanceModule::Instance;

	const TSubclassOf<AFGBuildable> BuildableClass = Module.ResolveBuildableClass(OriginalBuildableClass.Get());

	const uint32* ClassID = Module.ClassPtrToClassIDMap.Find(BuildableClass);
	if (!ClassID)
	{
		CARTO_LOG_ERROR("Can't find hash for %s", *BuildableClass->GetName());
		return;
	}

	const FBuildLayerData* LayerData = Module.GetBuildLayerData(*ClassID);
	if (!LayerData)
	{
		return;
	}
	LayerDataCache = LayerData;

	// Modded buildings without their own data fall back to the unspecified defaults
	const bool IsModded = Module.ModdedBuildings.Contains(BuildableClass.Get());

	if (BuildableClass->ImplementsInterface(UFGSplineBuildableInterface::StaticClass()))
	{
		const FSplineData* SplineData = Module.BuildableSplineDataMap.Find(BuildableClass.Get());
		if (!SplineData && IsModded)
		{
			SplineData = &Module.UnspecifiedSplineData;
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
		DataCache = FSplineDataCache{ .SplineData = SplineData };
		for (const FVector2D& Point : SplineExtraData->Points)
		{
			VisualBoxCache += Point;
		}
	}
	else if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()) || BuildableClass->IsChildOf(AFGBuildableBeam::StaticClass()))
	{
		// Both are drawn as a single line using FWireData
		const bool IsWire = BuildableClass->IsChildOf(AFGBuildableWire::StaticClass());

		const FWireData* LineData = Module.BuildableWireDataMap.Find(BuildableClass.Get());
		if (!LineData && IsModded)
		{
			LineData = &Module.UnspecifiedWireData;
		}
		if (!LineData)
		{
			CARTO_LOG_WARNING("Can't find wire data for %s", *BuildableClass->GetName());
			return;
		}

		if (LineData->Thickness <= 0)  // We don't need to draw, so don't even bother initializing the data cache.
		{
			return;
		}

		FVector2D End;
		if (IsWire)
		{
			const FWireExtraData* WireExtraData = std::get_if<FWireExtraData>(&BuildableExtraData);
			CARTO_LOG_ERROR_RETURN_IF_NULL(WireExtraData);
			End = WireExtraData->End;
		}
		else
		{
			const FBeamExtraData* BeamExtraData = std::get_if<FBeamExtraData>(&BuildableExtraData);
			CARTO_LOG_ERROR_RETURN_IF_NULL(BeamExtraData);
			End = FVector2D{ Transform.GetLocation() + Transform.GetRotation().Vector() * BeamExtraData->Length };
		}

		DataType = IsWire ? EBuildingDataType::Wire : EBuildingDataType::Beam;
		DataCache = LineData;
		VisualBoxCache += FVector2D{ Transform.GetLocation() };
		VisualBoxCache += End;
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
		if (const FRotator* ExtraRotation = Module.BuildableExtraRotationMap.Find(BuildableClass.Get()))
		{
			Rotation += *ExtraRotation;
		}

		FNormalDataCache NormalData{
			.ScreenPosition = world_position_to_screen_position(Transform.GetLocation(), Size),
			.Size = Size,
			.Rotation = Rotation,
		};

		const FCategoryData* CategoryData = nullptr;
		if (const TSoftObjectPtr<UTexture2D>* Texture = Module.BuildableIconOverrideMap.Find(BuildableClass.Get());
			Texture && !Texture->IsNull())
		{
			DataType = EBuildingDataType::Icon;
			NormalData.IconOrRectangleData = *Texture;
		}
		else
		{
			CategoryData = Module.GetDataByBuildableClass(Module.BuildableBuildCategoryDataOverrideMap, Module.BuildCategoryDataMap, BuildableClass.Get());
			if (!CategoryData && IsModded)
			{
				CategoryData = &Module.UnspecifiedCategoryData;
			}
			if (!CategoryData)
			{
				CARTO_LOG_WARNING("Can't find category data for %s", *BuildableClass->GetName());
				return;
			}
			DataType = EBuildingDataType::Rectangle;
		}

		// Corners of the rotated rectangle, which is also the visual box of an icon
		const FVector2D HalfSize = Size / 2;
		FTransform TransformNoScale = Transform;
		TransformNoScale.SetScale3D(FVector::OneVector);
		FRectangleDataCache RectangleData{ .CategoryData = CategoryData };
		RectangleData.Corners[0] = TransformNoScale.TransformPosition(FVector{ -HalfSize.X, -HalfSize.Y, 0 });
		RectangleData.Corners[1] = TransformNoScale.TransformPosition(FVector{ HalfSize.X, -HalfSize.Y, 0 });
		RectangleData.Corners[2] = TransformNoScale.TransformPosition(FVector{ HalfSize.X, HalfSize.Y, 0 });
		RectangleData.Corners[3] = TransformNoScale.TransformPosition(FVector{ -HalfSize.X, HalfSize.Y, 0 });
		for (const FVector& Corner : RectangleData.Corners)
		{
			VisualBoxCache += FVector2D{ Corner };
		}

		if (DataType == EBuildingDataType::Rectangle)
		{
			NormalData.IconOrRectangleData = std::move(RectangleData);
		}
		DataCache = std::move(NormalData);
	}

	if (VisualBoxCache.bIsValid)
	{
		VisualBoxCache = VisualBoxCache.ExpandBy(BoxExpansionCentimeters);
	}
}


void FBuildingData::FillInHash(const TSubclassOf<AFGBuildable>& OriginalBuildableClass)
{
	CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);

	const TSubclassOf<AFGBuildable> BuildableClass = UCartographGameInstanceModule::Instance->ResolveBuildableClass(OriginalBuildableClass.Get());

	if (const uint32* Hash = UCartographGameInstanceModule::Instance->ClassPtrToClassIDMap.Find(BuildableClass))
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
