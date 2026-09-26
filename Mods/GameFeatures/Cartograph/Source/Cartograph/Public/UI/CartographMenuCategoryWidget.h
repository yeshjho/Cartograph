#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CartographMenuCategoryWidget.generated.h"


UENUM(BlueprintType)
enum class ECategoryType : uint8
{
	Heading,
	MainCategory,
	SubCategory,
};


/**
 * 
 */
UCLASS()
class CARTOGRAPH_API UCartographMenuCategoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintImplementableEvent)
	void Initialize(ECategoryType CategoryType, const FText& Text);

	using Super::Initialize;  // Unhide

	UFUNCTION(BlueprintImplementableEvent)
	void PostInitialize();

	UFUNCTION(BlueprintImplementableEvent)
	void AddCategory(UWidget* Widget);

	UFUNCTION(BlueprintImplementableEvent)
	void AddItem(UWidget* Widget, bool ShouldFillUp);
	
	UFUNCTION(BlueprintImplementableEvent)
	void SetExpanded(bool DoExpand);


protected:
	UPROPERTY(BlueprintReadWrite)
	bool IsExpanded = true;
};
