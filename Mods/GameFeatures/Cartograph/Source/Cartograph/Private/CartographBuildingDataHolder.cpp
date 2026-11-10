#include "CartographBuildingDataHolder.h"

#include "Net/UnrealNetwork.h"

#include "CartographBuildingDataSubsystem.h"
#include "CartographHelpers.h"


ACartographBuildingDataHolder::ACartographBuildingDataHolder()
{
    PrimaryActorTick.bCanEverTick = false;

    bReplicates = true;
    bAlwaysRelevant = true;

    BuildingDataArray.Owner = ACartographBuildingDataSubsystem::Instance;
    BuildingDataArray.Items.Reserve(Cartograph::MAX_REPLICATED_ARRAY_LENGTH);
}


void ACartographBuildingDataHolder::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	AInfo::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ACartographBuildingDataHolder, BuildingDataArray);
}


void ACartographBuildingDataHolder::BeginPlay()
{
	Super::BeginPlay();

    CARTO_LOG_ERROR_RETURN_IF_NULL(ACartographBuildingDataSubsystem::Instance);

    if (!HasAuthority())
    {
        ACartographBuildingDataSubsystem::Instance->OnBuildingDataHolderAddedFromReplication(this);
    }
}


void ACartographBuildingDataHolder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

    CARTO_LOG_ERROR_RETURN_IF_NULL(ACartographBuildingDataSubsystem::Instance);

    if (!HasAuthority())
    {
        ACartographBuildingDataSubsystem::Instance->OnBuildingDataHolderRemovedFromReplication(this);
    }
}
