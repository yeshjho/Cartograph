#include "CartographMapDrawSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Misc/OutputDeviceNull.h"
#include "NiagaraComponent.h"

#include "FGBuildSubCategory.h"

#include "CartographGameInstanceModule.h"
#include "CartographHelpers.h"
#include "CartographSceneCaptureComponent2D.h"


namespace
{
	constexpr bool DEBUG_NIAGARA_DRAW = true;
}


ACartographMapDrawSubsystem::ACartographMapDrawSubsystem()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnClient;
	bReplicates = false;
}


void ACartographMapDrawSubsystem::MarkMapTextureDirty()
{
	if (IsMapBeingRendered)
	{
		RedrawMap();
	}
	else
	{
		IsMapTextureDirty = true;
	}
}


UNiagaraComponent* ACartographMapDrawSubsystem::GetNiagaraComponentPerType(const uint32 ClassHash)
{
	if (const TObjectPtr<UNiagaraComponent>* NiagaraComponentPtr = NiagaraComponentPerType.Find(ClassHash))
	{
		return *NiagaraComponentPtr;
	}

	const auto* GameInstanceModule = UCartographGameInstanceModule::Instance;
	CARTO_LOG_ERROR_DO_IF_NULL(GameInstanceModule, return nullptr);

	const TSoftClassPtr<AFGBuildable>* ClassPtr = GameInstanceModule->ClassIDToClassPtrMap.Find(ClassHash);
	CARTO_LOG_ERROR_DO_IF_NULL(ClassPtr, return nullptr);

	UNiagaraComponent* Niagara = NewObject<UNiagaraComponent>(NiagaraActor);
	NiagaraActor->AddInstanceComponent(Niagara);
	Niagara->SetupAttachment(NiagaraActorRoot);

	Niagara->SetAsset(GameInstanceModule->NiagaraSystem.LoadSynchronous());
	Niagara->SetAutoActivate(false);
	if (!DEBUG_NIAGARA_DRAW)
	{
		Niagara->SetOnlyOwnerSee(true);
	}
	Niagara->SetWorldLocation(FVector::ZeroVector);

	Niagara->RegisterComponent();
	Niagara->Activate(true);

	if (GameInstanceModule->SplineBuildables.Contains(ClassHash))
	{
		const FSplineData* SplineData = GameInstanceModule->BuildableSplineDataMap.Find(*ClassPtr);
		if (!SplineData && GameInstanceModule->ModdedBuildings.Contains(*ClassPtr))
		{
			SplineData = &GameInstanceModule->UnspecifiedSplineData;
		}
		if (!SplineData)
		{
			CARTO_LOG_WARNING("Can't find spline data for %s", *ClassPtr->LoadSynchronous()->GetName());
			return nullptr;
		}

		Niagara->SetColorParameter("User.Color", SplineData->Color);
		Niagara->SetFloatParameter("User.Thickness", SplineData->Thickness);
	}
	else
	{
		const FCategoryData* CategoryData = GameInstanceModule->GetDataByBuildableClass(
			GameInstanceModule->BuildableBuildCategoryDataOverrideMap,
			GameInstanceModule->BuildCategoryDataMap,
			*ClassPtr);
		if (!CategoryData && GameInstanceModule->ModdedBuildings.Contains(*ClassPtr))
		{
			CategoryData = &GameInstanceModule->UnspecifiedCategoryData;
		}
		if (!CategoryData)
		{
			CARTO_LOG_WARNING("Can't find category data for %s", *ClassPtr->LoadSynchronous()->GetName());
			return nullptr;
		}

		if (GameInstanceModule->VariableSizeBuildable.Contains(*ClassPtr))
		{
			Niagara->SetVectorParameter("User.Extent", FVector::OneVector);  // Will be driven by scale
		}
		else
		{
			Niagara->SetVectorParameter("User.Extent", Cartograph::GetBuildingSize(ClassHash));
		}

		Niagara->SetColorParameter("User.FillColor", CategoryData->MainColor);
		Niagara->SetColorParameter("User.BorderColor", CategoryData->OutlineColor);
		Niagara->SetFloatParameter("User.BorderThickness", CategoryData->OutlineThickness);
	}

	CARTO_LOG("Created a Niagara component for type %u", ClassHash);
	NiagaraComponentPerType.Add(ClassHash, Niagara);

	return Niagara;
}


void ACartographMapDrawSubsystem::OnMapOpen()
{
	IsMapBeingRendered = true;

	if (IsMapTextureDirty)
	{
		RedrawMap();
	}
}


void ACartographMapDrawSubsystem::OnMapClose()
{
	IsMapBeingRendered = false;
}


void ACartographMapDrawSubsystem::OnMapDisplaySizedChanged(const FVector2D& DisplaySizePixels)
{
	static constexpr int32 MAX_RENDER_TARGET_DIMENSION = 1024;
	static constexpr float RENDER_TARGET_SUPERSAMPLING = 1.5f;

	const FVector2D DesiredSize = DisplaySizePixels * RENDER_TARGET_SUPERSAMPLING;

	const float Scale = FMath::Min(
		1.f,
		static_cast<float>(MAX_RENDER_TARGET_DIMENSION) / FMath::Max(DesiredSize.X, DesiredSize.Y)
	);

	const FIntPoint NewSize{
		FMath::Max(1, static_cast<int>(FMath::RoundToInt(DesiredSize.X * Scale))),
		FMath::Max(1, static_cast<int>(FMath::RoundToInt(DesiredSize.Y * Scale)))
	};

	if (RenderTarget &&
		RenderTarget->SizeX == NewSize.X && RenderTarget->SizeY == NewSize.Y)
	{
		return;
	}

	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("CartographMapRenderTarget"), RF_Transient);
		RenderTarget->ClearColor = FLinearColor::Transparent;
		RenderTarget->bAutoGenerateMips = false;
		RenderTarget->Filter = TF_Bilinear;
		RenderTarget->InitCustomFormat(NewSize.X, NewSize.Y, PF_B8G8R8A8, false);

		SceneCaptureComponent->TextureTarget = RenderTarget;
	}
	else
	{
		RenderTarget->ResizeTarget(NewSize.X, NewSize.Y);
	}

	MarkMapTextureDirty();
}


void ACartographMapDrawSubsystem::BeginPlay()
{
	Super::BeginPlay();

    Instance = this;

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	NiagaraActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	NiagaraActor->SetReplicates(false);
	NiagaraActor->SetActorEnableCollision(false);
	NiagaraActor->SetActorTickEnabled(false);
	NiagaraActor->SetCanBeDamaged(false);

	NiagaraActorRoot = NewObject<USceneComponent>(NiagaraActor, TEXT("NiagaraRoot"));

	NiagaraActor->SetRootComponent(NiagaraActorRoot);
	NiagaraActor->AddInstanceComponent(NiagaraActorRoot);
	NiagaraActorRoot->RegisterComponent();


	SceneCaptureComponent = NewObject<UCartographSceneCaptureComponent2D>(NiagaraActor, "SceneCaptureComponent");
	NiagaraActor->AddInstanceComponent(SceneCaptureComponent);
	SceneCaptureComponent->SetupAttachment(NiagaraActorRoot);

	SceneCaptureComponent->ProjectionType = ECameraProjectionMode::Type::Orthographic;

	SceneCaptureComponent->bCaptureEveryFrame = false;
	SceneCaptureComponent->bCaptureOnMovement = false;

	SceneCaptureComponent->bAutoCalculateOrthoPlanes = false;
	SceneCaptureComponent->bUpdateOrthoPlanes = false;

	SceneCaptureComponent->bUseCustomProjectionMatrix = true;

	SceneCaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;

	SceneCaptureComponent->SetWorldRotation(FRotator{ -90.f, -90.f, 0.f });

	SceneCaptureComponent->RegisterComponent();

	CARTO_LOG("CartographMapDrawSubsystem BeginPlay, created Niagara actor and scene capture component");
}


void ACartographMapDrawSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Instance = nullptr;

	NiagaraActor->Destroy();

	CARTO_LOG("CartographMapDrawSubsystem EndPlay, destroyed Niagara actor");

	Super::EndPlay(EndPlayReason);
}


void ACartographMapDrawSubsystem::RedrawMap()
{
	if (!RenderTarget)
	{
		return;
	}

	constexpr float Z_PADDING = 100.f;
	constexpr float WORLD_CM_PER_PIXEL_AT_ZOOM_1_X = Cartograph::MAP_WIDTH_CENTIMETERS / 1024.f;
	constexpr float WORLD_CM_PER_PIXEL_AT_ZOOM_1_Y = Cartograph::MAP_HEIGHT_CENTIMETERS / 1024.f;

	const int32 TargetWidth = RenderTarget->SizeX;
	const int32 TargetHeight = RenderTarget->SizeY;

	const FVector2D WorldCmPerPixel = FVector2D{ WORLD_CM_PER_PIXEL_AT_ZOOM_1_X, WORLD_CM_PER_PIXEL_AT_ZOOM_1_Y } / ZoomScale;

	const float OrthoWidth = WorldCmPerPixel.X * TargetWidth;
	const float OrthoHeight = WorldCmPerPixel.Y * TargetHeight;

	const FVector2D WorldCenter{
		Cartograph::MAP_CENTER_WORLD_COORD_X + MapCenterWorldPosition.X * WORLD_CM_PER_PIXEL_AT_ZOOM_1_X,
		Cartograph::MAP_CENTER_WORLD_COORD_Y + MapCenterWorldPosition.Y * WORLD_CM_PER_PIXEL_AT_ZOOM_1_Y
	};

	const float CameraZ = MaxZFilter + Z_PADDING;

	SceneCaptureComponent->SetWorldLocation(FVector{ WorldCenter.X, WorldCenter.Y, CameraZ });

	SceneCaptureComponent->OrthoWidth = OrthoWidth;

	FMinimalViewInfo ProjectionInfo;
	ProjectionInfo.ProjectionMode = ECameraProjectionMode::Orthographic;
	ProjectionInfo.OrthoWidth = OrthoWidth;
	ProjectionInfo.AspectRatio = OrthoWidth / OrthoHeight;
	ProjectionInfo.bConstrainAspectRatio = false;

	ProjectionInfo.OrthoNearClipPlane = Z_PADDING;
	ProjectionInfo.OrthoFarClipPlane = CameraZ - MinZFilter;

	ProjectionInfo.bAutoCalculateOrthoPlanes = false;
	ProjectionInfo.bUpdateOrthoPlanes = false;

	SceneCaptureComponent->CustomProjectionMatrix = ProjectionInfo.CalculateProjectionMatrix();

	SceneCaptureComponent->ClearShowOnlyComponents();
	SceneCaptureComponent->ShowOnlyActorComponents(this);

	if (auto* GameInstanceModule = UCartographGameInstanceModule::Instance)
	{
		const FRuntimeConfig& RuntimeConfig = GameInstanceModule->RuntimeConfig;
		for (const auto& [ClassHash, NiagaraComponent] : NiagaraComponentPerType)
		{
			if (RuntimeConfig.DisabledLayerBuildable.Contains(ClassHash))
			{
				SceneCaptureComponent->RemoveShowOnlyComponent(NiagaraComponent);
				continue;
			}

			const auto* LayerData = GameInstanceModule->GetBuildLayerData(ClassHash);
			if (!LayerData)
			{
				continue;
			}

			if (RuntimeConfig.DisabledLayerMainCategory.Contains(LayerData->MainCategoryCache))
			{
				SceneCaptureComponent->RemoveShowOnlyComponent(NiagaraComponent);
			}
			else if (const TSet<FName>* SubCategories = RuntimeConfig.DisabledLayerSubCategory.Find(LayerData->MainCategoryCache);
					 SubCategories->Contains(LayerData->SubCategoryCache))
			{
				SceneCaptureComponent->RemoveShowOnlyComponent(NiagaraComponent);
			}
		}
	}

	SceneCaptureComponent->CaptureSceneDeferred();

	IsMapTextureDirty = false;
}


void ACartographMapDrawSubsystem::OnZFilterUpdated(const float Min, const float Max)  // [0, 1]
{
	constexpr float MAP_HEIGHT_METERS = Cartograph::MAX_HEIGHT_METERS - Cartograph::MIN_HEIGHT_METERS;

	MinZFilter = FMath::Floor(Min * MAP_HEIGHT_METERS + Cartograph::MIN_HEIGHT_METERS);
	MaxZFilter = FMath::CeilToInt(Max * MAP_HEIGHT_METERS + Cartograph::MIN_HEIGHT_METERS);

	CARTO_LOG("Min is now %f and max is now %f", MinZFilter, MaxZFilter);

	MarkMapTextureDirty();
}


void ACartographMapDrawSubsystem::OnCartographMenuButtonClicked(UUserWidget* Widget, const bool IsOpen)
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


void ACartographMapDrawSubsystem::OnVanillaMapMenuShown(const UUserWidget* Widget) const
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


void ACartographMapDrawSubsystem::OnZoomScaleChanged(const float NewScale)
{
	ZoomScale = NewScale;
	MarkMapTextureDirty();
}


void ACartographMapDrawSubsystem::OnPanChanged(const FVector2D NewMapCenterWorldPosition)
{
	MapCenterWorldPosition = NewMapCenterWorldPosition;
	MarkMapTextureDirty();
}
