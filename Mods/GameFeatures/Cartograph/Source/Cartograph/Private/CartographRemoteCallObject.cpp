#include "CartographRemoteCallObject.h"

#include "Net/UnrealNetwork.h"

#include "Module/GameInstanceModuleManager.h"

#include "CartographGameInstanceModule.h"
#include "Cartograph_ConfigStruct.h"


// Ideally, the server would send all the data without getting the request after every slice.
// But I couldn't figure out how to "gradually" send the slices.
// I should poll for whether it's "safe"(not overflow the network buffer) to send the next slice, but seems like there's no easy way to do that.

bool FBuildingDataBuffer::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
    Ar.Serialize(Data, BuildingDataBufferMaxSize);
    bOutSuccess = !Ar.IsError();
    return bOutSuccess;
}


void UCartographRemoteCallObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UCartographRemoteCallObject, bDummy);
}


void UCartographRemoteCallObject::ServerRequestInitialBuildingData_Implementation(APlayerController* PlayerController, EInitialDataSendPhase SendPhase)
{
    constexpr float TimeOut = 10.f;

    switch (SendPhase)
    {
    case EInitialDataSendPhase::Initial:
    {
        CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);
        // FBufferWriter doesn't handle moving properly, don't TakeOwnership and free manually.
        // Another warning: FBufferWriter ignores FName
        FBufferWriter Archive{ nullptr, 0, EBufferWriterFlags::AllowResize };

        // This should NOT be set since it'll prevent big array from being serialized.
        // It's OK since we'll divide them and send gradually.
        //Archive.ArIsNetArchive = true;

        Archive << UCartographGameInstanceModule::Instance->CurrentBuildingData;
        CARTO_LOG("Started Sending Initial Data: %d", Archive.TotalSize());

        InitialBuildingDataToSendPerPlayer.Add(PlayerController, FInitialBuildingDataToSend{
	            .InitialBuildingData = std::move(Archive),
	            .Slices = Archive.TotalSize() / BuildingDataBufferMaxSize + 1,
	            .LastSentSlice = -1,
            });
        break;
    }

    case EInitialDataSendPhase::Normal:
        break;

    case EInitialDataSendPhase::Finished:
    {
        CARTO_LOG("Finished Sending Initial Data");
	    FInitialBuildingDataToSend& Data = *InitialBuildingDataToSendPerPlayer.Find(PlayerController);
        GetWorld()->GetTimerManager().ClearTimer(Data.TimerHandle);
        FMemory::Free(Data.InitialBuildingData.GetWriterData());
        InitialBuildingDataToSendPerPlayer.Remove(PlayerController);
        return;
    }
    }

    auto& [InitialBuildingData, Slices, LastSentSlice, TimerHandle] = *InitialBuildingDataToSendPerPlayer.Find(PlayerController);

    const int i = ++LastSentSlice;
    FBuildingDataBuffer SendBuffer;
    const int16 Size = FMath::Min(BuildingDataBufferMaxSize, InitialBuildingData.TotalSize() - i * BuildingDataBufferMaxSize);
    FMemory::Memcpy(SendBuffer.Data, static_cast<uint8*>(InitialBuildingData.GetWriterData()) + i * BuildingDataBufferMaxSize, Size);
    ClientReceiveInitialBuildingData(SendBuffer, Size, Slices);

    CARTO_LOG_DEBUG("Sending Initial Data (%d/%d)", i + 1, Slices);

    GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this, PlayerController]()
        {
            CARTO_LOG_WARNING("Initial Data Send Timed Out");

            FInitialBuildingDataToSend& Data = *InitialBuildingDataToSendPerPlayer.Find(PlayerController);
            FMemory::Free(Data.InitialBuildingData.GetWriterData());
            InitialBuildingDataToSendPerPlayer.Remove(PlayerController);
        }, TimeOut, false);
}


void UCartographRemoteCallObject::ClientReceiveInitialBuildingData_Implementation(const FBuildingDataBuffer& Array, int16 Size, int16 TotalSliceCount)
{
    CARTO_LOG_DEBUG("Received Initial Data");
    Buffer.Append(Array.Data, Size);

    CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);
    ReceivedSliceCount++;
    UCartographGameInstanceModule::Instance->InitializeProgress = static_cast<float>(ReceivedSliceCount) / TotalSliceCount;

    const bool IsLast = ReceivedSliceCount == TotalSliceCount;
    if (IsLast)
    {
        CARTO_LOG("Received Last Initial Data: %d.", Buffer.Num());
        InitialBuildableDeserialize();
    }

    ServerRequestInitialBuildingData(GetWorld()->GetFirstPlayerController(), 
        IsLast ? EInitialDataSendPhase::Finished : EInitialDataSendPhase::Normal);
}


UE5Coro::TCoroutine<> UCartographRemoteCallObject::InitialBuildableDeserialize(FForceLatentCoroutine)
{
    FMemoryReader Ar{ Buffer };
    Ar.ArIsNetArchive = true;

    // Will use redraw time budget since it's most likely to be in the middle of the game.
    const float TimeBudget = FCartograph_ConfigStruct::GetActiveConfig(GetWorld()).RedrawTimeBudget;
    UE5Coro::Latent::FTickTimeBudget Budget = UE5Coro::Latent::FTickTimeBudget::Milliseconds(TimeBudget);

    if (!UCartographGameInstanceModule::Instance)
    {
        CARTO_LOG_ERROR("UCartographGameInstanceModule::Instance is null");
        co_return;
    }

    auto& A = UCartographGameInstanceModule::Instance->CurrentBuildingData;

    /// Below is from `FArchive& TArrayPrivateFriend::Serialize(FArchive& Ar, TArray<ElementType, AllocatorType>& A)`
    A.CountBytes(Ar);

    int32 SerializeNum;
    Ar << SerializeNum;

    //A.Empty(SerializeNum);  // If SerializeNum is big, this can cause a lag spike.
    A.Empty();

    for (int32 i = 0; i < SerializeNum; i++)
    {
	    FBuildingData& NewElement = A.AddDefaulted_GetRef();
        Ar << NewElement;
        UCartographGameInstanceModule::Instance->OnInitialBuildingDataAdd(NewElement, i);
        co_await Budget;
    }
    /// End

    for (const auto& [ClassHash, Count] : UCartographGameInstanceModule::Instance->BuildingCountMap)
    {
        CARTO_LOG_DEBUG("Building: %u, Count: %d", ClassHash, Count);
    }

    CARTO_LOG("InitialBuildableDeserialize Finished, %d", SerializeNum);
    
    UCartographGameInstanceModule::Instance->IsInitializing = false;
    UCartographGameInstanceModule::Instance->OnZFilterUpdated(0, 1);
    UCartographGameInstanceModule::Instance->RedrawMap(true);
}
