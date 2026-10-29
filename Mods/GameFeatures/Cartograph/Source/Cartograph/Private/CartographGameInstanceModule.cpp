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


namespace
{
	void InstallHooks()
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
					BuildingDataSubsystem->OnAddFromBuildableInstanceData(BuildableClass, BuildableInstanceData, FromSaveData, ReturnValue);
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
					BuildingDataSubsystem->OnAddFromReplicatedData(ClassInstance, BuildableClass, ReplicationData);
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
					BuildingDataSubsystem->OnInvalidateRuntimeInstanceDataForIndex(BuildableClass, Index);
				}
			};
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, InvalidateRuntimeInstanceDataForIndex, LambdaAfterInvalidateRuntimeInstanceDataForIndex);
	}


	TSet<FTopLevelAssetPath> GetDerivedClassPaths(UClass* ParentClass)
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
}


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
		InstallHooks();
		break;
	}
}


void UCartographGameInstanceModule::OnLayerConfigChanged()
{
	CARTO_LOG_DEBUG("OnLayerConfigChanged");

	if (auto* MapDrawSubsystem = ACartographMapDrawSubsystem::Instance)
	{
		MapDrawSubsystem->MarkMapTextureDirty();
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
		if (!LayerData && ModdedBuildings.Contains(BuildableClass))
		{
			const FString& ModName = *ModdedBuildings.Find(BuildableClass);
			LayerData = ModdedBuildLayerData.Find(ModName);
		}
		if (!LayerData)
		{
			CARTO_LOG_WARNING("Can't find layer data for %s", *BuildableClass.LoadSynchronous()->GetName());
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
