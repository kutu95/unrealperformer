#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#ifndef UNREAL_PERFORMER_API
#define UNREAL_PERFORMER_API
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogUnrealPerformerModule, Log, All);

class FUnrealPerformerGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override;
};
