using UnrealBuildTool;

public class UnrealPerformer : ModuleRules
{
	public UnrealPerformer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimGraphRuntime",
			"ControlRig",
			"InputCore",
			"HTTP",
			"Json",
			"JsonUtilities",
			"AudioMixer",
			"ACERuntime",
			"ACECore",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"MetaHumanSDKRuntime",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
