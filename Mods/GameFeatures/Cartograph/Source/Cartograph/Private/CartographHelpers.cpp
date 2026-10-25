#include "CartographHelpers.h"

#include "CartographGameInstanceModule.h"


DEFINE_LOG_CATEGORY(LogCartograph)


bool Cartograph::IsBuildableIgnored(const TSoftClassPtr<AFGBuildable>& Type)
{
	CARTO_LOG_ERROR_DO_IF_NULL(UCartographGameInstanceModule::Instance, return true);

	return UCartographGameInstanceModule::Instance->BuildableToIgnore.Contains(Type);
};
