#include "CartographGameWorldModule.h"

#include "CartographGameInstanceModule.h"


void UCartographGameWorldModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    Super::DispatchLifecycleEvent(Phase);

    if (Phase != ELifecyclePhase::POST_INITIALIZATION)
    {
        return;
    }

    CARTO_LOG("UCartographGameWorldModule Init")

    CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);
    UCartographGameInstanceModule::Instance->OnWorldLoaded(GetWorld());
}
