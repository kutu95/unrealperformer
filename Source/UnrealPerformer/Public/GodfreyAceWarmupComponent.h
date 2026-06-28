#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "GodfreyAceWarmupComponent.generated.h"

/**
 * Exhibition-oriented ACE pre-warm: optionally delays after BeginPlay, calls AllocateA2F3DResources (hint),
 * runs UGodfreyPcmStreamSession::WarmupAcePipeline (silent PCM + EndAudioSamples) on the same actor as live speech,
 * and can temporarily zero UACEAudioCurveSourceComponent::Volume to avoid audible warmup output.
 * Add to the Captain Godfrey actor that already has UACEAudioCurveSourceComponent (same provider/sample rate as live).
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyAceWarmupComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyAceWarmupComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warmup")
	bool bWarmupOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warmup")
	FName AceProviderName = FName(TEXT("LocalA2F-Mark"));

	/** Match exhibition TTS / PullQueuedGodfreySpeechToAudio (often 24000) so ACE warms the same procedural rate as live speech. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warmup", meta = (ClampMin = "8000", ClampMax = "48000"))
	int32 WarmupSampleRate = 24000;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ScheduleWarmup();
	void ExecuteWarmup();

	FTimerHandle WarmupDelayTimerHandle;
};
