#include "UnrealPerformer.h"

#include "ACEBlueprintLibrary.h"
#include "Modules/ModuleManager.h"
#include "UnrealPerformerGodfreySettings.h"

DEFINE_LOG_CATEGORY(LogUnrealPerformerModule);

void FUnrealPerformerGameModule::StartupModule()
{
	FDefaultGameModuleImpl::StartupModule();

	if (GetDefault<UUnrealPerformerGodfreySettings>()->bApplyAceBurstInferenceOverrideAtStartup)
	{
		UACEBlueprintLibrary::OverrideA2F3DInferenceMode(true);
		UE_LOG(LogUnrealPerformerModule, Log,
			TEXT("ACE burst mode enabled at startup (Project Settings)."));
	}

	const UUnrealPerformerGodfreySettings* GodfreySettings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (GodfreySettings->bAllocateAceProviderResourcesAtGameStartup)
	{
		UACEBlueprintLibrary::AllocateA2F3DResources(GodfreySettings->GodfreyAceProviderNameForStartupAllocation);
		UE_LOG(LogUnrealPerformerModule, Log,
			TEXT("ACE warmup: AllocateA2F3DResources(%s) at game startup."),
			*GodfreySettings->GodfreyAceProviderNameForStartupAllocation.ToString());
	}
}

IMPLEMENT_PRIMARY_GAME_MODULE(FUnrealPerformerGameModule, UnrealPerformer, "UnrealPerformer");
