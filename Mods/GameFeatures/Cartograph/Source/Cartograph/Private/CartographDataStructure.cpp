#include "CartographDataStructure.h"

#include "FGBuildableBeam.h"
#include "FGSplineBuildableInterface.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"

#include "CartographGameInstanceModule.h"


FBuildingData::FBuildingData(FTransform Transform, const AFGBuildable* Buildable)
	: BuildableClassHash(Cartograph::GetHashWithRedirection(Buildable->GetClass()))
	, BuildingKey(TObjectKey<AFGBuildable>{ Buildable })
{
	FillTransformOrSplinePoints(std::move(Transform), Buildable);
}


FBuildingData::FBuildingData(FTransform Transform, const FFGDynamicStruct& TypeSpecificData, const TSoftClassPtr<AFGBuildable>& LightweightBuildableType, int32 InstanceIndex)
	: BuildableClassHash(Cartograph::GetHashWithRedirection(LightweightBuildableType))
	, BuildingKey(std::make_pair(LightweightBuildableType, InstanceIndex))
{
	FillTransformOrSplinePoints(std::move(Transform), TypeSpecificData);
}


bool FBuildingData::NetSerialize(FArchive& Ar, [[maybe_unused]] UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;

	const bool IsSerializing = Ar.IsSaving();

	Ar.SerializeBits(&BuildableClassHash, 32);

	bool IsCuboid = std::holds_alternative<FCuboidBuildingData>(CuboidOrSplineData);
	Ar.SerializeBits(&IsCuboid, 1);

	if (IsCuboid)
	{
		if (IsSerializing)
		{
			auto& CuboidData = std::get<FCuboidBuildingData>(CuboidOrSplineData);

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
			CuboidOrSplineData = FCuboidBuildingData{ .Transform = FTransform{ Rotation, Location/*, Scale */ } };
		}
	}
	else
	{
		uint32 Num = 0;
		if (IsSerializing)
		{
			auto& SplinePoints = std::get<FSplineBuildingData>(CuboidOrSplineData).Points;

			const uint32 OriginalNum = static_cast<uint32>(SplinePoints.Num());
			const bool NeedToSkipPoints = OriginalNum > Cartograph::MAX_SPLINE_POINT_COUNT_FOR_REPLICATION;
			if (!NeedToSkipPoints)
			{
				Num = OriginalNum;
			}
			else
			{
				Num = OriginalNum / Cartograph::MAX_SPLINE_POINT_COUNT_DIVISION_FACTOR_FOR_REPLICATION;
			}
			Ar.SerializeIntPacked(Num);

			if (!NeedToSkipPoints)
			{
				for (FVector& Point : SplinePoints)
				{
					bOutSuccess &= SerializePackedVector<1, 20>(Point, Ar);
				}
			}
			else
			{
				const uint32 SourceIntervals = OriginalNum - 1;
				const uint32 OutputIntervals = Num - 1;

				for (uint32 i = 0; i < Num; ++i)
				{
					// Uniform degradation
					const uint32 SourceIndex = (i * SourceIntervals + OutputIntervals / 2) / OutputIntervals;
					bOutSuccess &= SerializePackedVector<1, 20>(SplinePoints[SourceIndex], Ar);
				}
			}
		}
		else
		{
			Ar.SerializeIntPacked(Num);
			if (Num > Cartograph::MAX_SPLINE_POINT_COUNT_FOR_REPLICATION)
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
		constexpr float MAX_DEVIATION_CM = 25.0f;
		constexpr float MAX_DEVIATION_SQUARED = MAX_DEVIATION_CM * MAX_DEVIATION_CM;

		const auto* Spline = Cast<IFGSplineBuildableInterface>(Buildable);
		const USplineComponent* SplineComponent = Spline->GetSplineComponent();
		CARTO_LOG_ERROR_RETURN_IF_NULL(SplineComponent);

		TArray<FVector> Points;

		struct FSegment
		{
			float StartDistance;
			float EndDistance;
			FVector StartLocation;
			FVector EndLocation;
			FVector Midpoint;
			float ErrorSquared;
		};

		const float SplineLength = SplineComponent->GetSplineLength();
		const FVector StartLocation = SplineComponent->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World);
		const FVector EndLocation = SplineComponent->GetLocationAtDistanceAlongSpline(SplineLength, ESplineCoordinateSpace::World);

		const auto MakeSegment = [SplineComponent](const float StartDistance, const float EndDistance, const FVector& SegmentStart, const FVector& SegmentEnd)
			{
				constexpr float TEST_ALPHAS[] = { 0.25f, 0.5f, 0.75f };

				FSegment Segment{
					.StartDistance = StartDistance,
					.EndDistance = EndDistance,
					.StartLocation = SegmentStart,
					.EndLocation = SegmentEnd,
					.Midpoint = FVector::ZeroVector,
					.ErrorSquared = 0.0f
				};

				for (const float Alpha : TEST_ALPHAS)
				{
					const float Distance = FMath::Lerp(StartDistance, EndDistance, Alpha);

					const FVector ActualLocation = SplineComponent->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
					const FVector LinearLocation = FMath::Lerp(SegmentStart, SegmentEnd, Alpha);

					if (const float ErrorSquared = FVector::DistSquared(ActualLocation, LinearLocation); 
						ErrorSquared > Segment.ErrorSquared)
					{
						Segment.ErrorSquared = ErrorSquared;
					}

					if (Alpha == 0.5f)
					{
						Segment.Midpoint = ActualLocation;
					}
				}

				return Segment;
			};

		TArray<FSegment> Segments;
		Segments.Reserve(Cartograph::MAX_SPLINE_POINT_COUNT - 1);

		Segments.Add(MakeSegment(0.0f, SplineLength, StartLocation, EndLocation));

		while (Segments.Num() + 1 < Cartograph::MAX_SPLINE_POINT_COUNT)
		{
			int32 WorstSegmentIndex = INDEX_NONE;
			float WorstErrorSquared = MAX_DEVIATION_SQUARED;

			for (int32 i = 0; i < Segments.Num(); ++i)
			{
				if (Segments[i].ErrorSquared > WorstErrorSquared)
				{
					WorstErrorSquared = Segments[i].ErrorSquared;
					WorstSegmentIndex = i;
				}
			}

			if (WorstSegmentIndex == INDEX_NONE)
			{
				break;
			}

			const FSegment Segment = Segments[WorstSegmentIndex];
			const float MidDistance = (Segment.StartDistance + Segment.EndDistance) * 0.5f;

			FSegment Left = MakeSegment(
				Segment.StartDistance,
				MidDistance,
				Segment.StartLocation,
				Segment.Midpoint);

			FSegment Right = MakeSegment(
				MidDistance,
				Segment.EndDistance,
				Segment.Midpoint,
				Segment.EndLocation);

			Segments[WorstSegmentIndex] = std::move(Left);
			Segments.Insert(std::move(Right), WorstSegmentIndex + 1);
		}

		Points.Reserve(Segments.Num() + 1);
		Points.Add(StartLocation);

		for (const FSegment& Segment : Segments)
		{
			Points.Add(Segment.EndLocation);
		}

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
