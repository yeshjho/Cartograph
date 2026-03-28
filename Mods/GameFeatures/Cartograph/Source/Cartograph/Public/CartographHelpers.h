#pragma once
#include "CoreMinimal.h"


DECLARE_LOG_CATEGORY_EXTERN(LogCartograph, Display, All);


constexpr bool ENABLE_DEBUG_LOG = true;
constexpr bool ENABLE_VERBOSE_LOG = false;
constexpr bool ENABLE_VERY_VERBOSE_LOG = false;


#define CARTO_LOG(format, ...) UE_LOG(LogCartograph, Display, TEXT("(%u)") TEXT(format), __LINE__ __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_WARNING(format, ...) UE_LOG(LogCartograph, Warning, TEXT("(%u)") TEXT(format), __LINE__ __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_ERROR(format, ...) UE_LOG(LogCartograph, Error, TEXT("(%u)") TEXT(format), __LINE__ __VA_OPT__(, __VA_ARGS__))

#define CARTO_LOG_DEBUG(format, ...) if constexpr (ENABLE_DEBUG_LOG) UE_LOG(LogCartograph, Display, TEXT(format) __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_VERBOSE(format, ...) if constexpr (ENABLE_VERBOSE_LOG) UE_LOG(LogCartograph, Display, TEXT(format) __VA_OPT__(, __VA_ARGS__))
#define CARTO_LOG_VERY_VERBOSE(format, ...) if constexpr (ENABLE_VERY_VERBOSE_LOG) UE_LOG(LogCartograph, Display, TEXT(format) __VA_OPT__(, __VA_ARGS__))

#define CARTO_LOG_ERROR_DO_IF_NULL(ptr, action) if (!ptr) { CARTO_LOG_ERROR("'%s' is null", TEXT(#ptr)); action; }
#define CARTO_LOG_ERROR_RETURN_IF_NULL(ptr) CARTO_LOG_ERROR_DO_IF_NULL(ptr, return)
#define CARTO_LOG_ERROR_BREAK_IF_NULL(ptr) CARTO_LOG_ERROR_DO_IF_NULL(ptr, break)
