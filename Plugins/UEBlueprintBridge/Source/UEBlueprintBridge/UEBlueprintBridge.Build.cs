using UnrealBuildTool;
public class UEBlueprintBridge : ModuleRules
{
    public UEBlueprintBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "Json", "AssetRegistry", "BlueprintGraph", "AssetTools", "AnimGraph", "AnimGraphRuntime"
        });
    }
}
