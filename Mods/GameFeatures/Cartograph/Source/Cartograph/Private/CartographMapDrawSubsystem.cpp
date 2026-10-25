#include "CartographMapDrawSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Misc/OutputDeviceNull.h"

#include "CartographHelpers.h"
#include "Blueprint/WidgetTree.h"


ACartographMapDrawSubsystem::ACartographMapDrawSubsystem()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnClient;
    bReplicates = false;
}


void ACartographMapDrawSubsystem::BeginPlay()
{
	Super::BeginPlay();

    UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, { 0, 0, 0, 0 });

    Instance = this;
}

void ACartographMapDrawSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Instance = nullptr;

	Super::EndPlay(EndPlayReason);
}

void ACartographMapDrawSubsystem::OnZFilterUpdated(const float Min, const float Max)  // [0, 1]
{
	MinCached = Min;
	MaxCached = Max;

	const float Length = MaxHeight - MinHeight;
	MinZFilter = FMath::Floor(Min * Length + MinHeight);
	MaxZFilter = FMath::CeilToInt(Max * Length + MinHeight);

	CARTO_LOG("Min is now %f and max is now %f", MinZFilter, MaxZFilter);

	if (!IsInitializing)
	{
		RedrawMap();
	}
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

