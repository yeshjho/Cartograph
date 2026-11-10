#pragma once

#include "CoreMinimal.h"
#include "BuildingDataArray.h"
#include "CartographBuildingDataHolder.generated.h"


/**
 *
 */
UCLASS(Transient)
class ACartographBuildingDataHolder : public AInfo
{
	GENERATED_BODY()

	friend class ACartographBuildingDataSubsystem;

public:
	ACartographBuildingDataHolder();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


private:
	UPROPERTY(Replicated)
	FBuildingDataArray BuildingDataArray;
};
