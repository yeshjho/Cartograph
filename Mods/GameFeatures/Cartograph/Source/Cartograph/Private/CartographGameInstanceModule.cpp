#include "CartographGameInstanceModule.h"

#include <sstream>

#include "Algo/FindLast.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Misc/OutputDeviceNull.h"

#include "FGBuildableBeam.h"
#include "FGBuildableSubsystem.h"
#include "FGBuildCategory.h"
#include "FGBuildSubCategory.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSplineBuildableInterface.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"
#include "Resources/FGBuildingDescriptor.h"

#include "Configuration/Properties/ConfigPropertyString.h"
#include "ModLoading/ModLoadingLibrary.h"
#include "Patching/BlueprintHookManager.h"
#include "Patching/NativeHookManager.h"

#include "CartographBuildingDataSubsystem.h"
#include "CartographMapDrawSubsystem.h"
#include "CartographRemoteCallObject.h"
#include "Cartograph_ConfigStruct.h"


#define LOCTEXT_NAMESPACE "Cartograph"


void UCartographGameInstanceModule::DispatchLifecycleEvent(const ELifecyclePhase Phase)
{
	Super::DispatchLifecycleEvent(Phase);

	switch (Phase)
	{
	case ELifecyclePhase::CONSTRUCTION:
		break;

	case ELifecyclePhase::INITIALIZATION:
		CARTO_LOG("UCartographGameInstanceModule Init - INITIALIZATION")

		RegisterMenuButton();
		break;

	case ELifecyclePhase::POST_INITIALIZATION:
		CARTO_LOG("UCartographGameInstanceModule Init - POST_INITIALIZATION")

		Instance = this;

		GatherBuildables();
		GatherModOverrides();
		FlattenMaterialData();
		LoadRuntimeConfig();
		break;
	}
}


void UCartographGameInstanceModule::OnLayerConfigChanged()
{
	CARTO_LOG_DEBUG("OnLayerConfigChanged");

	if (auto* MapDrawSubsystem = ACartographMapDrawSubsystem::Instance)
	{
		MapDrawSubsystem->RedrawMap();
	}
	SaveRuntimeConfig();
}


const FBuildLayerData* UCartographGameInstanceModule::GetBuildLayerData(const uint32 ClassHash)
{
	FillBuildLayerDataCache();

    if (const auto* DataCache = BuildLayerDataMapCache.Find(ClassHash))
    {
        return *DataCache;
    }
	return nullptr;
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
	CurrentBuildingData.Empty(Total);
    BuildingDataIndexRedirector.Empty();
    CurrentBuildingQuadTree.Empty();
    BuildingCountMap.Empty();

	const float TimeBudget = FCartograph_ConfigStruct::GetActiveConfig(GetWorld()).InitializeTimeBudget;
	UE5Coro::Latent::FTickTimeBudget Budget = UE5Coro::Latent::FTickTimeBudget::Milliseconds(TimeBudget);

	int Processed = 0;

	for (const TWeakObjectPtr<AFGBuildable>& Factory : Factories)
	{
		if (!Factory.IsValid() || BuildableToIgnore.Contains(Factory->GetClass()))
		{
			continue;
		}
		
		FBuildingData NewBuildingData{
			.Transform = Factory->GetTransform(),
            //.CustomizationData = Factory->GetCustomizationData_Native(),
		};
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
			FBuildingData NewBuildingData{
				.Transform = InstanceData.Transform,
				//.CustomizationData = InstanceData.CustomizationData,
			};
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
		const FBuildingData& BuildingData = CurrentBuildingData[i];
		OnBuildingDataAdd(BuildingData, i);
	}

	MinHeight = !CurrentBuildingData.IsEmpty() ? CurrentBuildingData[0].Transform.GetLocation().Z : -100;
	MaxHeight = !CurrentBuildingData.IsEmpty() ? CurrentBuildingData.Last().Transform.GetLocation().Z : 100;
	OnZFilterUpdated(0, 1);

	CARTO_LOG("InitialBuildableGather Finished");

	for (const auto& [ClassHash, Count] : BuildingCountMap)
	{
        CARTO_LOG_DEBUG("Building: %u, Count: %d", ClassHash, Count);
	}

	IsInitializing = false;
	IsPendingRedraw = false;
	ExecuteRedrawMapCoroutine(true);
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

	const float TimeBudget = FCartograph_ConfigStruct::GetActiveConfig(GetWorld()).RedrawTimeBudget;
	UE5Coro::Latent::FTickTimeBudget Budget = UE5Coro::Latent::FTickTimeBudget::Milliseconds(TimeBudget);

    IsRedrawingEntirely |= bRedrawEntirely;

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
                if (RemovedBuildingData > CurrentBuildingData[i])
                {
                    CARTO_LOG_ERROR("Can't find removed building data");
                    break;
                }
	        }

	        co_await Budget;
	    }

        MinHeight = !CurrentBuildingData.IsEmpty() ? CurrentBuildingData[0].Transform.GetLocation().Z : -100;
        MaxHeight = !CurrentBuildingData.IsEmpty() ? CurrentBuildingData.Last().Transform.GetLocation().Z : 100;
		const float Length = MaxHeight - MinHeight;
		MinZFilter = FMath::Floor(MinCached * Length + MinHeight);
		MaxZFilter = FMath::CeilToInt(MaxCached * Length + MinHeight);

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
		IsRedrawingEntirely = false;
		RedrawArea = {};
		RedrawArea.bIsValid = false;
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
			IsRedrawingEntirely = false;
			RedrawArea = {};
			RedrawArea.bIsValid = false;
            co_return;
        }

		const int* Beg = BuildingsToDraw.GetData();
        BuildingsToDraw.RemoveAt(MaxIt - Beg + 1, BuildingsToDraw.Num() - (MaxIt - Beg + 1), EAllowShrinking::No);
        BuildingsToDraw.RemoveAt(0, MinIt - Beg);
		co_await Budget;

		CARTO_LOG_DEBUG("Overlapping Elements: %d", BuildingsToDraw.Num());
	}

	for (int32 i : BuildingsToDraw)
	{
        const auto& [ClassHash, Transform/*, CustomizationData*/, BuildableExtraData, 
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
				LoadedTexture = co_await UE5Coro::Latent::AsyncLoadObject(*Texture);
			}
			CARTO_LOG_ERROR_BREAK_IF_NULL(LoadedTexture);

			FCanvasTileItem TileItem{
				ScreenPosition,
				LoadedTexture->GetResource(),
				{ Size.X * PIXEL_PER_CENTIMETER[0], Size.Y * PIXEL_PER_CENTIMETER[1] },
				{ 0, 0 },
				{ 1, 1 },
				FLinearColor::White
			};
			TileItem.PivotPoint = { 0.5, 0.5 };
			TileItem.BlendMode = FCanvas::BlendToSimpleElementBlend(EBlendMode::BLEND_Translucent);
			TileItem.Rotation = Rotation;

			Canvas->DrawItem(TileItem);

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

			FCanvasTileItem TileItem{
				ScreenPosition,
				{ Size.X * PIXEL_PER_CENTIMETER[0], Size.Y * PIXEL_PER_CENTIMETER[1] },
				CategoryData->MainColor
			};
			TileItem.PivotPoint = { 0.5, 0.5 };
			TileItem.BlendMode = FCanvas::BlendToSimpleElementBlend(EBlendMode::BLEND_Translucent);
			TileItem.Rotation = Rotation;

			Canvas->DrawItem(TileItem);
			co_await Budget;

			if (CategoryData->OutlineThickness > 0)
			{
				draw_line(Canvas, Corners[0], Corners[1], CategoryData->OutlineColor, CategoryData->OutlineThickness);
				co_await Budget;
				draw_line(Canvas, Corners[1], Corners[2], CategoryData->OutlineColor, CategoryData->OutlineThickness);
				co_await Budget;
				draw_line(Canvas, Corners[2], Corners[3], CategoryData->OutlineColor, CategoryData->OutlineThickness);
				co_await Budget;
				draw_line(Canvas, Corners[3], Corners[0], CategoryData->OutlineColor, CategoryData->OutlineThickness);
			}

			break;
		}

		case EBuildingDataType::Spline:
		{
            const FSplineDataCache* SplineDataCachePtr = std::get_if<FSplineDataCache>(&DataCache);
            CARTO_LOG_ERROR_BREAK_IF_NULL(SplineDataCachePtr);

			const FSplineExtraData* SplineExtraData = std::get_if<FSplineExtraData>(&BuildableExtraData);
            CARTO_LOG_ERROR_BREAK_IF_NULL(SplineExtraData);

            const int Num = SplineExtraData->Points.Num();
            for (int j = 0; j < Num - 1; j++)
            {
                draw_line(Canvas, SplineExtraData->Points[j], SplineExtraData->Points[j + 1],
					SplineDataCachePtr->SplineData->Color, SplineDataCachePtr->SplineData->Thickness);
                co_await Budget;
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

			draw_line(Canvas, Transform.GetLocation(), WireExtraData->End, WireData->Color, WireData->Thickness);

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
			draw_line(Canvas, Start, End, BeamData->Color, BeamData->Thickness);

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
            draw_line(Canvas, FVector2D{ MinPoint.X, MinPoint.Y }, FVector2D{ MaxPoint.X, MinPoint.Y }, Color, Thickness);
			draw_line(Canvas, FVector2D{ MaxPoint.X, MinPoint.Y }, FVector2D{ MaxPoint.X, MaxPoint.Y }, Color, Thickness);
			draw_line(Canvas, FVector2D{ MaxPoint.X, MaxPoint.Y }, FVector2D{ MinPoint.X, MaxPoint.Y }, Color, Thickness);
			draw_line(Canvas, FVector2D{ MinPoint.X, MaxPoint.Y }, FVector2D{ MinPoint.X, MinPoint.Y }, Color, Thickness);
		}

		co_await Budget;
	}

	IsRedrawingEntirely = false;
	RedrawArea = {};
	RedrawArea.bIsValid = false;

	CARTO_LOG_DEBUG("RedrawMapCoroutine Finished");
}




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
	UWidget* CartographMenuShowHideButton = WidgetTree->ConstructWidget<UWidget>(MenuShowHideButtonWidget.LoadSynchronous(), "CartographMenuShowHideButton");
	auto* HBoxSlot = Cast<UHorizontalBoxSlot>(HBox->AddChild(CartographMenuShowHideButton));
	HBoxSlot->SetPadding({ 10, 0, 0, 0 });

	// Update the button's text
	FProperty* TextProperty = CartographMenuShowHideButton->GetClass()->FindPropertyByName("mText");
    CARTO_LOG_ERROR_RETURN_IF_NULL(TextProperty);
	FText* TextPtr = TextProperty->ContainerPtrToValuePtr<FText>(CartographMenuShowHideButton);
    CARTO_LOG_ERROR_RETURN_IF_NULL(TextPtr);
    *TextPtr = LOCTEXT("CartographMenuShow", "Show Cartograph Menu");

	// Now create the actual menu
	UWidget* CartographMenu = NewObject<UWidget>(HBox, MenuWidget.LoadSynchronous(), "CartographMenu", RF_Transient);
	auto* CartographMenuPanelSlot = Cast<UCanvasPanelSlot>(Parent->AddChild(CartographMenu));
    CartographMenuPanelSlot->SetLayout(MenuPanelSlot->GetLayout());
    CartographMenuPanelSlot->SetPosition(MenuPanelSlot->GetPosition());
    CartographMenuPanelSlot->SetSize(MenuPanelSlot->GetSize());
    CartographMenuPanelSlot->SetAutoSize(MenuPanelSlot->GetAutoSize());
    CartographMenuPanelSlot->SetZOrder(MenuPanelSlot->GetZOrder());

	CartographMenu->SetVisibility(ESlateVisibility::Collapsed);

    CARTO_LOG("Menu Button Registered");
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
			CARTO_LOG_ERROR_CONTINUE_IF_NULL(ModdedCategory);
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
		ClassPtrToClassIDMap.Add(Class.Get(), Hash);
		ClassIDToClassPtrMap.Add(Hash, Class.Get());
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
		CARTO_LOG_ERROR_CONTINUE_IF_NULL(DescriptorInstance);
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

#define VAR(x) std::make_pair(std::ref(x), FName{ #x })
	const auto OverrideableVariables = std::make_tuple(
		VAR(BuildCategoryDataMap), VAR(BuildableBuildCategoryDataOverrideMap), VAR(MaterialBuildCategoryDataOverrideMap),
		VAR(BuildableIconOverrideMap), VAR(BuildableSizeOverrideMap), VAR(BuildableExtraRotationMap),
		VAR(BuildableSplineDataMap), VAR(BuildableWireDataMap),
		VAR(BuildableClassRedirectMap), VAR(BuildableToIgnore),
		/*VAR(LayerCategories),*/ VAR(BuildLayerDataMap), VAR(BuildableBuildLayerDataOverrideMap), VAR(MaterialBuildLayerDataOverrideMap));
#undef VAR


	for (const FAssetData& OverrideAssetData : OverrideAssetArray)
	{
		CARTO_LOG("Override Data Found: %s", *OverrideAssetData.PackageName.ToString());

		const FString OverrideDataClassName = OverrideAssetData.GetObjectPathString() + TEXT("_C");
		UClass* OverrideDataClass = LoadObject<UClass>(nullptr, *OverrideDataClassName);
		CARTO_LOG_ERROR_CONTINUE_IF_NULL(OverrideDataClass);

		const auto LambdaProcessOverrideData =
			[this, OverrideDataClass, &OverrideableVariables]<size_t Index>()
		{
			auto& [VariableRef, Name] = std::get<Index>(OverrideableVariables);
			ProcessOverrideData(VariableRef, OverrideDataClass, Name);
		};

		[&LambdaProcessOverrideData]<size_t ...Index>(std::index_sequence<Index...>)
		{
			(LambdaProcessOverrideData.template operator()<Index>(), ...);
		}(std::make_index_sequence<std::tuple_size_v<decltype(OverrideableVariables)>>{});
		ProcessLayerCategoriesOverride(OverrideDataClass);
	}
}

void UCartographGameInstanceModule::FlattenMaterialData()
{
	for (const auto& [Material, CategoryData] : MaterialBuildCategoryDataOverrideMap)
	{
		for (const auto& [_, Recipe] : GetMutableDefault<UFGFactoryCustomizationDescriptor_Material>(Material.LoadSynchronous())->GetBuildableMap())
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
		for (const auto& [_, Recipe] : GetMutableDefault<UFGFactoryCustomizationDescriptor_Material>(Material.LoadSynchronous())->GetBuildableMap())
		{
			TSubclassOf<AFGBuildable> Buildable = GetDefault<UFGBuildingDescriptor>(UFGRecipe::GetDescriptorForRecipe(Recipe))->mBuildableClass;
			if (!BuildableBuildLayerDataOverrideMap.Contains(Buildable.Get()))
			{
				BuildableBuildLayerDataOverrideMap.Add(Buildable.Get(), LayerData);
			}
		}
	}
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

		const TSoftClassPtr<AFGBuildable>* RedirectClass = BuildableClassRedirectMap.Find(OriginalBuildableClass.Get());
		const TSoftClassPtr<AFGBuildable> BuildableClass = RedirectClass ? *RedirectClass : OriginalBuildableClass;

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


void UCartographGameInstanceModule::InstallHooks()
{
	if (WITH_EDITOR)
	{
		return;
	}

	const auto LambdaAfterAddBuildable =
		[](AFGBuildableSubsystem* ClassInstance, AFGBuildable* Buildable)
		{
			if (auto* BuildingDataSubsystem = ACartographBuildingDataSubsystem::Instance)
			{
				BuildingDataSubsystem->OnAddBuildable(Buildable);
			}
		};
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableSubsystem, AddBuildable, LambdaAfterAddBuildable);

	const auto LambdaAfterAddFromBuildableInstanceData =
		[](int32 ReturnValue, AFGLightweightBuildableSubsystem* ClassInstance, TSubclassOf<AFGBuildable> BuildableClass,
		   FRuntimeBuildableInstanceData& BuildableInstanceData, bool FromSaveData = false, int32 SaveDataBuildableIndex = INDEX_NONE,
		   uint16 ConstructId = MAX_uint16, AActor* BuildEffectInstigator = nullptr, int32 BlueprintBuildEffectIndex = INDEX_NONE)
		{
			if (auto* BuildingDataSubsystem = ACartographBuildingDataSubsystem::Instance)
			{
				BuildingDataSubsystem->OnAddFromBuildableInstanceData(BuildableClass, BuildableInstanceData, FromSaveData);
			}
		};
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, AddFromBuildableInstanceData, LambdaAfterAddFromBuildableInstanceData);

	const auto LambdaAfterAddFromReplicatedData =
		[](AFGLightweightBuildableSubsystem* ClassInstance, TSubclassOf<AFGBuildable> BuildableClass, TSubclassOf<UFGRecipe> BuiltWithRecipe,
		   const FLightweightBuildableReplicationItem& ReplicationData, int32 MaxSize,
		   AActor* BuildEffectInstigator, int32 BlueprintBuildIndex)
		{
			if (auto* BuildingDataSubsystem = ACartographBuildingDataSubsystem::Instance)
			{
				BuildingDataSubsystem->OnAddFromReplicatedData(BuildableClass, ReplicationData);
			}
		};
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, AddFromReplicatedData, LambdaAfterAddFromReplicatedData);

	const auto LambdaAfterRemoveBuildable =
		[](AFGBuildableSubsystem* ClassInstance, AFGBuildable* Buildable)
		{
			if (auto* BuildingDataSubsystem = ACartographBuildingDataSubsystem::Instance)
			{
				BuildingDataSubsystem->OnRemoveBuildable(Buildable);
			}
		};
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableSubsystem, RemoveBuildable, LambdaAfterRemoveBuildable);

	const auto LambdaAfterInvalidateRuntimeInstanceDataForIndex =
		[](AFGLightweightBuildableSubsystem* ClassInstance, TSubclassOf<AFGBuildable> BuildableClass, int32 Index)
		{
			if (auto* BuildingDataSubsystem = ACartographBuildingDataSubsystem::Instance)
			{
				BuildingDataSubsystem->OnInvalidateRuntimeInstanceDataForIndex(ClassInstance, BuildableClass, Index);
			}
		};
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, InvalidateRuntimeInstanceDataForIndex, LambdaAfterInvalidateRuntimeInstanceDataForIndex);
}


void UCartographGameInstanceModule::LoadRuntimeConfig()
{
#if !UE_SERVER
	const FCartograph_ConfigStruct ConfigInstance = FCartograph_ConfigStruct::GetActiveConfig(GetWorld());

	RuntimeConfig = {};

	// std::getline doesn't support std::string_view :(
	{
		std::wstringstream Stream{ *ConfigInstance.MainCategoryToggle };
		std::wstring Line;
		while (std::getline(Stream, Line, L','))
		{
			if (!Line.empty())
			{
				RuntimeConfig.DisabledLayerMainCategory.Add(FName{ Line.data() });
			}
		}
	}
	{
		std::wstringstream Stream{ *ConfigInstance.SubCategoryToggle };
		std::wstring MainCategoryLine;
		while (std::getline(Stream, MainCategoryLine, L'|'))
		{
			const size_t MainCategoryColonIndex = MainCategoryLine.find(':');
			if (MainCategoryColonIndex == std::wstring::npos)
			{
				CARTO_LOG_ERROR("Invalid BuildingToggle: %s", *ConfigInstance.BuildingToggle);
				break;
			}
			std::wstring MainCategoryName = MainCategoryLine.substr(0, MainCategoryColonIndex);
			const FName MainCategoryFName{ MainCategoryName.data() };

			std::wstringstream SubCategoryStream{ MainCategoryLine.substr(MainCategoryColonIndex + 1) };
			std::wstring SubCategoryLine;
			while (std::getline(SubCategoryStream, SubCategoryLine, L','))
			{
				if (!SubCategoryLine.empty())
				{
					RuntimeConfig.DisabledLayerSubCategory.FindOrAdd(MainCategoryFName).Add(FName{ SubCategoryLine.data() });
				}
			}
		}
	}

	{
		std::wstringstream Stream{ *ConfigInstance.BuildingToggle };
		std::wstring Line;
		while (std::getline(Stream, Line, L','))
		{
			if (!Line.empty())
			{
				RuntimeConfig.DisabledLayerBuildable.Add(std::stoul(Line));
			}
		}
	}

	CARTO_LOG_DEBUG("RuntimeConfig Loaded");
#endif
}


void UCartographGameInstanceModule::SaveRuntimeConfig()
{
#if !UE_SERVER
	const FConfigId ConfigId{ "Cartograph", "" };
	const UConfigManager* ConfigManager = GetWorld()->GetGameInstance()->GetSubsystem<UConfigManager>();
	const UConfigPropertySection* RootSection = ConfigManager->GetConfigurationRootSection(ConfigId);

	{
		const TObjectPtr<UConfigProperty>* MainCategoryProperty = RootSection->SectionProperties.Find("MainCategoryToggle");
		CARTO_LOG_ERROR_RETURN_IF_NULL(MainCategoryProperty);
		auto* MainCategoryStringProperty = Cast<UConfigPropertyString>(*MainCategoryProperty);
		CARTO_LOG_ERROR_RETURN_IF_NULL(MainCategoryStringProperty);

		TStringBuilder<500> Builder;
		for (const FName& Name : RuntimeConfig.DisabledLayerMainCategory)
		{
			Builder.Appendf(TEXT("%s,"), *Name.ToString());
		}
		MainCategoryStringProperty->Value = Builder.ToString();
		MainCategoryStringProperty->MarkDirty();
	}
	{
		const TObjectPtr<UConfigProperty>* SubCategoryProperty = RootSection->SectionProperties.Find("SubCategoryToggle");
		CARTO_LOG_ERROR_RETURN_IF_NULL(SubCategoryProperty);
		auto* SubCategoryStringProperty = Cast<UConfigPropertyString>(*SubCategoryProperty);
		CARTO_LOG_ERROR_RETURN_IF_NULL(SubCategoryStringProperty);
		TStringBuilder<1000> Builder;
		for (const auto& [MainCategory, SubCategories] : RuntimeConfig.DisabledLayerSubCategory)
		{
			Builder.Appendf(TEXT("%s:"), *MainCategory.ToString());
			for (const FName& Name : SubCategories)
			{
				Builder.Appendf(TEXT("%s,"), *Name.ToString());
			}
			Builder.Append(TEXT("|"));
		}
		SubCategoryStringProperty->Value = Builder.ToString();
		SubCategoryStringProperty->MarkDirty();
	}
	{
		const TObjectPtr<UConfigProperty>* BuildingProperty = RootSection->SectionProperties.Find("BuildingToggle");
		CARTO_LOG_ERROR_RETURN_IF_NULL(BuildingProperty);
		auto* BuildingStringProperty = Cast<UConfigPropertyString>(*BuildingProperty);
		CARTO_LOG_ERROR_RETURN_IF_NULL(BuildingStringProperty);
		TStringBuilder<11 * 551> Builder;
		for (const uint32& Hash : RuntimeConfig.DisabledLayerBuildable)
		{
			Builder.Appendf(TEXT("%u,"), Hash);
		}
		BuildingStringProperty->Value = Builder.ToString();
		BuildingStringProperty->MarkDirty();
	}

	CARTO_LOG_DEBUG("RuntimeConfig Saved");
#endif
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
void UCartographGameInstanceModule::ProcessOverrideData(TMap<KeyType, ValueType>& MapToBeOverriden, UClass* OverrideDataClass, const FName& PropertyName)
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
void UCartographGameInstanceModule::ProcessOverrideData(TSet<T>& SetToBeOverriden, UClass* OverrideDataClass, const FName& PropertyName)
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
    CARTO_LOG("Categories: %d", Num);
    for (int32 i = 0; i < Num; i++)
    {
		void* ElementPtr = ArrayProperty->GetValueAddressAtIndex_Direct(ArrayProperty->Inner, OverrideArray, i);
		FLayerCategoryData& OverrideValue = LayerCategories.AddDefaulted_GetRef();

		for (const auto& [OriginalStructProperty, OverrideStructProperty] : MatchingValueStructProperties)
		{
			auto* StructPropertyValue = OverrideStructProperty->ContainerPtrToValuePtr<void>(ElementPtr);
			OverrideStructProperty->CopyCompleteValue(OriginalStructProperty->ContainerPtrToValuePtr<void>(&OverrideValue), StructPropertyValue);
		}

		CARTO_LOG("MainCategory #%d", i);
        CARTO_LOG("Name: %s", *OverrideValue.Name.ToString());
        CARTO_LOG("DisplayName: %s", *OverrideValue.DisplayName.ToString());
        CARTO_LOG("Priority: %d", OverrideValue.Priority);

        if (!SubCategoriesArrayProperty)
        {
			continue;
        }

		void* SubCategoriesArray = SubCategoriesArrayProperty->ContainerPtrToValuePtr<void>(ElementPtr);
        const int32 SubCategoryNum = FScriptArrayHelper{ SubCategoriesArrayProperty, SubCategoriesArray }.Num();
		CARTO_LOG("SubCategories: %d", SubCategoryNum);
    	for (int32 j = 0; j < SubCategoryNum; j++)
		{
			const void* SubCategoriesElementPtr = SubCategoriesArrayProperty->GetValueAddressAtIndex_Direct(SubCategoriesArrayProperty->Inner, SubCategoriesArray, j);
			FLayerSubCategoryData& SubCategoryData = OverrideValue.SubCategories.AddDefaulted_GetRef();

			for (const auto& [OriginalStructProperty, OverrideStructProperty] : MatchingSubCategoriesStructProperties)
			{
				auto* StructPropertyValue = OverrideStructProperty->ContainerPtrToValuePtr<void>(SubCategoriesElementPtr);
				OverrideStructProperty->CopyCompleteValue(OriginalStructProperty->ContainerPtrToValuePtr<void>(&SubCategoryData), StructPropertyValue);
			}

            CARTO_LOG("SubCategory #%d", j);
            CARTO_LOG("Name: %s", *SubCategoryData.Name.ToString());
            CARTO_LOG("DisplayName: %s", *SubCategoryData.DisplayName.ToString());
            CARTO_LOG("Priority: %d", SubCategoryData.Priority);
		}
    }
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


#undef LOCTEXT_NAMESPACE
