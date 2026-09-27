// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/QuantizedVector2DSerialization.h"

#include "HAL/PlatformMath.h"
#include "Logging/LogMacros.h"
#include "Math/Vector.h"
#include "Traits/IntType.h"


/* Returns the number of bits needed for the int32 to be replicated properly. At least 1 for the sign bit. */
inline uint32 GetBitsNeeded(const int32 Value)
{
	const uint32 MassagedValue = uint32(Value ^ (Value >> 31U));
	return 33U - static_cast<uint32>(FPlatformMath::CountLeadingZeros(MassagedValue));
}

/* Round to integer */
inline int32 RoundFloatToInt(float F)
{
	return int32(F + FPlatformMath::Sign(F) * 0.5f);
}


bool WriteQuantizedVector2D(const int32 Scale, const FVector2D& Value, FArchive& Ar)
{
	using ScalarType = float;
	constexpr SIZE_T ScalarTypeSize = sizeof(ScalarType);
	using IntType = typename TSignedIntType<ScalarTypeSize>::Type;

	static_assert(ScalarTypeSize == 4U || ScalarTypeSize == 8U, "Unknown floating point type.");

	// Beyond 2^MaxExponentForScaling scaling cannot improve the precision as the next floating point value is at least 1.0 more. 
	constexpr uint32 MaxExponentForScaling = ScalarTypeSize == 4 ? 23U : 52U;
	constexpr ScalarType MaxValueToScale = ScalarType(IntType(1) << MaxExponentForScaling);

	// Rounding of large values can introduce additional precision errors and the extra cost to serialize with full precision is small.
	constexpr uint32 MaxExponentAfterScaling = ScalarTypeSize == 4 ? 30U : 62U;
	constexpr ScalarType MaxScaledValue = ScalarType(IntType(1) << MaxExponentAfterScaling);

	const ScalarType Factor = IntCastChecked<int16>(Scale);
	FVector2D ScaledValue;
	ScaledValue.X = Value.X * Factor;
	ScaledValue.Y = Value.Y * Factor;

	// If the component values are within bounds then we optimize the bandwidth, otherwise we use full precision.
	if (ScaledValue.GetAbsMax() < MaxScaledValue)
	{
		const float AbsMin = FMath::Min(FMath::Abs(Value.X), FMath::Abs(Value.Y));
		const bool bUseScaledValue = AbsMin < MaxValueToScale;

		IntType X;
		IntType Y;
		if (bUseScaledValue)
		{
			X = RoundFloatToInt(ScaledValue.X);
			Y = RoundFloatToInt(ScaledValue.Y);
		}
		else
		{
			X = RoundFloatToInt(Value.X);
			Y = RoundFloatToInt(Value.Y);
		}

		const uint32 ComponentBitCount = FPlatformMath::Max(GetBitsNeeded(X), GetBitsNeeded(Y));
		uint32 ComponentBitCountAndScaleInfo = (bUseScaledValue ? (1U << 6U) : 0U) | ComponentBitCount;
		Ar.SerializeInt(ComponentBitCountAndScaleInfo, 1U << 7U);

		Ar.SerializeBits(&X, ComponentBitCount);
		Ar.SerializeBits(&Y, ComponentBitCount);
	}
	else
	{
		// A component bit count of 0 indicates full precision.
		constexpr uint32 ComponentBitCount = 0;
		uint32 ComponentBitCountAndTypeInfo = (ScalarTypeSize == 8U ? (1U << 6U) : 0U) | ComponentBitCount;
		Ar.SerializeInt(ComponentBitCountAndTypeInfo, 1U << 7U);
		Ar.SerializeBits(const_cast<FVector2D*>(&Value), ScalarTypeSize * 8U * 2U);
	}

	return true;
}


bool ReadQuantizedVector2D(const int32 Scale, FVector2D& Value, FArchive& Ar)
{
	using ScalarType = float;
	constexpr SIZE_T ScalarTypeSize = sizeof(ScalarType);
	static_assert(ScalarTypeSize == 4 || ScalarTypeSize == 8, "Unknown floating point type.");
	using IntType = typename TSignedIntType<ScalarTypeSize>::Type;

	uint32 ComponentBitCountAndExtraInfo = 0;
	Ar.SerializeInt(ComponentBitCountAndExtraInfo, 1U << 7U);
	const uint32 ComponentBitCount = ComponentBitCountAndExtraInfo & 63U;
	const uint32 ExtraInfo = ComponentBitCountAndExtraInfo >> 6U;

	if (ComponentBitCount > 0U)
	{
		int64 X = 0;
		int64 Y = 0;

		Ar.SerializeBits(&X, ComponentBitCount);
		Ar.SerializeBits(&Y, ComponentBitCount);

		// Sign-extend the values. The most significant bit read indicates the sign.
		const uint64 SignBit = (1ULL << (ComponentBitCount - 1U));
		X = (X ^ SignBit) - SignBit;
		Y = (Y ^ SignBit) - SignBit;

		FVector2D TempValue;
		TempValue.X = ScalarType(X);
		TempValue.Y = ScalarType(Y);

		// Apply scaling if needed.
		if (ExtraInfo)
		{
			Value = TempValue / ScalarType(Scale);
		}
		else
		{
			Value = TempValue;
		}

		return true;
	}
	else
	{
		FVector2D TempValue;
		Ar.SerializeBits(&TempValue, 4U * 8U * 2U);
		if (TempValue.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("%s"), TEXT("ReadQuantizedVector: Value isn't finite. Clearing for safety."));
			Value = FVector2D{ 0,0 };
			return false;
		}

		Value = FVector2D(TempValue);
		return true;
	}
}

