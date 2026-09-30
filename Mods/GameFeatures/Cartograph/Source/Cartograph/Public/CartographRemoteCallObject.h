#pragma once

#include "FGRemoteCallObject.h"

#include "CoreMinimal.h"

#include "CartographGameInstanceModule.h"

#include "CartographRemoteCallObject.generated.h"


UENUM()
enum class EInitialDataSendPhase
{
	Initial,
	Normal,
	Finished
};


// We'll take up all the bandwidth if this value is not small enough,
// resulting the client not being able to do anything while initializing.
constexpr int BuildingDataBufferMaxSize = std::numeric_limits<uint16_t>::max() / 32;


USTRUCT()
struct FBuildingDataBuffer
{
	GENERATED_BODY()

    uint8 Data[BuildingDataBufferMaxSize];

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};


template<>
struct TStructOpsTypeTraits<FBuildingDataBuffer> : public TStructOpsTypeTraitsBase2<FBuildingDataBuffer>
{
	enum
	{
		WithNetSerializer = true
	};
};


/**
 * 
 */
UCLASS()
class CARTOGRAPH_API UCartographRemoteCallObject : public UFGRemoteCallObject
{
	GENERATED_BODY()

    friend class UCartographGameInstanceModule;

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// PlayerController is unused (each player has its own RCO), kept so the RPC stays compatible with 1.3.0.
	UFUNCTION(Server, Reliable)
	void ServerRequestInitialBuildingData(APlayerController* PlayerController, EInitialDataSendPhase SendPhase);
	void ServerRequestInitialBuildingData_Implementation(APlayerController* PlayerController, EInitialDataSendPhase SendPhase);

	UFUNCTION(Client, Reliable)
	void ClientReceiveInitialBuildingData(const FBuildingDataBuffer& Array, int16 Size, int16 TotalSliceCount);
	void ClientReceiveInitialBuildingData_Implementation(const FBuildingDataBuffer& Array, int16 Size, int16 TotalSliceCount);


	UE5Coro::TCoroutine<> InitialBuildableDeserialize(FForceLatentCoroutine = {});


protected:
	UPROPERTY(Replicated)
	bool bDummy = true;

	// Server side
	TArray<uint8> InitialBuildingDataToSend;
	int32 SliceCount = 0;
	int32 LastSentSlice = -1;

	// Client side
	int16 ReceivedSliceCount = 0;
    TArray<uint8> Buffer;
};
