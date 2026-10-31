#include "CartographDataStructure.h"

#include "FGBuildableBeam.h"
#include "FGSplineBuildableInterface.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"

#include "CartographGameInstanceModule.h"


FBuildingData::FBuildingData(FTransform Transform, const AFGBuildable* Buildable)
	: BuildableClassHash(Cartograph::GetHashWithRedirection(Buildable->GetClass()))
{
	FillTransformOrSplinePoints(std::move(Transform), Buildable);
}


FBuildingData::FBuildingData(FTransform Transform, const FFGDynamicStruct& TypeSpecificData, const TSoftClassPtr<AFGBuildable>& LightweightBuildableType)
	: BuildableClassHash(Cartograph::GetHashWithRedirection(LightweightBuildableType))
{
	FillTransformOrSplinePoints(std::move(Transform), TypeSpecificData);
}


bool FBuildingData::NetSerialize(FArchive& Ar, [[maybe_unused]] UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;

	const bool IsSerializing = Ar.IsSaving();

	Ar.SerializeBits(&BuildableClassHash, 32);

	bool IsCuboid = holds_alternative<FCuboidBuildingData>(CuboidOrSplineData);
	Ar.SerializeBits(&IsCuboid, 1);

	if (IsCuboid)
	{
		if (IsSerializing)
		{
			auto& CuboidData = get<FCuboidBuildingData>(CuboidOrSplineData);

			const FTransform& Transform = CuboidData.Transform;
			FVector Location = Transform.GetLocation();
			bOutSuccess &= SerializePackedVector<1, 20>(Location, Ar);
			Transform.Rotator().SerializeCompressedShort(Ar);
			// We really need to pack the data, so we'll ignore scales for the clients.
			// We use scale for variable size buildings, but right now they're variable only in height, 
			// so it only affects the z-filter correctness. A sacrifice willing to make.
			//FVector Scale = Transform.GetScale3D();
			//bOutSuccess &= SerializePackedVector<10, 20>(Scale, Ar);
		}
		else  // Deserialize
		{
			FVector Location;
			bOutSuccess &= SerializePackedVector<1, 20>(Location, Ar);
			FRotator Rotation;
			Rotation.SerializeCompressedShort(Ar);
			//FVector Scale;
			//bOutSuccess &= SerializePackedVector<10, 20>(Scale, Ar);
		}
	}
	else
	{
		uint32 Num = 0;
		if (IsSerializing)
		{
			auto& SplinePoints = get<FSplineBuildingData>(CuboidOrSplineData).Points;
			Num = static_cast<uint32>(SplinePoints.Num());
			Ar.SerializeIntPacked(Num);
			for (FVector& Point : SplinePoints)
			{
				bOutSuccess &= SerializePackedVector<1, 20>(Point, Ar);
			}
		}
		else
		{
			Ar.SerializeIntPacked(Num);
			if (Num > Cartograph::MAX_SPLINE_POINT_COUNT)
			{
				Ar.SetError();
				bOutSuccess = false;
				return true;
			}

			TArray<FVector> Points;
			Points.SetNumUninitialized(static_cast<int>(Num));
			for (FVector& Point : Points)
			{
				bOutSuccess &= SerializePackedVector<1, 20>(Point, Ar);
			}
			CuboidOrSplineData = FSplineBuildingData{ .Points = std::move(Points) };
		}
	}

	return true;
}


void FBuildingData::FillTransformOrSplinePoints(FTransform Transform, const AFGBuildable* Buildable)
{
	const TSubclassOf<AFGBuildable> BuildableClass = Cartograph::GetRedirectedBuildableClass(Buildable->GetClass()).LoadSynchronous();

	if (BuildableClass->ImplementsInterface(UFGSplineBuildableInterface::StaticClass()))
	{
		const auto* Spline = Cast<IFGSplineBuildableInterface>(Buildable);
		const USplineComponent* SplineComponent = Spline->GetSplineComponent();
		CARTO_LOG_ERROR_RETURN_IF_NULL(SplineComponent);

		TArray<FVector> Points;
		// TODO: Dynamic calculation so that sharp curves have more points
		//Points.Add(SplineComponent->GetLocationAtTime(i * Step, ESplineCoordinateSpace::World, true));

		CuboidOrSplineData = FSplineBuildingData{ .Points = std::move(Points) };
	}
	else if (BuildableClass->IsChildOf(AFGBuildableWire::StaticClass()))
	{
		const auto* Wire = Cast<AFGBuildableWire>(Buildable);

		TArray<FVector> Points;
		Points.Reserve(2);
		Points.Add(Wire->GetConnectionLocation(0));
		Points.Add(Wire->GetConnectionLocation(1));
		CuboidOrSplineData = FSplineBuildingData{ .Points = std::move(Points) };
	}
	else
	{
		FCuboidBuildingData Data{ .Transform = std::move(Transform) };

		const auto* GameInstanceModule = UCartographGameInstanceModule::Instance;
		CARTO_LOG_ERROR_DO_IF_NULL(GameInstanceModule,);
		if (GameInstanceModule && GameInstanceModule->VariableSizeBuildable.Contains(BuildableClass.Get()))
		{
			Data.Transform.MultiplyScale3D(Cartograph::GetBuildingSize(Buildable));
		}

		CuboidOrSplineData = std::move(Data);
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
		CuboidOrSplineData = FSplineBuildingData{ .Points = std::move(Points) };
	}
	else
	{
		// Don't think it's possible for other lightweight buildable to have a variable size
		CuboidOrSplineData = FCuboidBuildingData{ .Transform = std::move(Transform) };
	}
}
