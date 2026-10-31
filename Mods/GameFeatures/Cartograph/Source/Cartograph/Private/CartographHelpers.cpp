#include "CartographHelpers.h"

#include "Engine/InheritableComponentHandler.h"

#include "CartographGameInstanceModule.h"


DEFINE_LOG_CATEGORY(LogCartograph)


bool Cartograph::IsBuildableIgnored(const TSoftClassPtr<AFGBuildable>& Type)
{
	CARTO_LOG_ERROR_DO_IF_NULL(UCartographGameInstanceModule::Instance, return true);

	return UCartographGameInstanceModule::Instance->BuildableToIgnore.Contains(Type);
}


TSoftClassPtr<AFGBuildable> Cartograph::GetRedirectedBuildableClass(const TSoftClassPtr<AFGBuildable>& OriginalBuildableClass)
{
	CARTO_LOG_ERROR_DO_IF_NULL(UCartographGameInstanceModule::Instance, return OriginalBuildableClass);

	const auto& BuildableClassRedirectMap = UCartographGameInstanceModule::Instance->BuildableClassRedirectMap;
	const TSoftClassPtr<AFGBuildable>* RedirectClass = BuildableClassRedirectMap.Find(OriginalBuildableClass);
	return RedirectClass ? *RedirectClass : OriginalBuildableClass;
}


uint32 Cartograph::GetHashWithRedirection(const TSoftClassPtr<AFGBuildable>& OriginalBuildableClass)
{
	CARTO_LOG_ERROR_DO_IF_NULL(UCartographGameInstanceModule::Instance, return 0);

	const TSoftClassPtr<AFGBuildable> BuildableClass = GetRedirectedBuildableClass(OriginalBuildableClass);

	if (const uint32* Hash = UCartographGameInstanceModule::Instance->ClassPtrToClassIDMap.Find(BuildableClass.Get()))
	{
		return *Hash;
	}
	else
	{
		CARTO_LOG_WARNING("Class %s not found in ClassPtrToClassIDMap", *BuildableClass.LoadSynchronous()->GetName());
		return 0;
	}
}


FVector Cartograph::GetBuildingSize(const AFGBuildable* Buildable)
{
	const auto* GameInstanceModule = UCartographGameInstanceModule::Instance;
	CARTO_LOG_ERROR_DO_IF_NULL(GameInstanceModule, return {});

	TSoftClassPtr<AFGBuildable> BuildableClass = GetRedirectedBuildableClass(Buildable->GetClass());

	const auto& BuildableSizeOverrideMap = GameInstanceModule->BuildableSizeOverrideMap;
	if (const FVector* Size = BuildableSizeOverrideMap.Find(BuildableClass.Get()))
	{
		return *Size;
	}

	const bool ShouldCache = !GameInstanceModule->VariableSizeBuildable.Contains(BuildableClass);

	if (const FBox ClearanceBox = Buildable->GetCombinedClearanceBox();
		ClearanceBox.IsValid)
	{
		if (const FVector Size = ClearanceBox.GetSize();
			!Size.IsZero())
		{
			if (ShouldCache)
			{
				CARTO_LOG_DEBUG("Calculated and cached building size from Clearance Box: %s", *BuildableClass.LoadSynchronous()->GetName());
				UCartographGameInstanceModule::Instance->BuildableSizeOverrideMap.Add(BuildableClass, Size);
			}

			return Size;
		}
	}

	BuildableClass.LoadSynchronous();

	FBox Box;

	const auto* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(BuildableClass.Get());
	while (BlueprintGeneratedClass)
	{
		if (const UInheritableComponentHandler* InheritableHandler = BlueprintGeneratedClass->InheritableComponentHandler)
		{
			TArray<UActorComponent*> Templates;
			InheritableHandler->GetAllTemplates(Templates, true);
			for (const UActorComponent* ComponentTemplate : Templates)
			{
				if (const auto* Component = Cast<USceneComponent>(ComponentTemplate))
				{
					Box += Component->GetLocalBounds().GetBox();
				}
			}
		}

		if (const USimpleConstructionScript* ConstructionScript = BlueprintGeneratedClass->SimpleConstructionScript)
		{
			for (const USCS_Node* Node : ConstructionScript->GetAllNodes())
			{
				if (!Node)
				{
					continue;
				}

				if (const auto* Component = Cast<USceneComponent>(Node->ComponentTemplate))
				{
					Box += Component->GetLocalBounds().GetBox();
				}
			}
		}

		BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(BlueprintGeneratedClass->GetSuperClass());
	}

	UClass* NativeClass = BuildableClass.Get();
	while (NativeClass)
	{
		TArray<UObject*> DefaultObjectSubobjects;
		NativeClass->GetDefaultObjectSubobjects(DefaultObjectSubobjects);

		for (const UObject* DefaultSubObject : DefaultObjectSubobjects)
		{
			if (const auto* Component = Cast<USceneComponent>(DefaultSubObject))
			{
				Box += Component->GetLocalBounds().GetBox();
			}
		}

		NativeClass = NativeClass->GetSuperClass();
	}

	if (Box.IsValid)
	{
		if (const FVector Size = Box.GetSize();
			!Size.IsZero())
		{
			if (ShouldCache)
			{
				CARTO_LOG_DEBUG("Calculated and cached building size from USceneComponent Bounds: %s", *BuildableClass->GetName());
				UCartographGameInstanceModule::Instance->BuildableSizeOverrideMap.Add(BuildableClass, Size);
			}
			return Size;
		}
	}

	CARTO_LOG_WARNING("Can't find size for %s", *BuildableClass->GetName());
	if (ShouldCache)
	{
		// Cache anyway so that we don't waste time in the future
		UCartographGameInstanceModule::Instance->BuildableSizeOverrideMap.Add(BuildableClass, {});
	}
	return {};
}


FVector Cartograph::GetBuildingSize(const uint32 RedirectedClassHash)
{
	const auto* GameInstanceModule = UCartographGameInstanceModule::Instance;
	CARTO_LOG_ERROR_DO_IF_NULL(GameInstanceModule, return {});

	const TSoftClassPtr<AFGBuildable>* ClassPtr = GameInstanceModule->ClassIDToClassPtrMap.Find(RedirectedClassHash);
	CARTO_LOG_ERROR_DO_IF_NULL(ClassPtr, return {});

	if (GameInstanceModule->VariableSizeBuildable.Contains(*ClassPtr))
	{
		CARTO_LOG_ERROR("Size of %s is variable, should be called with an actual instance!", *ClassPtr->LoadSynchronous()->GetName());
		return {};
	}

	const auto* CDO = GetDefault<AFGBuildable>(ClassPtr->LoadSynchronous());
	if (!CDO)
	{
		CARTO_LOG_ERROR("Can't find CDO for %s", *ClassPtr->Get()->GetName());
		return {};
	}

	return GetBuildingSize(CDO);
}
