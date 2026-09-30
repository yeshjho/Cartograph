using UnrealBuildTool;

public class Cartograph : ModuleRules
{
	public Cartograph(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// FactoryGame transitive dependencies
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject",
			"Engine",
			"DeveloperSettings",
			"PhysicsCore",
			"InputCore",
			"GeometryCollectionEngine",
			"AnimGraphRuntime",
			"AssetRegistry",
			"NavigationSystem",
			"AIModule",
			"GameplayTasks",
			"SlateCore", "Slate", "UMG",
			"RenderCore",
			"CinematicCamera",
			"Foliage",
			"NetCore",
			"GameplayTags",
			"Json", "JsonUtilities"
		});

		// Header stubs
		PublicDependencyModuleNames.Add("DummyHeaders");

		if (Target.Type == TargetRules.TargetType.Editor) {
			PublicDependencyModuleNames.Add("AnimGraph");
		}
		PublicDependencyModuleNames.AddRange(new string[] {"FactoryGame", "SML"});

		PrivateDependencyModuleNames.Add("RHI");
	}
}
