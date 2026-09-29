#include "CartographGameInstanceModule.h"

#include <utility>

#include "AssetRegistry/AssetRegistryModule.h"
#include "CanvasItem.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Algo/FindLast.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Misc/OutputDeviceNull.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Compression/OodleDataCompressionUtil.h"
#include "GlobalRenderResources.h"
#include "HAL/FileManager.h"
#include "Hash/CityHash.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Tasks/Task.h"
#include "TextureResource.h"

#include "FGLightweightBuildableSubsystem.h"
#include "Buildables/FGBuildable.h"
#include "FGBuildableBeam.h"
#include "FGBuildableSubsystem.h"
#include "Buildables/FGBuildableWire.h"
#include "Resources/FGBuildingDescriptor.h"
#include "FGBuildCategory.h"
#include "FGBuildSubCategory.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSplineBuildableInterface.h"

#include "Configuration/Properties/ConfigPropertyString.h"
#include "ModLoading/ModLoadingLibrary.h"
#include "Patching/BlueprintHookManager.h"
#include "Patching/NativeHookManager.h"

#include "Util/CartographCanvasRenderItem.h"
#include "CartographModSubsystem.h"
#include "CartographRemoteCallObject.h"
#include "Cartograph_ConfigStruct.h"


#define LOCTEXT_NAMESPACE "Cartograph"


DEFINE_LOG_CATEGORY(LogCartograph);


// The vertices FCanvasTileItem makes, but rotated here instead of through the canvas transform,
// so that tiles with different rotations can share a batch
static void draw_tile(FBatchedElements& Batch, const FTexture* Texture, const FVector2D& Position, const FVector2D& Size, const FRotator& Rotation, const FLinearColor& Color)
{
	const FVector Pivot{ Position + Size / 2, 0 };
	const FMatrix Transform = FTranslationMatrix{ -Pivot } * FRotationMatrix{ Rotation } * FTranslationMatrix{ Pivot };
	const auto AddVertex = [&](double X, double Y, float U, float V)
		{
			const FVector P = Transform.TransformPosition(FVector{ X, Y, 0 });
			return Batch.AddVertexf(FVector4f{ float(P.X), float(P.Y), float(P.Z), 1 }, FVector2f{ U, V }, Color, FHitProxyId{});
		};

	const int32 V00 = AddVertex(Position.X, Position.Y, 0, 0);
	const int32 V10 = AddVertex(Position.X + Size.X, Position.Y, 1, 0);
	const int32 V01 = AddVertex(Position.X, Position.Y + Size.Y, 0, 1);
	const int32 V11 = AddVertex(Position.X + Size.X, Position.Y + Size.Y, 1, 1);
	Batch.AddTriangle(V00, V10, V11, Texture, SE_BLEND_AlphaBlend);
	Batch.AddTriangle(V00, V11, V01, Texture, SE_BLEND_AlphaBlend);
}


// The shape FBatchedElements gives a thick line (a square as wide as the line, swept from start to end, see its Draw),
// but made of triangles so it goes into the same batch as the tiles. Opaque, like FBatchedElements::AddLine makes every line.
template<IsFVector T, IsFVector U>
void draw_line(FBatchedElements& Batch, const T& WorldStart, const U& WorldEnd, FLinearColor Color, float Thickness)
{
	const FVector2D Start = world_position_to_screen_position(WorldStart, FVector::ZeroVector);
	const FVector2D End = world_position_to_screen_position(WorldEnd, FVector::ZeroVector);
	const double X = End.X >= Start.X ? Thickness / 2 : -Thickness / 2;
	const double Y = End.Y <= Start.Y ? Thickness / 2 : -Thickness / 2;
	Color.A = 1;
	const auto AddVertex = [&](const FVector2D& Point, double OffsetX, double OffsetY)
		{
			return Batch.AddVertexf(FVector4f{ float(Point.X + OffsetX), float(Point.Y + OffsetY), 0, 1 }, FVector2f::ZeroVector, Color, FHitProxyId{});
		};

	const int32 S0 = AddVertex(Start, X, Y);
	const int32 S2 = AddVertex(Start, -X, Y);
	const int32 S3 = AddVertex(Start, -X, -Y);
	const int32 E0 = AddVertex(End, X, Y);
	const int32 E1 = AddVertex(End, X, -Y);
	const int32 E3 = AddVertex(End, -X, -Y);
	Batch.AddTriangle(S2, S0, E0, GWhiteTexture, SE_BLEND_AlphaBlend);
	Batch.AddTriangle(S2, E1, E0, GWhiteTexture, SE_BLEND_AlphaBlend);
	Batch.AddTriangle(S2, E3, E1, GWhiteTexture, SE_BLEND_AlphaBlend);
	Batch.AddTriangle(S2, S3, E3, GWhiteTexture, SE_BLEND_AlphaBlend);
}


void UCartographGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	Super::DispatchLifecycleEvent(Phase);

	switch (Phase)
	{
	case ELifecyclePhase::CONSTRUCTION:
		return;

	case ELifecyclePhase::INITIALIZATION:
		RegisterMenuButton();
		return;

	case ELifecyclePhase::POST_INITIALIZATION:
		break;
	}

    CARTO_LOG("UCartographGameInstanceModule Init")

    GatherBuildables();
	GatherModOverrides();

	Instance = this;


	for (const auto& [Material, CategoryData] : MaterialBuildCategoryDataOverrideMap)
	{
		for (const auto& [_, Recipe] : GetMutableDefault<UFGFactoryCustomizationDescriptor_Material>(Material)->GetBuildableMap())
		{
			TSubclassOf<AFGBuildable> Buildable = GetDefault<UFGBuildingDescriptor>(UFGRecipe::GetDescriptorForRecipe(Recipe))->mBuildableClass;
			if (!BuildableBuildCategoryDataOverrideMap.Contains(Buildable.Get()))
			{
				BuildableBuildCategoryDataOverrideMap.Add(Buildable.Get(), CategoryData);
			}
		}
	}

	for (const auto& [Material, LayerData] : MaterialBuildLayerDataOverrideMap)
	{
		for (const auto& [_, Recipe] : GetMutableDefault<UFGFactoryCustomizationDescriptor_Material>(Material)->GetBuildableMap())
		{
			TSubclassOf<AFGBuildable> Buildable = GetDefault<UFGBuildingDescriptor>(UFGRecipe::GetDescriptorForRecipe(Recipe))->mBuildableClass;
			if (!BuildableBuildLayerDataOverrideMap.Contains(Buildable.Get()))
			{
				BuildableBuildLayerDataOverrideMap.Add(Buildable.Get(), LayerData);
			}
		}
	}

	LoadRuntimeConfig();

	FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UCartographGameInstanceModule::OnWorldTearDown);


#pragma region Hooking
	const auto ShouldSkipChange = [this] { return ShouldInitialize || IsClient || !GIsRunning; };

	// Queues a building added/removed after initialization for the next redraw.
	// ExtraDataSource is either the AFGBuildable* or the lightweight TypeSpecificData.
	const auto QueueBuildingChange = [this, ShouldSkipChange](UClass* BuildableClass, const FTransform& Transform, const auto& ExtraDataSource, bool bRemoved)
		{
			if (ShouldSkipChange() || BuildableToIgnore.Contains(BuildableClass))
			{
				return;
			}

			CARTO_LOG_VERBOSE("%s: %s", bRemoved ? TEXT("Removed") : TEXT("Added"), *BuildableClass->GetName());

			FBuildingData Data{ .Transform = Transform };
			Data.AddExtraData(ExtraDataSource);
			if (bRemoved)
			{
				Data.FillInHash(BuildableClass);  // Cache isn't used in comparison (==, <=>), so no need to fill it
				PendingRemoveBuildingData.Add(std::move(Data));
			}
			else
			{
				Data.FillInHashAndCache(BuildableClass);
				PendingAddBuildingData.Add(std::move(Data));
			}

			RedrawMap(false);
		};

	const auto LambdaAfterAddFromBuildableInstanceData =
        [QueueBuildingChange](int32 ReturnValue, AFGLightweightBuildableSubsystem* ClassInstance, TSubclassOf<AFGBuildable> BuildableClass,
            FRuntimeBuildableInstanceData& BuildableInstanceData, bool FromSaveData = false, int32 SaveDataBuildableIndex = INDEX_NONE,
            uint16 ConstructId = MAX_uint16, AActor* BuildEffectInstigator = nullptr, int32 BlueprintBuildEffectIndex = INDEX_NONE)
        {
			if (!FromSaveData)
			{
				QueueBuildingChange(BuildableClass.Get(), BuildableInstanceData.Transform, BuildableInstanceData.TypeSpecificData, false);
			}
        };

	const auto LambdaAfterAddFromReplicatedData =
		[QueueBuildingChange](AFGLightweightBuildableSubsystem* ClassInstance, TSubclassOf<AFGBuildable> BuildableClass, TSubclassOf<UFGRecipe> BuiltWithRecipe,
			const FLightweightBuildableReplicationItem& ReplicationData, int32 MaxSize,
			AActor* BuildEffectInstigator, int32 BlueprintBuildIndex)
		{
			QueueBuildingChange(BuildableClass.Get(), ReplicationData.Transform, ReplicationData.TypeSpecificData, false);
		};

	const auto LambdaAfterAddBuildable =
		[QueueBuildingChange](AFGBuildableSubsystem* ClassInstance, AFGBuildable* Buildable)
		{
			QueueBuildingChange(Buildable->GetClass(), Buildable->GetTransform(), Buildable, false);
		};

	const auto LambdaAfterInvalidateRuntimeInstanceDataForIndex =
		[QueueBuildingChange, ShouldSkipChange](AFGLightweightBuildableSubsystem* ClassInstance, TSubclassOf<AFGBuildable> BuildableClass, int32 Index)
		{
			if (ShouldSkipChange())
			{
				return;
			}
			if (const FRuntimeBuildableInstanceData* LightweightData = ClassInstance->GetRuntimeDataForBuildableClassAndIndex(BuildableClass, Index))
			{
				QueueBuildingChange(BuildableClass.Get(), LightweightData->Transform, LightweightData->TypeSpecificData, true);
			}
		};

	const auto LambdaAfterRemoveBuildable =
		[QueueBuildingChange](AFGBuildableSubsystem* ClassInstance, AFGBuildable* Buildable)
		{
			QueueBuildingChange(Buildable->GetClass(), Buildable->GetTransform(), Buildable, true);
		};


	const auto LambdaAfterCloseRespawnUI =
        [this](AFGHUD* ClassInstance)
        {
            if (!ShouldInitialize || !GIsRunning)
            {
				return;
            }

            CARTO_LOG("CloseRespawnUI");

	        AFGPlayerController* PlayerController = Cast<AFGPlayerController>(GetWorld()->GetFirstPlayerController());
			if (PlayerController->HasAuthority())
			{
				return;
			}

			auto* RCO = PlayerController->GetRemoteCallObjectOfClass<UCartographRemoteCallObject>();
			if (RCO)
			{
				ShouldInitialize = false;
				IsInitializing = true;
				ResetBuildingData();
				RCO->ReceivedSliceCount = 0;
				RCO->Buffer.Empty();
				RCO->ServerRequestInitialBuildingData(PlayerController, EInitialDataSendPhase::Initial);
			}
			else
			{
                CARTO_LOG_ERROR("Failed to get RemoteCallObject");
			}
        };


	if (!WITH_EDITOR)
	{
		// Doing it after PlayerController::BeginPlay would interfere other network packets,
	    // resulting higher chance of packet loss (due to timeout)
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGHUD, CloseRespawnUI, LambdaAfterCloseRespawnUI);


		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, AddFromBuildableInstanceData, LambdaAfterAddFromBuildableInstanceData);
	    SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, AddFromReplicatedData, LambdaAfterAddFromReplicatedData);

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableSubsystem, AddBuildable, LambdaAfterAddBuildable);


		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, InvalidateRuntimeInstanceDataForIndex, LambdaAfterInvalidateRuntimeInstanceDataForIndex);

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableSubsystem, RemoveBuildable, LambdaAfterRemoveBuildable);


		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGSaveSession, SaveGame,
			[this](UFGSaveSession* ClassInstance, const FString& FileName, FOnSaveGameComplete CompleteDelegate, void* UserData)
			{
				SaveMapCache();
			});


		SUBSCRIBE_METHOD(FCanvas::GetBatchedElements,
			[](auto& Scope, FCanvas* ClassInstance,
				FCanvas::EElementType InElementType, FBatchedElementParameters* InBatchedElementParameters, const FTexture* InTexture, ESimpleElementBlendMode InBlendMode, const FDepthFieldGlowInfo& GlowInfo, bool bApplyDPIScale)
			{
				// Only Cartograph's map canvas needs the custom render item (for the redraw scissor).
				// Every other canvas must keep the engine implementation, otherwise its render data leaks VRAM.
				if (!Instance || ClassInstance != Instance->CurrentCanvas)
				{
					Scope(ClassInstance, InElementType, InBatchedElementParameters, InTexture, InBlendMode, GlowInfo, bApplyDPIScale);
					return;
				}

				// get sort element based on the current sort key from top of sort key stack
				FCanvas::FCanvasSortElement& SortElement = ClassInstance->GetSortElement(ClassInstance->TopDepthSortKey());
				// find a batch to use 
				FCartographCanvasRenderItem* RenderBatch = nullptr;
				// get the current transform entry from top of transform stack
				FCanvas::FTransformEntry FinalTransform = ClassInstance->GetTransformStack().Top();

				if (!bApplyDPIScale && ClassInstance->GetDPIScale() != 1.0f)
				{
					FinalTransform = FCanvas::FTransformEntry(FScaleMatrix(1 / ClassInstance->GetDPIScale()) * FinalTransform.GetMatrix());
				}

				// try to use the current top entry in the render batch array
				if (SortElement.RenderBatchArray.Num() > 0)
				{
					checkSlow(SortElement.RenderBatchArray.Last());
					RenderBatch = static_cast<FCartographCanvasRenderItem*>(SortElement.RenderBatchArray.Last());
				}

				// if a matching entry for this batch doesn't exist then allocate a new entry
				if (RenderBatch == nullptr ||
					!RenderBatch->IsMatch(InBatchedElementParameters, InTexture, InBlendMode, InElementType, FinalTransform, GlowInfo))
				{
					RenderBatch = new FCartographCanvasRenderItem(InBatchedElementParameters, InTexture, InBlendMode, InElementType, FinalTransform, GlowInfo);
					SortElement.RenderBatchArray.Add(RenderBatch);
				}

				Scope.Override(RenderBatch->GetBatchedElements());
			});
	}
#pragma endregion
}


void UCartographGameInstanceModule::OnWorldLoaded(UWorld* World)
{
	CARTO_LOG("OnWorldLoaded");

	// ShouldInitialize is set by ACartographModSubsystem, here it's too late: the buildables are already registered.
	IsClient = GetWorld()->IsNetMode(NM_Client);

	if (!IsClient)
	{
		// Wait for ACartographModSubsystem to initialize
		GetWorld()->GetTimerManager().SetTimerForNextTick(
			[this, World]()
			{
				if (!ShouldInitialize || !GIsRunning)
				{
					return;
				}

				ShouldInitialize = false;
				IsInitializing = true;

				TArray<TWeakObjectPtr<AFGBuildable>> Factories;
				Algo::Transform(AFGBuildableSubsystem::Get(World)->GetAllBuildablesRef(), Factories,
					[](AFGBuildable* Buildable) { return Buildable; });
				Coroutine = InitialBuildableGather(
					std::move(Factories),
					AFGLightweightBuildableSubsystem::Get(World)->mBuildableClassToInstanceArray
				);
			});
	}

	if (!FPlatformProperties::IsServerOnly())
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, { 0, 0, 0, 0 });
	}
}


// Nothing may carry over to the next world: a redraw still going on would work on the data gathered there
void UCartographGameInstanceModule::OnWorldTearDown(UWorld* World)
{
	if (World != GetWorld())
	{
		return;
	}

	CARTO_LOG("OnWorldTearDown");

	ShouldInitialize = true;  // Also ignores the buildings removed while tearing down
	IsPendingRedraw = false;
	IsPendingRedrawEntire = false;
	PendingAddBuildingData.Empty();
	PendingRemoveBuildingData.Empty();
	IsRedrawingEntirely = false;
	RedrawArea = FBox2D{ ForceInit };
	Coroutine.Cancel();
}


void UCartographGameInstanceModule::OnLayerConfigChanged()
{
	CARTO_LOG_DEBUG("OnLayerConfigChanged");

	RedrawMap(true);
	SaveRuntimeConfig();
}


const FBuildLayerData* UCartographGameInstanceModule::GetBuildLayerData(uint32 ClassHash)
{
	FillBuildLayerDataCache();

    if (const auto* DataCache = BuildLayerDataMapCache.Find(ClassHash))
    {
        return *DataCache;
    }
	return nullptr;
}


bool UCartographGameInstanceModule::DoesBuildingExist(uint32 ClassHash) const
{
	return BuildingCountMap.FindRef(ClassHash) > 0;
}


TSubclassOf<AFGBuildable> UCartographGameInstanceModule::ResolveBuildableClass(UClass* BuildableClass) const
{
	const TSoftClassPtr<AFGBuildable>* RedirectClass = BuildableClassRedirectMap.Find(BuildableClass);
	return RedirectClass ? RedirectClass->LoadSynchronous() : BuildableClass;
}


#pragma region Drawing
void UCartographGameInstanceModule::RedrawMap(bool bRedrawEntirely)
{
	IsMapCacheOutdated = true;

	if (!Coroutine.IsDone())
	{
		// An entire redraw isn't restarted for a building change, the change gets drawn right after it instead.
		// Otherwise building something every few seconds would keep the map from ever finishing.
		if (!IsInitializing && (bRedrawEntirely || !IsRedrawingEntirely))
		{
			CARTO_LOG_DEBUG("RedrawMapCoroutine Cancel Requested");
			Coroutine.Cancel();
		}
		// Don't let a later partial redraw request drop a pending entire one
        IsPendingRedrawEntire |= bRedrawEntirely;
		IsPendingRedraw = true;
	}
	else if (!IsClient || !IsInitializing)
	{
		ExecuteRedrawMapCoroutine(bRedrawEntirely);
	}
}


// Factories/Buildings: Intentional copies
UE5Coro::TCoroutine<> UCartographGameInstanceModule::InitialBuildableGather(
	TArray<TWeakObjectPtr<AFGBuildable>> Factories, TMap<TSubclassOf<AFGBuildable>, TArray<FRuntimeBuildableInstanceData>> Buildings, FForceLatentCoroutine)
{
	int BuildingCount = 0;
	for (const auto& [Type, Arr] : Buildings)
	{
        BuildingCount += Arr.Num();
	}

    CARTO_LOG("InitialBuildableGather Started. Factories: %d, Buildings: %d", Factories.Num(), BuildingCount);

	const int Total = Factories.Num() + BuildingCount;
	ResetBuildingData();
	CurrentBuildingData.Reserve(Total);

	const float TimeBudget = FCartograph_ConfigStruct::GetActiveConfig(GetWorld()).InitializeTimeBudget;
	UE5Coro::Latent::FTickTimeBudget Budget = UE5Coro::Latent::FTickTimeBudget::Milliseconds(TimeBudget);

	int Processed = 0;

	for (const TWeakObjectPtr<AFGBuildable>& Factory : Factories)
	{
		if (!Factory.IsValid() || BuildableToIgnore.Contains(Factory->GetClass()))
		{
			continue;
		}
		
		FBuildingData NewBuildingData{ .Transform = Factory->GetTransform() };
        NewBuildingData.AddExtraData(Factory.Get());
		NewBuildingData.FillInHashAndCache(Factory->GetClass());

		CurrentBuildingData.Add(std::move(NewBuildingData));

        InitializeProgress = static_cast<float>(++Processed) / Total;
		co_await Budget;
	}

	for (const auto& [Type, Arr] : Buildings)
	{
        if (BuildableToIgnore.Contains(Type.Get()))
        {
            continue;
        }

		for (const FRuntimeBuildableInstanceData& InstanceData : Arr)
		{
			FBuildingData NewBuildingData{ .Transform = InstanceData.Transform };
            NewBuildingData.AddExtraData(InstanceData.TypeSpecificData);
			NewBuildingData.FillInHashAndCache(Type);

			CurrentBuildingData.Add(std::move(NewBuildingData));

			InitializeProgress = static_cast<float>(++Processed) / Total;
			co_await Budget;
		}
	}

	Algo::Sort(CurrentBuildingData);
	const int Size = CurrentBuildingData.Num();
	for (int i = 0; i < Size; i++)
	{
		OnBuildingDataAdd(CurrentBuildingData[i], i);
		co_await Budget;
	}

	OnZFilterUpdated(0, 1);

	// With an image saved for this same map there's nothing to draw
	const bool bMapCacheUsed = LoadMapCache();
	IsMapCacheOutdated = !bMapCacheUsed;

	CARTO_LOG("InitialBuildableGather Finished. Map cache used: %d", bMapCacheUsed);

	for (const auto& [ClassHash, Count] : BuildingCountMap)
	{
        CARTO_LOG_DEBUG("Building: %u, Count: %d", ClassHash, Count);
	}

	IsInitializing = false;
	IsPendingRedraw = false;
	const bool bRedrawEntirely = std::exchange(IsPendingRedrawEntire, false) || !bMapCacheUsed;
	if (bRedrawEntirely || !PendingAddBuildingData.IsEmpty() || !PendingRemoveBuildingData.IsEmpty())
	{
		ExecuteRedrawMapCoroutine(bRedrawEntirely);
	}
}


// AddedBuildings/RemovedBuildings: Intentional copies
UE5Coro::TCoroutine<> UCartographGameInstanceModule::RedrawMapCoroutine(
	TArray<FBuildingData> AddedBuildings, TArray<FBuildingData> RemovedBuildings, bool bRedrawEntirely, FForceLatentCoroutine)
{
	ON_SCOPE_EXIT
	{
        OnCoroutineFinishedOrCancelled();
	};

	CARTO_LOG_DEBUG("RedrawMapCoroutine Started. Entire: %d", bRedrawEntirely);
	const double StartTime = FPlatformTime::Seconds();

	const float TimeBudget = FCartograph_ConfigStruct::GetActiveConfig(GetWorld()).RedrawTimeBudget;
	UE5Coro::Latent::FTickTimeBudget Budget = UE5Coro::Latent::FTickTimeBudget::Milliseconds(TimeBudget);

    IsRedrawingEntirely |= bRedrawEntirely;

	// Not on scope exit: a cancelled redraw has to hand its area over to the next one
	const auto ResetRedrawArea = [this]
		{
			IsRedrawingEntirely = false;
			RedrawArea = FBox2D{ ForceInit };
		};

	{
        UE5Coro::FCancellationGuard Guard{};  // We'll lose added/removed building information if the coroutine is cancelled

		for (FBuildingData& AddedBuildingData : AddedBuildings)
		{
	        CARTO_LOG_DEBUG("AddedBuilding: %u", AddedBuildingData.BuildableClassHash);

			if (!IsRedrawingEntirely && AddedBuildingData.VisualBoxCache.bIsValid)
			{
				RedrawArea += AddedBuildingData.VisualBoxCache;
			}

			const int32 Pos = Algo::LowerBound(CurrentBuildingData, AddedBuildingData);
			OnBuildingDataAdd(AddedBuildingData, Pos);
			CurrentBuildingData.Insert(std::move(AddedBuildingData), Pos);

			co_await Budget;
		}

	    for (const FBuildingData& RemovedBuildingData : RemovedBuildings)
	    {
	        CARTO_LOG_DEBUG("RemovedBuilding: %u", RemovedBuildingData.BuildableClassHash);

	        const int32 Start = Algo::LowerBound(CurrentBuildingData, RemovedBuildingData);
            const int32 End = CurrentBuildingData.Num();

	        for (int32 i = Start; i < End; ++i)
	        {
	            if (RemovedBuildingData == CurrentBuildingData[i])
	            {
					if (!IsRedrawingEntirely && CurrentBuildingData[i].VisualBoxCache.bIsValid)
					{
						RedrawArea += CurrentBuildingData[i].VisualBoxCache;
					}

					OnBuildingDataRemove(CurrentBuildingData[i], i);
                    CurrentBuildingData.RemoveAt(i);
	                break;
	            }
                if (RemovedBuildingData < CurrentBuildingData[i])  // Sorted by Z, we're past it
                {
                    CARTO_LOG_ERROR("Can't find removed building data");
                    break;
                }
	        }

	        co_await Budget;
	    }

		UpdateZFilter();

		CARTO_LOG_DEBUG("Buildings Change Processed");
	}

	if (FPlatformProperties::IsServerOnly())
	{
		co_return;
	}

	// Sometimes lines go crazy (goes to the top or far right) if we don't delay.
	// My guess is because EndDraw and BeginDraw are called in the same frame, so I'm putting it here.
	co_await UE5Coro::Latent::NextTick();

	UCanvas* Canvas = nullptr;
	FVector2D _;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, _, RenderContext);
	CurrentCanvas = Canvas->Canvas;

	if (IsRedrawingEntirely)
	{
		ScissorArea = { 0, 0, RENDER_TEXTURE_SIZE, RENDER_TEXTURE_SIZE };
	}
	else
	{
		const FVector2D MinScreenPosition = world_position_to_screen_position(RedrawArea.Min, FVector::ZeroVector);
        const FVector2D MaxScreenPosition = world_position_to_screen_position(RedrawArea.Max, FVector::ZeroVector);
		const auto MinIntX = static_cast<uint32>(FMath::Max(0, FMath::FloorToInt(MinScreenPosition.X)));
        const auto MinIntY = static_cast<uint32>(FMath::Max(0, FMath::FloorToInt(MinScreenPosition.Y)));
        const auto MaxIntX = static_cast<uint32>(FMath::Min(RENDER_TEXTURE_SIZE, FMath::CeilToInt(MaxScreenPosition.X)));
        const auto MaxIntY = static_cast<uint32>(FMath::Min(RENDER_TEXTURE_SIZE, FMath::CeilToInt(MaxScreenPosition.Y)));
        ScissorArea = { MinIntX, MinIntY, MaxIntX, MaxIntY };
        RedrawArea = {
        	screen_position_to_world_position(FVector2D{ static_cast<double>(MinIntX), static_cast<double>(MinIntY) }),
			screen_position_to_world_position(FVector2D{ static_cast<double>(MaxIntX), static_cast<double>(MaxIntY) })
        };
		CARTO_LOG_DEBUG("RedrawArea: %s", *RedrawArea.ToString());
	}

	FCanvasTileItem ClearItem{
		{ 0, 0 },
		{ RENDER_TEXTURE_SIZE, RENDER_TEXTURE_SIZE },
		{ 0, 0, 0, 0 }
	};
	ClearItem.BlendMode = SE_BLEND_Opaque;

	Canvas->DrawItem(ClearItem);


    const int32 Min = Algo::LowerBound(CurrentBuildingData, MinZFilter);
    const int32 Max = Algo::UpperBound(CurrentBuildingData, MaxZFilter);
    if (Min >= CurrentBuildingData.Num() || Max <= 0)
    {
		ResetRedrawArea();
        co_return;
    }

	CARTO_LOG_DEBUG("From %d to %d out of %d", Min, Max, CurrentBuildingData.Num());

	TArray<int32> BuildingsToDraw;
	if (IsRedrawingEntirely)
	{
        BuildingsToDraw.Reserve(Max - Min);
        for (int32 i = Min; i < Max; i++)
        {
			BuildingsToDraw.Add(i);
        }
	}
	else
	{
		CurrentBuildingQuadTree.GetElements(RedrawArea, BuildingsToDraw);
		co_await Budget;

        for (int32& Index : BuildingsToDraw)
        {
            Index = BuildingDataIndexRedirector[Index];
        }
		Algo::Sort(BuildingsToDraw);
        co_await Budget;

		const int* MinIt = Algo::FindByPredicate(BuildingsToDraw, [Min](int32 Index) { return Index >= Min; });
		const int* MaxIt = Algo::FindLastByPredicate(BuildingsToDraw, [Max](int32 Index) { return Index < Max; });
        if (!MinIt || !MaxIt)
        {
			ResetRedrawArea();
            co_return;
        }

		const int* Beg = BuildingsToDraw.GetData();
        BuildingsToDraw.RemoveAt(MaxIt - Beg + 1, BuildingsToDraw.Num() - (MaxIt - Beg + 1), EAllowShrinking::No);
        BuildingsToDraw.RemoveAt(0, MinIt - Beg);
		co_await Budget;

		CARTO_LOG_DEBUG("Overlapping Elements: %d", BuildingsToDraw.Num());
	}

	// The canvas flushes after every DrawItem, so drawing item by item sent every tile and line to the GPU on its own,
	// which took about a minute for the entire map on a big save. Tiles and lines are all triangles here, blended the same,
	// so what a tick draws goes out in one pass. Only a texture change starts a new batch, that keeps the order by height.
	const auto GetBatch = [this](const FTexture* Texture) -> FBatchedElements&
		{
			return *CurrentCanvas->GetBatchedElements(FCanvas::ET_Triangle, nullptr, Texture, SE_BLEND_AlphaBlend);
		};

	for (int32 i : BuildingsToDraw)
	{
        const auto& [ClassHash, Transform, BuildableExtraData,
			DataType, DataCache, LayerDataCache, VisualBoxCache] = CurrentBuildingData[i];

		CARTO_LOG_VERY_VERBOSE("%d | Buildable: %u, Transform: %s", i, ClassHash, *Transform.ToString());

		if (RuntimeConfig.DisabledLayerBuildable.Contains(ClassHash))
		{
            continue;
		}
		if (LayerDataCache)
		{
			if (RuntimeConfig.DisabledLayerMainCategory.Contains(LayerDataCache->MainCategoryCache))
			{
				continue;
			}
			if (const TSet<FName>* SubCategories = RuntimeConfig.DisabledLayerSubCategory.Find(LayerDataCache->MainCategoryCache))
			{
				if (SubCategories->Contains(LayerDataCache->SubCategoryCache))
				{
					continue;
				}
			}
		}

		switch (DataType)
		{
		case EBuildingDataType::Invalid:
			break;

		case EBuildingDataType::Icon:
		{
			const FNormalDataCache* NormalDataCachePtr = std::get_if<FNormalDataCache>(&DataCache);
			CARTO_LOG_ERROR_BREAK_IF_NULL(NormalDataCachePtr);

			const auto& [ScreenPosition, Size, Rotation, IconOrRectangleData] = *NormalDataCachePtr;
            const TSoftObjectPtr<UTexture2D>* Texture = std::get_if<TSoftObjectPtr<UTexture2D>>(&IconOrRectangleData);
            CARTO_LOG_ERROR_BREAK_IF_NULL(Texture);

			// The texture might have gotten unloaded between redraws, so we can't cache it.
			const UTexture2D* LoadedTexture = Texture->Get();
			if (!LoadedTexture)
			{
				CurrentCanvas->Flush_GameThread();  // What's batched can't wait for the load, its textures aren't kept loaded
				LoadedTexture = co_await UE5Coro::Latent::AsyncLoadObject(*Texture);
			}
			CARTO_LOG_ERROR_BREAK_IF_NULL(LoadedTexture);
			const FTexture* Resource = LoadedTexture->GetResource();
			CARTO_LOG_ERROR_BREAK_IF_NULL(Resource);

			draw_tile(GetBatch(Resource), Resource, ScreenPosition, { Size.X * PIXEL_PER_CENTIMETER[0], Size.Y * PIXEL_PER_CENTIMETER[1] }, Rotation, FLinearColor::White);

			break;
		}

		case EBuildingDataType::Rectangle:
		{
			const FNormalDataCache* NormalDataCachePtr = std::get_if<FNormalDataCache>(&DataCache);
            CARTO_LOG_ERROR_BREAK_IF_NULL(NormalDataCachePtr);

			const auto& [ScreenPosition, Size, Rotation, IconOrRectangleData] = *NormalDataCachePtr;
			const FRectangleDataCache* RectangleDataCachePtr = std::get_if<FRectangleDataCache>(&IconOrRectangleData);
			CARTO_LOG_ERROR_BREAK_IF_NULL(RectangleDataCachePtr);

			const auto& [CategoryData, Corners] = *RectangleDataCachePtr;
			CARTO_LOG_ERROR_BREAK_IF_NULL(CategoryData);

			FBatchedElements& Batch = GetBatch(GWhiteTexture);
			draw_tile(Batch, GWhiteTexture, ScreenPosition, { Size.X * PIXEL_PER_CENTIMETER[0], Size.Y * PIXEL_PER_CENTIMETER[1] }, Rotation, CategoryData->MainColor);

			if (CategoryData->OutlineThickness > 0)
			{
				for (int32 k = 0; k < 4; k++)
				{
					draw_line(Batch, Corners[k], Corners[(k + 1) % 4], CategoryData->OutlineColor, CategoryData->OutlineThickness);
				}
			}

			break;
		}

		case EBuildingDataType::Spline:
		{
            const FSplineDataCache* SplineDataCachePtr = std::get_if<FSplineDataCache>(&DataCache);
            CARTO_LOG_ERROR_BREAK_IF_NULL(SplineDataCachePtr);

			const FSplineExtraData* SplineExtraData = std::get_if<FSplineExtraData>(&BuildableExtraData);
            CARTO_LOG_ERROR_BREAK_IF_NULL(SplineExtraData);

            FBatchedElements& Batch = GetBatch(GWhiteTexture);
            const int Num = SplineExtraData->Points.Num();
            for (int j = 0; j < Num - 1; j++)
            {
                draw_line(Batch, SplineExtraData->Points[j], SplineExtraData->Points[j + 1],
					SplineDataCachePtr->SplineData->Color, SplineDataCachePtr->SplineData->Thickness);
            }

			break;
		}

		case EBuildingDataType::Wire:
		{
			const FWireExtraData* WireExtraData = std::get_if<FWireExtraData>(&BuildableExtraData);
			CARTO_LOG_ERROR_BREAK_IF_NULL(WireExtraData);

			const FWireData* const* WireDataPtr = std::get_if<const FWireData*>(&DataCache);
			CARTO_LOG_ERROR_BREAK_IF_NULL(WireDataPtr);
            const FWireData* WireData = *WireDataPtr;

			draw_line(GetBatch(GWhiteTexture), Transform.GetLocation(), WireExtraData->End, WireData->Color, WireData->Thickness);

			break;
		}

        case EBuildingDataType::Beam:
		{
			const FBeamExtraData* BeamExtraData = std::get_if<FBeamExtraData>(&BuildableExtraData);
			CARTO_LOG_ERROR_BREAK_IF_NULL(BeamExtraData);

			const FWireData* const* BeamDataPtr = std::get_if<const FWireData*>(&DataCache);
            CARTO_LOG_ERROR_BREAK_IF_NULL(BeamDataPtr);
            const FWireData* BeamData = *BeamDataPtr;

			const float Length = BeamExtraData->Length;
			const FVector Start = Transform.GetLocation();
			const FVector End = Start + Transform.GetRotation().Vector() * Length;
			draw_line(GetBatch(GWhiteTexture), Start, End, BeamData->Color, BeamData->Thickness);

            break;
		}

		default:
			break;
		}

		if constexpr (DRAW_BOUNDARIES)
		{
            constexpr FLinearColor Color{ 1, 0, 1, 1 };
            constexpr float Thickness = 2;

			const FVector2D MinPoint = VisualBoxCache.Min;
            const FVector2D MaxPoint = VisualBoxCache.Max;
			FBatchedElements& Batch = GetBatch(GWhiteTexture);
            draw_line(Batch, FVector2D{ MinPoint.X, MinPoint.Y }, FVector2D{ MaxPoint.X, MinPoint.Y }, Color, Thickness);
			draw_line(Batch, FVector2D{ MaxPoint.X, MinPoint.Y }, FVector2D{ MaxPoint.X, MaxPoint.Y }, Color, Thickness);
			draw_line(Batch, FVector2D{ MaxPoint.X, MaxPoint.Y }, FVector2D{ MinPoint.X, MaxPoint.Y }, Color, Thickness);
			draw_line(Batch, FVector2D{ MinPoint.X, MaxPoint.Y }, FVector2D{ MinPoint.X, MinPoint.Y }, Color, Thickness);
		}

		if (!Budget.await_ready())
		{
			CurrentCanvas->Flush_GameThread();
			co_await Budget;
		}
	}

	if (IsRedrawingEntirely)
	{
		CARTO_LOG("Map drawn in %.2f s, %d buildings", FPlatformTime::Seconds() - StartTime, BuildingsToDraw.Num());
	}

	ResetRedrawArea();

	CARTO_LOG_DEBUG("RedrawMapCoroutine Finished");
}


void UCartographGameInstanceModule::OnCoroutineFinishedOrCancelled()
{
    CARTO_LOG_DEBUG("OnCoroutineFinishedOrCancelled");

    if (RenderContext.RenderTarget)
    {
        UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, RenderContext);
		RenderContext = {};
		CurrentCanvas = nullptr;
    }

	if (!IsPendingRedraw)
	{
		return;
	}

	// The coroutine is also on the game thread, so I think no data race here.
    IsPendingRedraw = false;
	ExecuteRedrawMapCoroutine(std::exchange(IsPendingRedrawEntire, false));
}


void UCartographGameInstanceModule::ExecuteRedrawMapCoroutine(bool bRedrawEntirely)
{
	Coroutine = RedrawMapCoroutine(PendingAddBuildingData, PendingRemoveBuildingData, bRedrawEntirely);
	if (!IsClient)
	{
		if (!ACartographModSubsystem::Instance)
		{
            CARTO_LOG_ERROR("ACartographModSubsystem is not initialized");
		}
		else
		{
			ACartographModSubsystem::Instance->ClientUpdateBuildingData(PendingAddBuildingData, PendingRemoveBuildingData);
		}
	}
	PendingAddBuildingData.Empty();
	PendingRemoveBuildingData.Empty();
}


void UCartographGameInstanceModule::OnZFilterUpdated(float Min, float Max)
{
	MinCached = Min;
    MaxCached = Max;
	UpdateZFilter();

	CARTO_LOG("Min is now %f and max is now %f", MinZFilter, MaxZFilter);

	if (!IsInitializing)
	{
		RedrawMap(true);
	}
}


void UCartographGameInstanceModule::UpdateZFilter()
{
	// CurrentBuildingData is sorted by Z
	MinHeight = !CurrentBuildingData.IsEmpty() ? CurrentBuildingData[0].Transform.GetLocation().Z : -100;
	MaxHeight = !CurrentBuildingData.IsEmpty() ? CurrentBuildingData.Last().Transform.GetLocation().Z : 100;

	const float Length = MaxHeight - MinHeight;
	MinZFilter = FMath::Floor(MinCached * Length + MinHeight);
	MaxZFilter = FMath::CeilToInt(MaxCached * Length + MinHeight);
}


void UCartographGameInstanceModule::ResetBuildingData()
{
	CurrentBuildingData.Empty();
	BuildingDataIndexRedirector.Empty();
	CurrentBuildingQuadTree.Empty();
	BuildingCountMap.Empty();
}
#pragma endregion


#pragma region Map Cache
constexpr int32 MAP_CACHE_COUNT = 10;


static FString GetMapCacheDir()
{
	return FPaths::ProjectSavedDir() / TEXT("Cartograph");
}


// Named after what is drawn and how, so an image only ever gets loaded for the same map
FString UCartographGameInstanceModule::GetMapCachePath() const
{
	return GetMapCacheDir() / FString::Printf(TEXT("%016llx.mapcache"), HashBuildingData() + HashDrawSettings());
}


// Called on every save, so loading that save can show the map right away instead of drawing it again.
void UCartographGameInstanceModule::SaveMapCache()
{
	if (IsClient || FPlatformProperties::IsServerOnly() || RenderTarget->GetFormat() != PF_B8G8R8A8)
	{
		return;
	}

	if (!IsMapCacheOutdated)
	{
		CARTO_LOG("Map cache is up to date");
		return;
	}

	// The image has to show exactly CurrentBuildingData, and unfiltered since the Z filter is reset on load
	if (ShouldInitialize || IsInitializing || !Coroutine.IsDone() || !PendingAddBuildingData.IsEmpty() || !PendingRemoveBuildingData.IsEmpty()
		|| MinCached != 0 || MaxCached != 1)
	{
		CARTO_LOG("Map cache not saved, the map is being updated or filtered");
		return;
	}

	// This waits for the GPU, but saving the game hitches anyway
	TArray<FColor> Pixels;
	if (!RenderTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))
	{
		CARTO_LOG_ERROR("Failed to read the map");
		return;
	}
	IsMapCacheOutdated = false;

	UE::Tasks::Launch(UE_SOURCE_LOCATION, [Path = GetMapCachePath(), Pixels = MoveTemp(Pixels)]
		{
			// A file cut short by a crash fails to decompress, which only costs one full redraw
			TArray<uint8> File;
			if (!FOodleCompressedArray::CompressTArray(File, Pixels, FOodleDataCompression::ECompressor::Kraken, FOodleDataCompression::ECompressionLevel::Fast)
				|| !FFileHelper::SaveArrayToFile(File, *Path))
			{
				CARTO_LOG_ERROR("Failed to save the map cache: %s", *Path);
				return;
			}
			CARTO_LOG("Map cache saved: %s (%d KB)", *Path, File.Num() / 1024);

			// Only the newest ones are kept
			IFileManager& FileManager = IFileManager::Get();
			TArray<FString> Files;
			FileManager.FindFiles(Files, *(GetMapCacheDir() / TEXT("*.mapcache")), true, false);
			Algo::SortBy(Files, [&FileManager](const FString& Name) { return FileManager.GetTimeStamp(*(GetMapCacheDir() / Name)); }, TGreater<>());
			for (int32 i = MAP_CACHE_COUNT; i < Files.Num(); i++)
			{
				FileManager.Delete(*(GetMapCacheDir() / Files[i]));
			}
		});
}


// Puts the image saved for this same map on it, if there is one
bool UCartographGameInstanceModule::LoadMapCache()
{
	if (FPlatformProperties::IsServerOnly() || RenderTarget->GetFormat() != PF_B8G8R8A8)
	{
		return false;
	}

	const FString Path = GetMapCachePath();
	TArray<uint8> File;
	TArray<FColor> Pixels;
	if (!FFileHelper::LoadFileToArray(File, *Path, FILEREAD_Silent)
		|| !FOodleCompressedArray::DecompressToTArray(Pixels, File) || Pixels.Num() != RENDER_TEXTURE_SIZE * RENDER_TEXTURE_SIZE)
	{
		CARTO_LOG("No map cache found");
		return false;
	}
	IFileManager::Get().SetTimeStamp(*Path, FDateTime::UtcNow());  // Keeps it among the newest

	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(CartographLoadMapCache)(
		[Resource, Pixels = MoveTemp(Pixels)](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.UpdateTexture2D(Resource->GetRenderTargetTexture(), 0,
				FUpdateTextureRegion2D{ 0, 0, 0, 0, RENDER_TEXTURE_SIZE, RENDER_TEXTURE_SIZE },
				RENDER_TEXTURE_SIZE * static_cast<uint32>(sizeof(FColor)), reinterpret_cast<const uint8*>(Pixels.GetData()));
		});
	RenderTarget->UpdateResourceImmediate(false);  // Regenerates the mips

	CARTO_LOG("Map cache loaded: %s", *Path);
	return true;
}


// Of the drawn buildings. Only the transforms, rounded: loading the game recalculates some of them
// (and things like spline points) with tiny differences.
uint64 UCartographGameInstanceModule::HashBuildingData() const
{
	uint64 Hash = 0;
	for (const FBuildingData& BuildingData : CurrentBuildingData)
	{
		if (!BuildingData.VisualBoxCache.bIsValid)
		{
			continue;  // Not drawn
		}

		const FVector Location = BuildingData.Transform.GetLocation();
		const FQuat Rotation = BuildingData.Transform.GetRotation();
		const int32 Values[] = {
			FMath::RoundToInt32(Location.X), FMath::RoundToInt32(Location.Y), FMath::RoundToInt32(Location.Z),
			FMath::RoundToInt32(Rotation.X * 10000), FMath::RoundToInt32(Rotation.Y * 10000),
			FMath::RoundToInt32(Rotation.Z * 10000), FMath::RoundToInt32(Rotation.W * 10000),
		};
		// A sum, so the order doesn't matter
		Hash += CityHash64WithSeed(reinterpret_cast<const char*>(Values), sizeof(Values), BuildingData.BuildableClassHash);
	}
	return Hash;
}


// Everything besides the buildings that changes the image. Summed since sets have no fixed order,
// and names are hashed as text since FName hashes differ between runs.
uint32 UCartographGameInstanceModule::HashDrawSettings() const
{
	uint32 Hash = 0;
	for (const FName& MainCategory : RuntimeConfig.DisabledLayerMainCategory)
	{
		Hash += FCrc::StrCrc32(*MainCategory.ToString());
	}
	for (const auto& [MainCategory, SubCategories] : RuntimeConfig.DisabledLayerSubCategory)
	{
		for (const FName& SubCategory : SubCategories)
		{
			Hash += FCrc::StrCrc32(*(MainCategory.ToString() + TEXT(":") + SubCategory.ToString()));
		}
	}
	for (const uint32 ClassHash : RuntimeConfig.DisabledLayerBuildable)
	{
		Hash += ClassHash;
	}
	// Mods can change the colors, or be Cartograph itself
	for (const FModInfo& Mod : GetWorld()->GetGameInstance()->GetSubsystem<UModLoadingLibrary>()->GetLoadedMods())
	{
		Hash += FCrc::StrCrc32(*(Mod.Name + TEXT("@") + Mod.Version.ToString()));
	}
	return Hash;
}
#pragma endregion


#pragma region UI
void UCartographGameInstanceModule::RegisterMenuButton() const
{
	if (!FPlatformProperties::RequiresCookedData() || FPlatformProperties::IsServerOnly()) 
	{
		return;
	}

	const auto* WidgetBlueprintClass = Cast<UWidgetBlueprintGeneratedClass>(MapContainerWidget.LoadSynchronous());
	UWidgetTree* WidgetTree = WidgetBlueprintClass->GetWidgetTreeArchetype();

	const UWidget* Menu = WidgetTree->FindWidget("BPW_MapMenu");
	CARTO_LOG_ERROR_RETURN_IF_NULL(Menu);
	const auto* MenuPanelSlot = Cast<UCanvasPanelSlot>(Menu->Slot);
	CARTO_LOG_ERROR_RETURN_IF_NULL(MenuPanelSlot);

	UWidget* ShowHideButton = WidgetTree->FindWidget("ShowHideButton");
    CARTO_LOG_ERROR_RETURN_IF_NULL(ShowHideButton);

	// Remove the Show/Hide menu button from its parent, as we'll be replacing that with a hbox
	int32 Index;
	UPanelWidget* Parent = UWidgetTree::FindWidgetParent(ShowHideButton, Index);
	CARTO_LOG_ERROR_RETURN_IF_NULL(Parent);
	Parent->RemoveChild(ShowHideButton);

	// Create a new hox and add it to the parent, maintaining the original index
    UHorizontalBox* HBox = NewObject<UHorizontalBox>(WidgetTree, UHorizontalBox::StaticClass(), "MenuShowHideButtonHBox", RF_Transient);
	auto* HBoxPanelSlot = Cast<UCanvasPanelSlot>(Parent->InsertChildAt(Index, HBox));
	HBoxPanelSlot->SetPosition({ 6, 6 });
	HBoxPanelSlot->SetAutoSize(true);

	// Add the Show/Hide menu button to the hbox
	HBox->AddChild(ShowHideButton);

	// Create Cartograph's Show/Hide menu button and add it to the hbox
	UWidget* CartographMenuShowHideButton = WidgetTree->ConstructWidget<UWidget>(MenuShowHideButtonWidget, "CartographMenuShowHideButton");
	auto* HBoxSlot = Cast<UHorizontalBoxSlot>(HBox->AddChild(CartographMenuShowHideButton));
	HBoxSlot->SetPadding({ 10, 0, 0, 0 });

	// Update the button's text
	FProperty* TextProperty = CartographMenuShowHideButton->GetClass()->FindPropertyByName("mText");
    CARTO_LOG_ERROR_RETURN_IF_NULL(TextProperty);
	FText* TextPtr = TextProperty->ContainerPtrToValuePtr<FText>(CartographMenuShowHideButton);
    CARTO_LOG_ERROR_RETURN_IF_NULL(TextPtr);
    *TextPtr = LOCTEXT("CartographMenuShow", "Show Cartograph Menu");


	// Now create the actual menu
	UWidget* CartographMenu = NewObject<UWidget>(HBox, MenuWidget, "CartographMenu", RF_Transient);
	auto* CartographMenuPanelSlot = Cast<UCanvasPanelSlot>(Parent->AddChild(CartographMenu));
    CartographMenuPanelSlot->SetLayout(MenuPanelSlot->GetLayout());
    CartographMenuPanelSlot->SetPosition(MenuPanelSlot->GetPosition());
    CartographMenuPanelSlot->SetSize(MenuPanelSlot->GetSize());
    CartographMenuPanelSlot->SetAutoSize(MenuPanelSlot->GetAutoSize());
    CartographMenuPanelSlot->SetZOrder(MenuPanelSlot->GetZOrder());

	CartographMenu->SetVisibility(ESlateVisibility::Collapsed);

    CARTO_LOG("Menu Button Registered");
}


void UCartographGameInstanceModule::LoadRuntimeConfig()
{
#if !UE_SERVER
	const FCartograph_ConfigStruct ConfigInstance = FCartograph_ConfigStruct::GetActiveConfig(GetWorld());

	RuntimeConfig = {};

	// ParseIntoArray drops empty entries, so trailing separators are fine
	TArray<FString> Entries;
	ConfigInstance.MainCategoryToggle.ParseIntoArray(Entries, TEXT(","));
	for (const FString& MainCategory : Entries)
	{
		RuntimeConfig.DisabledLayerMainCategory.Add(FName{ MainCategory });
	}

	// "Main:Sub,Sub|Main:Sub|"
	ConfigInstance.SubCategoryToggle.ParseIntoArray(Entries, TEXT("|"));
	for (const FString& Entry : Entries)
	{
		FString MainCategory, SubCategories;
		if (!Entry.Split(TEXT(":"), &MainCategory, &SubCategories))
		{
			CARTO_LOG_ERROR("Invalid SubCategoryToggle: %s", *ConfigInstance.SubCategoryToggle);
			break;
		}

		TArray<FString> SubCategoryNames;
		SubCategories.ParseIntoArray(SubCategoryNames, TEXT(","));
		for (const FString& SubCategory : SubCategoryNames)
		{
			RuntimeConfig.DisabledLayerSubCategory.FindOrAdd(FName{ MainCategory }).Add(FName{ SubCategory });
		}
	}

	ConfigInstance.BuildingToggle.ParseIntoArray(Entries, TEXT(","));
	for (const FString& Hash : Entries)
	{
		// Skip garbage instead of aborting like std::stoul would (no exceptions in UE)
		if (Hash.IsNumeric())
		{
			RuntimeConfig.DisabledLayerBuildable.Add(static_cast<uint32>(FCString::Strtoui64(*Hash, nullptr, 10)));
		}
	}

	CARTO_LOG_DEBUG("RuntimeConfig Loaded");
#endif
}


void UCartographGameInstanceModule::SaveRuntimeConfig()
{
#if !UE_SERVER
	const UConfigManager* ConfigManager = GetWorld()->GetGameInstance()->GetSubsystem<UConfigManager>();
	const UConfigPropertySection* RootSection = ConfigManager->GetConfigurationRootSection(FConfigId{ "Cartograph", "" });

	const auto SetStringProperty = [RootSection](const TCHAR* Name, const FString& Value)
		{
			const TObjectPtr<UConfigProperty>* Property = RootSection->SectionProperties.Find(Name);
			CARTO_LOG_ERROR_RETURN_IF_NULL(Property);
			auto* StringProperty = Cast<UConfigPropertyString>(*Property);
			CARTO_LOG_ERROR_RETURN_IF_NULL(StringProperty);
			StringProperty->Value = Value;
			StringProperty->MarkDirty();
		};
	const auto NameToString = [](const FName& Name) { return Name.ToString(); };

	SetStringProperty(TEXT("MainCategoryToggle"), FString::JoinBy(RuntimeConfig.DisabledLayerMainCategory, TEXT(","), NameToString));

	FString SubCategoryToggle;
	for (const auto& [MainCategory, SubCategories] : RuntimeConfig.DisabledLayerSubCategory)
	{
		SubCategoryToggle += MainCategory.ToString() + TEXT(":") + FString::JoinBy(SubCategories, TEXT(","), NameToString) + TEXT("|");
	}
	SetStringProperty(TEXT("SubCategoryToggle"), SubCategoryToggle);

	SetStringProperty(TEXT("BuildingToggle"), FString::JoinBy(RuntimeConfig.DisabledLayerBuildable, TEXT(","),
		[](uint32 Hash) { return FString::Printf(TEXT("%u"), Hash); }));

	CARTO_LOG_DEBUG("RuntimeConfig Saved");
#endif
}


void UCartographGameInstanceModule::FillBuildLayerDataCache()
{
	if (!BuildLayerDataMapCache.IsEmpty())
	{
		return;
	}

	for (const auto& [OriginalBuildableClass, _] : ClassPtrToClassIDMap)
	{
		if (!OriginalBuildableClass || BuildableToIgnore.Contains(OriginalBuildableClass.Get()))
		{
			continue;
		}

		const TSubclassOf<AFGBuildable> BuildableClass = ResolveBuildableClass(OriginalBuildableClass.Get());

		const uint32* BuildableClassHash = ClassPtrToClassIDMap.Find(BuildableClass);
		if (!BuildableClassHash)
		{
			CARTO_LOG_ERROR("Can't find hash for %s", *BuildableClass->GetName());
			continue;
		}


        const FBuildLayerData* LayerData = GetDataByBuildableClass(BuildableBuildLayerDataOverrideMap, BuildLayerDataMap, BuildableClass);
		if (!LayerData && ModdedBuildings.Contains(BuildableClass.Get()))
		{
			const FString& ModName = *ModdedBuildings.Find(BuildableClass.Get());
            LayerData = ModdedBuildLayerData.Find(ModName);
		}
		if (!LayerData)
		{
            CARTO_LOG_WARNING("Can't find layer data for %s", *BuildableClass->GetName());
            continue;
		}

		if (LayerData->MainCategoryCache.IsNone())
		{
			TArray<FString> CategoryNames;
			LayerData->Category.ParseIntoArray(CategoryNames, TEXT("/"));
            if (CategoryNames.Num() == 0)
            {
                CARTO_LOG_ERROR("Invalid Category: %s", *LayerData->Category);
                continue;
            }

			const_cast<FBuildLayerData*>(LayerData)->MainCategoryCache = FName{ CategoryNames[0] };
			const_cast<FBuildLayerData*>(LayerData)->SubCategoryCache = CategoryNames.Num() > 1 ? FName{ CategoryNames[1] } : NAME_None;
		}

		BuildLayerDataMapCache.Add(*BuildableClassHash, LayerData);
	}

    CARTO_LOG("BuildLayerDataCache Filled. Count: %d", BuildLayerDataMapCache.Num());
}


void UCartographGameInstanceModule::OnBuildingDataAdd(const FBuildingData& AddedBuildingData, int32 Pos)
{
	if (AddedBuildingData.VisualBoxCache.bIsValid)
	{
		CurrentBuildingQuadTree.Insert(BuildingDataIndexRedirector.Num(), AddedBuildingData.VisualBoxCache);
		// The initial data is added in sorted order, so there's nothing to shift
		if (!IsInitializing)
		{
			for (int32& Index : BuildingDataIndexRedirector)
			{
				if (Index >= Pos)
				{
					Index++;
				}
			}
		}
		BuildingDataIndexRedirector.Add(Pos);
	}

	BuildingCountMap.FindOrAdd(AddedBuildingData.BuildableClassHash)++;
}


void UCartographGameInstanceModule::OnBuildingDataRemove(const FBuildingData& RemovedBuildingData, int32 Pos)
{
	if (RemovedBuildingData.VisualBoxCache.bIsValid)
	{
		const int32 Num = BuildingDataIndexRedirector.Num();
		for (int32 i = 0; i < Num; i++)
		{
			int32& BuildingDataArrayIndex = BuildingDataIndexRedirector[i];
			if (BuildingDataArrayIndex == Pos)
			{
				CurrentBuildingQuadTree.Remove(i, RemovedBuildingData.VisualBoxCache);
				BuildingDataArrayIndex = -1;
			}
			else if (BuildingDataArrayIndex > Pos)
			{
				BuildingDataArrayIndex--;
			}
		}
	}

	BuildingCountMap.FindChecked(RemovedBuildingData.BuildableClassHash)--;
}


void UCartographGameInstanceModule::OnCartographMenuButtonClicked(UUserWidget* Widget, bool IsOpen)
{
	for (UWidget* ChildWidget : Widget->GetParent()->GetParent()->GetAllChildren())
	{
		if (ChildWidget->GetName() == "CartographMenu")
		{
			ChildWidget->SetVisibility(IsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			break;
		}
	}

	if (IsOpen)
	{
		auto* RootWidget = Cast<UWidget>(Widget->GetParent()->GetOuter()->GetOuter());
        CARTO_LOG_ERROR_RETURN_IF_NULL(RootWidget);
		FOutputDeviceNull Ar;
		RootWidget->CallFunctionByNameWithArguments(TEXT("SetFiltersCollapsed 1"), Ar, nullptr, true);
	}
}


void UCartographGameInstanceModule::OnShowBuildingsCheckboxChanged(bool DoShow)
{
    DoShowBuildings = DoShow;
    CARTO_LOG("DoShowBuildings: %d", DoShowBuildings);
}


TArray<FString> UCartographGameInstanceModule::GetLayerCategoryOptions() const
{
	TArray<FString> Options;
	for (const FLayerCategoryData& LayerCategory : LayerCategories)
	{
		Options.Add(LayerCategory.Name.ToString());

		for (const FLayerSubCategoryData& LayerSubCategory : LayerCategory.SubCategories)
		{
			Options.Add(FString::Printf(TEXT("%s/%s"), *LayerCategory.Name.ToString(), *LayerSubCategory.Name.ToString()));
		}
	}
	return Options;
}


void UCartographGameInstanceModule::OnVanillaMapMenuShown(const UUserWidget* Widget) const
{
	UWidget* Menu = Widget->WidgetTree->FindWidget("CartographMenu");
	CARTO_LOG_ERROR_RETURN_IF_NULL(Menu);
	Menu->SetVisibility(ESlateVisibility::Collapsed);

	UWidget* Button = Widget->WidgetTree->FindWidget("CartographMenuShowHideButton");
	CARTO_LOG_ERROR_RETURN_IF_NULL(Button);

	FProperty* IsOpenProperty = Button->GetClass()->FindPropertyByName("IsOpen");
	CARTO_LOG_ERROR_RETURN_IF_NULL(IsOpenProperty);
	*IsOpenProperty->ContainerPtrToValuePtr<bool>(Button) = false;

	FOutputDeviceNull Ar;
	Button->CallFunctionByNameWithArguments(TEXT("SetShowHideText"), Ar, nullptr, true);
}
#pragma endregion


void UCartographGameInstanceModule::GatherBuildables()
{
    ClassIDToClassPtrMap.Empty();
    ClassPtrToClassIDMap.Empty();
    ClassPtrToDescriptorDataMap.Empty();

	for (const FTopLevelAssetPath& AssetPath : GetDerivedClassPaths(AFGBuildable::StaticClass()))
	{
		const TSubclassOf<AFGBuildable> Class = StaticLoadClass(AFGBuildable::StaticClass(), nullptr, *AssetPath.ToString());
        if (!Class)
        {
            CARTO_LOG_ERROR("Failed to load class from path: %s", *AssetPath.ToString());
            continue;
        }

		const FString Name = Class->GetName();
		const FString PackageName = AssetPath.GetPackageName().ToString();
		if (PackageName.StartsWith("/Script")
			|| Name.StartsWith("SKEL_") || Name.StartsWith("REINST_"))
		{
			continue;
		}

		if (!PackageName.StartsWith("/Game/"))
		{
			const int32 Index = PackageName.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 2);
			const FString ModName = PackageName.Mid(1, Index - 1);
			ModdedBuildings.Add(Class.Get(), ModName);
			FLayerCategoryData* ModdedCategory = LayerCategories.FindByPredicate(
				[](const FLayerCategoryData& CategoryData) { return CategoryData.Name == UnspecifiedMainCategory; });
			CARTO_LOG_ERROR_DO_IF_NULL(ModdedCategory, continue);
            if (!ModdedCategory->SubCategories.FindByPredicate(
				[ModFName = FName{ ModName }](const FLayerSubCategoryData& CategoryData) { return CategoryData.Name == ModFName; }))
            {
                ModdedCategory->SubCategories.Add(FLayerSubCategoryData{
                    .Name = FName{ ModName },
                    .DisplayName = FText::FromString(ModName),
					});
            }

			if (!ModdedBuildLayerData.Contains(ModName))
			{
				ModdedBuildLayerData.Add(ModName, FBuildLayerData{
					.MainCategoryCache = FName{ UnspecifiedMainCategory },
					.SubCategoryCache = FName{ ModName },
					});
			}
		}

		const uint32 Hash = TextKeyUtil::HashString(AssetPath.ToString());
		ClassPtrToClassIDMap.Add(Class, Hash);
		ClassIDToClassPtrMap.Add(Hash, Class);
		CARTO_LOG_DEBUG("Path: %s, Class: %s, Hash: %u", *AssetPath.ToString(), *Name, Hash);
	}
	for (const FTopLevelAssetPath& AssetPath : GetDerivedClassPaths(UFGBuildingDescriptor::StaticClass()))
	{
		const TSubclassOf<UFGBuildingDescriptor> Descriptor = StaticLoadClass(UFGBuildingDescriptor::StaticClass(), nullptr, *AssetPath.ToString());
        if (!Descriptor)
        {
            CARTO_LOG_ERROR("Failed to load descriptor from path: %s", *AssetPath.ToString());
            continue;
        }

		const FString Name = Descriptor->GetName();
		const FString PackageName = AssetPath.GetPackageName().ToString();
		if (PackageName.StartsWith("/Script")
			|| Name.StartsWith("SKEL_") || Name.StartsWith("REINST_"))
		{
			continue;
		}

		const auto* DescriptorInstance = GetDefault<UFGBuildingDescriptor>(Descriptor);
		CARTO_LOG_ERROR_DO_IF_NULL(DescriptorInstance, continue);
		TSubclassOf<AFGBuildable> BuildableClass = DescriptorInstance->mBuildableClass;
		if (!BuildableClass)
		{
			continue;
		}

		TSubclassOf<UFGBuildSubCategory> BuildSubCategory;
		for (const TSubclassOf<UFGCategory>& SubCategory : DescriptorInstance->mSubCategories)
		{
            if (!SubCategory)
            {
                continue;
            }

			if (SubCategory->IsChildOf(UFGBuildSubCategory::StaticClass()))
			{
				BuildSubCategory = SubCategory;
				break;
			}
		}

        UTexture2D* Icon = DescriptorInstance->mSmallIcon;
		if (!Icon)
		{
            // Some buildings like blueprint designers don't have small icon
			Icon = DescriptorInstance->mPersistentBigIcon;
		}

        ClassPtrToDescriptorDataMap.Add(BuildableClass, FBuildingDescriptorData{
			.Category = DescriptorInstance->mCategory,
			.SubCategory = BuildSubCategory,
			.Icon = Icon,
        });
		
		CARTO_LOG_DEBUG("Path: %s, Class: %s, BuildableClass: %s, NoIcon: %d",
			*AssetPath.ToString(), 
			*Name, 
			*BuildableClass->GetName(),
			Icon == nullptr);
	}

    CARTO_LOG("Buildables Gathered. Buildable: %d, Descriptor: %d", ClassPtrToClassIDMap.Num(), ClassPtrToDescriptorDataMap.Num());
}


template<typename T>
concept HasStaticStruct = requires
{
	T::StaticStruct();
};


template<typename T>
void UCartographGameInstanceModule::FillInMatchingProperties(const FProperty* StructPropertyToCompare, TArray<std::pair<const FProperty*, const FProperty*>>& Out)
{
	static_assert(HasStaticStruct<T>);

	const auto* StructProperty = CastField<FStructProperty>(StructPropertyToCompare);
	if (!StructProperty)
	{
		CARTO_LOG_ERROR("Property type should be a struct!");
		return;
	}

	for (TFieldIterator<FProperty> PropertyIt{ T::StaticStruct(), EFieldIteratorFlags::IncludeSuper }; PropertyIt; ++PropertyIt)
	{
		// FindPropertyByName doesn't work here, they're stored as like "Category_2_1FC60E30497F81318BD45A8E6799F144"
		const FProperty* OverrideStructProperty = StructProperty->Struct->CustomFindProperty(PropertyIt->GetFName());
		if (!OverrideStructProperty)
		{
			continue;
		}
		CARTO_LOG("Found struct property: %s", *OverrideStructProperty->GetAuthoredName());

		if (!OverrideStructProperty->SameType(*PropertyIt))
		{
			CARTO_LOG_ERROR("Struct property type is wrong");
			continue;
		}

		Out.Add({ *PropertyIt, OverrideStructProperty });
	}
}


template<typename KeyType, typename ValueType>
void UCartographGameInstanceModule::ProcessOverrideData(TMap<KeyType, ValueType>& MapToBeOverriden, UClass* OverrideDataClass, FName PropertyName)
{
	const FProperty* ThisProperty = GetClass()->FindPropertyByName(PropertyName);
	CARTO_LOG_ERROR_RETURN_IF_NULL(ThisProperty);

	const auto* ThisMapProperty = CastField<const FMapProperty>(ThisProperty);
	CARTO_LOG_ERROR_RETURN_IF_NULL(ThisMapProperty);


	const FProperty* Property = OverrideDataClass->FindPropertyByName(PropertyName);
	if (!Property)
	{
		return;
	}

    CARTO_LOG("Found Property: %s", *PropertyName.ToString());

	const auto* MapProperty = CastField<const FMapProperty>(Property);
	if (!MapProperty)
	{
		CARTO_LOG_ERROR("It should be a map!");
		return;
	}

	if (!MapProperty->KeyProp->SameType(ThisMapProperty->KeyProp))
	{
        CARTO_LOG_ERROR("Key type is wrong");
        return;
	}

    TArray<std::pair<const FProperty*, const FProperty*>> MatchingValueStructProperties;
    if constexpr (!HasStaticStruct<ValueType>)
    {
        if (!MapProperty->ValueProp->SameType(ThisMapProperty->ValueProp))
        {
            CARTO_LOG_ERROR("Value type is wrong");
            return;
        }
    }
	else
	{
        FillInMatchingProperties<ValueType>(MapProperty->ValueProp, MatchingValueStructProperties);
		if (MatchingValueStructProperties.Num() == 0)
		{
			return;
		}
	}


	auto* CDO = GetMutableDefault<UObject>(OverrideDataClass);
	CARTO_LOG_ERROR_RETURN_IF_NULL(CDO);
	MapProperty->WithScriptMap(Property->ContainerPtrToValuePtr<void>(CDO),
		[this, MapProperty, &MatchingValueStructProperties, &MapToBeOverriden](auto* OverrideMap)
		{
			const int32 Num = OverrideMap->Num();
			for (int32 i = 0; i < Num; i++)
			{
				const uint8* KeyPtr = static_cast<uint8*>(OverrideMap->GetData(i, MapProperty->MapLayout));
				const uint8* ValuePtr = KeyPtr + MapProperty->MapLayout.ValueOffset;

				const KeyType& Key = *reinterpret_cast<const KeyType*>(KeyPtr);
				ValueType& OverrideValue = MapToBeOverriden.FindOrAdd(Key);

				if constexpr (!HasStaticStruct<ValueType>)
				{
                    OverrideValue = *reinterpret_cast<const ValueType*>(ValuePtr);
				}
				else
				{
                    for (const auto& [OriginalStructProperty, OverrideStructProperty] : MatchingValueStructProperties)
                    {
                        auto* StructPropertyValue = OverrideStructProperty->ContainerPtrToValuePtr<void>(ValuePtr);
                        OverrideStructProperty->CopyCompleteValue(OriginalStructProperty->ContainerPtrToValuePtr<void>(&OverrideValue), StructPropertyValue);
                    }
				}
			}
            CARTO_LOG("Added %d elements", Num);
		}
	);
}


template<typename T>
void UCartographGameInstanceModule::ProcessOverrideData(TSet<T>& SetToBeOverriden, UClass* OverrideDataClass, FName PropertyName)
{
	static_assert(!HasStaticStruct<T>, "Didn't bother to implement");

	const FProperty* ThisProperty = GetClass()->FindPropertyByName(PropertyName);
	CARTO_LOG_ERROR_RETURN_IF_NULL(ThisProperty);

	const auto* ThisSetProperty = CastField<const FSetProperty>(ThisProperty);
	CARTO_LOG_ERROR_RETURN_IF_NULL(ThisSetProperty);


	const FProperty* Property = OverrideDataClass->FindPropertyByName(PropertyName);
	if (!Property)
	{
		return;
	}

	CARTO_LOG("Found Property: %s", *PropertyName.ToString());

	const auto* SetProperty = CastField<const FSetProperty>(Property);
	if (!SetProperty)
	{
		CARTO_LOG_ERROR("It should be a set!");
		return;
	}

	if (!SetProperty->ElementProp->SameType(ThisSetProperty->ElementProp))
	{
		CARTO_LOG_ERROR("Element type is wrong");
		return;
	}


	auto* CDO = GetMutableDefault<UObject>(OverrideDataClass);
	CARTO_LOG_ERROR_RETURN_IF_NULL(CDO);
	void* OverrideSet = Property->ContainerPtrToValuePtr<void>(CDO);
	const int32 Num = SetProperty->GetNum(OverrideSet);
    for (int32 i = 0; i < Num; i++)
    {
        const uint8* ElementPtr = SetProperty->GetElementPtr(OverrideSet, i);
        const T& Element = *reinterpret_cast<const T*>(ElementPtr);
        SetToBeOverriden.Add(Element);
    }
    CARTO_LOG("Added %d elements", Num);
}


void UCartographGameInstanceModule::ProcessLayerCategoriesOverride(UClass* OverrideDataClass)
{
	const FProperty* ThisProperty = GetClass()->FindPropertyByName("LayerCategories");
	CARTO_LOG_ERROR_RETURN_IF_NULL(ThisProperty);

	const auto* ThisArrayProperty = CastField<const FArrayProperty>(ThisProperty);
	CARTO_LOG_ERROR_RETURN_IF_NULL(ThisArrayProperty);


	const FProperty* Property = OverrideDataClass->FindPropertyByName("LayerCategories");
	if (!Property)
	{
		return;
	}

	CARTO_LOG("Found Property: LayerCategories");

	const auto* ArrayProperty = CastField<const FArrayProperty>(Property);
	if (!ArrayProperty)
	{
		CARTO_LOG_ERROR("It should be an array!");
		return;
	}

	const auto* StructProperty = CastField<FStructProperty>(ArrayProperty->Inner);
	if (!StructProperty)
	{
		CARTO_LOG_ERROR("Property type should be a struct!");
		return;
	}

	TArray<std::pair<const FProperty*, const FProperty*>> MatchingValueStructProperties;
    FillInMatchingProperties<FLayerSubCategoryData>(ArrayProperty->Inner, MatchingValueStructProperties);
	if (MatchingValueStructProperties.Num() == 0)
	{
		return;
	}

	const FArrayProperty* SubCategoriesArrayProperty = nullptr;
	TArray<std::pair<const FProperty*, const FProperty*>> MatchingSubCategoriesStructProperties;
	if (const FProperty* SubCategoriesProperty = StructProperty->Struct->CustomFindProperty("SubCategories"))
	{
		CARTO_LOG("Found struct property: SubCategories");

        SubCategoriesArrayProperty = CastField<FArrayProperty>(SubCategoriesProperty);
        if (!SubCategoriesArrayProperty)
        {
            CARTO_LOG_ERROR("It should be an array!");
        }
		else
		{
            FillInMatchingProperties<FLayerSubCategoryData>(SubCategoriesArrayProperty->Inner, MatchingSubCategoriesStructProperties);
		}
	}


	auto* CDO = GetMutableDefault<UObject>(OverrideDataClass);
	CARTO_LOG_ERROR_RETURN_IF_NULL(CDO);
    void* OverrideArray = Property->ContainerPtrToValuePtr<void>(CDO);
    const int32 Num = FScriptArrayHelper{ ArrayProperty, OverrideArray }.Num();
    CARTO_LOG_DEBUG("Categories: %d", Num);
    for (int32 i = 0; i < Num; i++)
    {
		void* ElementPtr = ArrayProperty->GetValueAddressAtIndex_Direct(ArrayProperty->Inner, OverrideArray, i);
		FLayerCategoryData& OverrideValue = LayerCategories.AddDefaulted_GetRef();

		for (const auto& [OriginalStructProperty, OverrideStructProperty] : MatchingValueStructProperties)
		{
			auto* StructPropertyValue = OverrideStructProperty->ContainerPtrToValuePtr<void>(ElementPtr);
			OverrideStructProperty->CopyCompleteValue(OriginalStructProperty->ContainerPtrToValuePtr<void>(&OverrideValue), StructPropertyValue);
		}

		CARTO_LOG_DEBUG("MainCategory #%d", i);
        CARTO_LOG_DEBUG("Name: %s", *OverrideValue.Name.ToString());
        CARTO_LOG_DEBUG("DisplayName: %s", *OverrideValue.DisplayName.ToString());
        CARTO_LOG_DEBUG("Priority: %d", OverrideValue.Priority);

        if (!SubCategoriesArrayProperty)
        {
			continue;
        }

		void* SubCategoriesArray = SubCategoriesArrayProperty->ContainerPtrToValuePtr<void>(ElementPtr);
        const int32 SubCategoryNum = FScriptArrayHelper{ SubCategoriesArrayProperty, SubCategoriesArray }.Num();
		CARTO_LOG_DEBUG("SubCategories: %d", SubCategoryNum);
    	for (int32 j = 0; j < SubCategoryNum; j++)
		{
			const void* SubCategoriesElementPtr = SubCategoriesArrayProperty->GetValueAddressAtIndex_Direct(SubCategoriesArrayProperty->Inner, SubCategoriesArray, j);
			FLayerSubCategoryData& SubCategoryData = OverrideValue.SubCategories.AddDefaulted_GetRef();

			for (const auto& [OriginalStructProperty, OverrideStructProperty] : MatchingSubCategoriesStructProperties)
			{
				auto* StructPropertyValue = OverrideStructProperty->ContainerPtrToValuePtr<void>(SubCategoriesElementPtr);
				OverrideStructProperty->CopyCompleteValue(OriginalStructProperty->ContainerPtrToValuePtr<void>(&SubCategoryData), StructPropertyValue);
			}

            CARTO_LOG_DEBUG("SubCategory #%d", j);
            CARTO_LOG_DEBUG("Name: %s", *SubCategoryData.Name.ToString());
            CARTO_LOG_DEBUG("DisplayName: %s", *SubCategoryData.DisplayName.ToString());
            CARTO_LOG_DEBUG("Priority: %d", SubCategoryData.Priority);
		}
    }
}


void UCartographGameInstanceModule::GatherModOverrides()
{
	TArray<FAssetData> OverrideAssetArray;
	const FName OverrideDataFileName{ "CartographOverrideData" };

	TBaseStructure<FVector>::Get();
	TBaseStructure<FCategoryData>::Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	IAssetRegistry::Get()->EnumerateAssets(Filter,
		[&OverrideAssetArray, &OverrideDataFileName](const FAssetData& Asset)
		{
			if (Asset.AssetName == OverrideDataFileName)
			{
				OverrideAssetArray.Add(Asset);
			}

			return true;
		});

	for (const FAssetData& OverrideAssetData : OverrideAssetArray)
	{
		CARTO_LOG("Override Data Found: %s", *OverrideAssetData.PackageName.ToString());

		const FString OverrideDataClassName = OverrideAssetData.GetObjectPathString() + TEXT("_C");
		UClass* OverrideDataClass = LoadObject<UClass>(nullptr, *OverrideDataClassName);
		CARTO_LOG_ERROR_DO_IF_NULL(OverrideDataClass, continue);

#define PROCESS_OVERRIDE(x) ProcessOverrideData(x, OverrideDataClass, FName{ #x })
		PROCESS_OVERRIDE(BuildCategoryDataMap);
		PROCESS_OVERRIDE(BuildableBuildCategoryDataOverrideMap);
		PROCESS_OVERRIDE(MaterialBuildCategoryDataOverrideMap);
		PROCESS_OVERRIDE(BuildableIconOverrideMap);
		PROCESS_OVERRIDE(BuildableSizeOverrideMap);
		PROCESS_OVERRIDE(BuildableExtraRotationMap);
		PROCESS_OVERRIDE(BuildableSplineDataMap);
		PROCESS_OVERRIDE(BuildableWireDataMap);
		PROCESS_OVERRIDE(BuildableClassRedirectMap);
		PROCESS_OVERRIDE(BuildableToIgnore);
		PROCESS_OVERRIDE(BuildLayerDataMap);
		PROCESS_OVERRIDE(BuildableBuildLayerDataOverrideMap);
		PROCESS_OVERRIDE(MaterialBuildLayerDataOverrideMap);
#undef PROCESS_OVERRIDE
		ProcessLayerCategoriesOverride(OverrideDataClass);
	}
}


TSet<FTopLevelAssetPath> UCartographGameInstanceModule::GetDerivedClassPaths(UClass* ParentClass)
{
	TArray<UClass*> NativeRootClasses;
	NativeRootClasses.Add(ParentClass);
	GetDerivedClasses(ParentClass, NativeRootClasses);

	TArray<FTopLevelAssetPath> NativeRootClassPaths;

	Algo::TransformIf(NativeRootClasses,
		NativeRootClassPaths,
		[](const UClass* RootClass) { return RootClass && RootClass->HasAnyClassFlags(CLASS_Native); },
		&UClass::GetClassPathName);

	TSet<FTopLevelAssetPath> AllClassPaths;
	IAssetRegistry::Get()->GetDerivedClassNames(NativeRootClassPaths, {}, AllClassPaths);

	return AllClassPaths;
}


#undef LOCTEXT_NAMESPACE
