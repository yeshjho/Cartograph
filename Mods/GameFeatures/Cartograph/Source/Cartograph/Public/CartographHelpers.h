#pragma once

#include "CoreMinimal.h"


DECLARE_LOG_CATEGORY_EXTERN(LogCartograph, Display, All);


constexpr double WEST_BOUND_CENTIMETERS = -324698.832031;
constexpr double EAST_BOUND_CENTIMETERS = 425301.832031;
constexpr double NORTH_BOUND_CENTIMETERS = -375000;
constexpr double SOUTH_BOUND_CENTIMETERS = 375000;
constexpr double MAP_WIDTH_CENTIMETERS = EAST_BOUND_CENTIMETERS - WEST_BOUND_CENTIMETERS;
constexpr double MAP_HEIGHT_CENTIMETERS = SOUTH_BOUND_CENTIMETERS - NORTH_BOUND_CENTIMETERS;

constexpr int RENDER_TEXTURE_SIZE = 1024 * 1;

constexpr double ORIGIN_UV[] = { -WEST_BOUND_CENTIMETERS / MAP_WIDTH_CENTIMETERS, -NORTH_BOUND_CENTIMETERS / MAP_HEIGHT_CENTIMETERS };
constexpr double PIXEL_PER_CENTIMETER[] = { RENDER_TEXTURE_SIZE / MAP_WIDTH_CENTIMETERS, RENDER_TEXTURE_SIZE / MAP_HEIGHT_CENTIMETERS };

constexpr int SPLINE_SEGMENTS = 8;


constexpr bool ENABLE_DEBUG_LOG = false;
constexpr bool ENABLE_VERBOSE_LOG = false;
constexpr bool ENABLE_VERY_VERBOSE_LOG = false;

constexpr bool DRAW_BOUNDARIES = false;


#define CARTO_LOG(format, ...) UE_LOG(LogCartograph, Display, TEXT("(%u)") TEXT(format), __LINE__ __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_WARNING(format, ...) UE_LOG(LogCartograph, Warning, TEXT("(%u)") TEXT(format), __LINE__ __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_ERROR(format, ...) UE_LOG(LogCartograph, Error, TEXT("(%u)") TEXT(format), __LINE__ __VA_OPT__(, __VA_ARGS__))

#define CARTO_LOG_DEBUG(format, ...) if constexpr (ENABLE_DEBUG_LOG) UE_LOG(LogCartograph, Display, TEXT(format) __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_VERBOSE(format, ...) if constexpr (ENABLE_VERBOSE_LOG) UE_LOG(LogCartograph, Display, TEXT(format) __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_VERY_VERBOSE(format, ...) if constexpr (ENABLE_VERY_VERBOSE_LOG) UE_LOG(LogCartograph, Display, TEXT(format) __VA_OPT__(, __VA_ARGS__))

#define CARTO_LOG_ERROR_DO_IF_NULL(ptr, action) if (!ptr) { CARTO_LOG_ERROR("'%s' is null", TEXT(#ptr)); action; }
#define CARTO_LOG_ERROR_RETURN_IF_NULL(ptr) CARTO_LOG_ERROR_DO_IF_NULL(ptr, return)
#define CARTO_LOG_ERROR_BREAK_IF_NULL(ptr) CARTO_LOG_ERROR_DO_IF_NULL(ptr, break)
#define CARTO_LOG_ERROR_CONTINUE_IF_NULL(ptr) CARTO_LOG_ERROR_DO_IF_NULL(ptr, continue)
