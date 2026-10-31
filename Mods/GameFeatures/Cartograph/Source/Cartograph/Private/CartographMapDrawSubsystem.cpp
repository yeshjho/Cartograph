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

	constexpr float MAP_HEIGHT_METERS = Cartograph::MAX_HEIGHT_METERS - Cartograph::MIN_HEIGHT_METERS;
}


ACartographMapDrawSubsystem::ACartographMapDrawSubsystem()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnClient;
    bReplicates = false;

	CARTO_LOG_ERROR_RETURN_IF_NULL(UCartographGameInstanceModule::Instance);

	SceneCaptureComponent = CreateDefaultSubobject<UCartographSceneCaptureComponent2D>("SceneCaptureComponent");
	SceneCaptureComponent->ProjectionType = ECameraProjectionMode::Type::Orthographic;
	SceneCaptureComponent->bCaptureEveryFrame = false;
	SceneCaptureComponent->TextureTarget = UCartographGameInstanceModule::Instance->RenderTarget.LoadSynchronous();
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

	UNiagaraComponent* Niagara = NewObject<UNiagaraComponent>(this);
	Niagara->SetAsset(GameInstanceModule->NiagaraSystem.LoadSynchronous());
	Niagara->SetAutoActivate(false);
	if (!DEBUG_NIAGARA_DRAW)
	{
		Niagara->SetOnlyOwnerSee(true);
	}
	Niagara->SetupAttachment(RootComponent);

	Niagara->SetWorldLocation(FVector::ZeroVector);
	Niagara->bNeverDistanceCull = true;

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

		Niagara->SetVectorParameter("User.Extent", /*TODO: Have a static map*/);

		Niagara->SetColorParameter("User.FillColor", CategoryData->MainColor);
		Niagara->SetColorParameter("User.BorderColor", CategoryData->OutlineColor);
		Niagara->SetFloatParameter("User.BorderThickness", CategoryData->OutlineThickness);
	}

	NiagaraComponentPerType[ClassHash] = Niagara;

	return Niagara;
}


void ACartographMapDrawSubsystem::BeginPlay()
{
	Super::BeginPlay();

    Instance = this;
}


void ACartographMapDrawSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Instance = nullptr;

	Super::EndPlay(EndPlayReason);
}


void ACartographMapDrawSubsystem::RedrawMap()
{
	// TODO: Setup position, width, near/far plane according to pan, zoom, z filter
	SceneCaptureComponent->OrthoWidth;

	// TODO: Hide/Unhide types

	SceneCaptureComponent->CaptureSceneDeferred();
}


void ACartographMapDrawSubsystem::OnZFilterUpdated(const float Min, const float Max)  // [0, 1]
{
	MinZFilter = FMath::Floor(Min * MAP_HEIGHT_METERS + Cartograph::MIN_HEIGHT_METERS);
	MaxZFilter = FMath::CeilToInt(Max * MAP_HEIGHT_METERS + Cartograph::MAX_HEIGHT_METERS);

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


void ACartographMapDrawSubsystem::OnShowBuildingsCheckboxChanged(const bool DoShow)
{
	DoShowBuildings = DoShow;
	CARTO_LOG("DoShowBuildings: %d", DoShowBuildings);
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


void ACartographMapDrawSubsystem::OnMapOpen()
{
	IsMapBeingRendered = true;

	if (IsMapTextureDirty)
	{
		RedrawMap();
		IsMapTextureDirty = false;
	}
}


void ACartographMapDrawSubsystem::OnMapClose()
{
	IsMapBeingRendered = false;
}
