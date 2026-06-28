#include "GodfreyAceWarmupComponent.h"

#include "ACEAudioCurveSourceComponent.h"
#include "ACEBlueprintLibrary.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GodfreyPcmStreamSession.h"
#include "UnrealPerformerGodfreySettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyAceWarmup, Log, All);

UGodfreyAceWarmupComponent::UGodfreyAceWarmupComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGodfreyAceWarmupComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bWarmupOnBeginPlay)
	{
		return;
	}

	ScheduleWarmup();
}

void UGodfreyAceWarmupComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WarmupDelayTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UGodfreyAceWarmupComponent::ScheduleWarmup()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		ExecuteWarmup();
		return;
	}

	const float Delay = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAceWarmupBeginPlayDelaySeconds;
	if (Delay > 0.f)
	{
		World->GetTimerManager().SetTimer(
			WarmupDelayTimerHandle,
			FTimerDelegate::CreateUObject(this, &UGodfreyAceWarmupComponent::ExecuteWarmup),
			Delay,
			false);
	}
	else
	{
		ExecuteWarmup();
	}
}

void UGodfreyAceWarmupComponent::ExecuteWarmup()
{
	WarmupDelayTimerHandle.Invalidate();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UACEAudioCurveSourceComponent* AceComp = Owner->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		UE_LOG(LogGodfreyAceWarmup, Warning, TEXT("GodfreyAceWarmupComponent: no UACEAudioCurveSourceComponent on %s; skipping WarmupAcePipeline."), *Owner->GetName());
		return;
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (Settings->bAllocateAceProviderResourcesBeforeCharacterWarmup)
	{
		UACEBlueprintLibrary::AllocateA2F3DResources(AceProviderName);
	}

	const float SilenceSeconds = Settings->WarmupSilenceSeconds;
	const bool bMute = Settings->bMuteAceAudioOutputDuringWarmup;
	const float SavedVolume = AceComp->Volume;
	if (bMute)
	{
		AceComp->Volume = 0.f;
	}

	const bool bWarmupOk = UGodfreyPcmStreamSession::WarmupAcePipeline(Owner, AceProviderName, WarmupSampleRate, SilenceSeconds);

	if (bMute)
	{
		AceComp->Volume = SavedVolume;
	}

	// Release warmup AudioComponent so it cannot hold the procedural mixer path during real speech.
	AceComp->Stop();

	if (!bWarmupOk)
	{
		UE_LOG(LogGodfreyAceWarmup, Warning,
			TEXT("GodfreyAceWarmupComponent: WarmupAcePipeline returned false on %s (check ACE logs)."),
			*Owner->GetName());
	}
}
