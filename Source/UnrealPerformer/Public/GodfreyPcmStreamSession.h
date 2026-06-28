#pragma once

#include "UnrealPerformerApi.h"
#include "CoreMinimal.h"
#include "TimerManager.h"
#include "UObject/Object.h"
#include "GodfreyAceStartupDiagnostics.h"
#include "GodfreyPcmStreamSession.generated.h"

class AActor;
class UACEAudioCurveSourceComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreyStreamSimpleEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGodfreyStreamErrorEvent, const FString&, ErrorMessage);

/**
 * Streams PCM16 into NVIDIA ACE (AnimateFromAudioSamples). Audible playback and curve sync are owned by
 * UACEAudioCurveSourceComponent's internal AudioComponent — no parallel procedural SoundWave playback.
 */
UCLASS(BlueprintType)
class UNREAL_PERFORMER_API UGodfreyPcmStreamSession : public UObject
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming", meta = (WorldContext = "WorldContextObject"))
	bool StartStream(UObject* WorldContextObject, AActor* CharacterForAce, FName ProviderName = FName("LocalA2F-Mark"), int32 SampleRate = 16000, int32 NumChannels = 1);

	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming")
	bool PushPcm16Chunk(const TArray<uint8>& PcmBytes, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming")
	bool FinishStream(FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming")
	void StopStream();

	/**
	 * Runs ~0.5s of silence through the same ACE path as streaming (AnimateFromAudioSamples + EndAudioSamples) to pay
	 * one-time costs (session / CUDA / model) before the first real utterance. Call from BeginPlay on the character.
	 * Silence duration should exceed DefaultEngine MaxInitialAudioChunkSize (often 0.5s) when testing non-burst pacing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming|Diagnostics")
	static bool WarmupAcePipeline(AActor* CharacterForAce, FName ProviderName = FName("LocalA2F-Mark"), int32 SampleRate = 16000, float SilenceDurationSeconds = 0.55f);

	/** Called by the streaming async action so latency summaries can include client request start. */
	void SetClientRequestT0PlatformSeconds(double PlatformSeconds);

	/** Called when the first HTTP body bytes reach the game-thread PCM pipeline (first FIFO batch). */
	void NotifyFirstHttpBodyBytesPlatformSeconds(double PlatformSeconds);

	UFUNCTION(BlueprintPure, Category = "Audio|Godfrey|Streaming")
	int32 GetBufferedPcmBytes() const { return RollingPcmBytes.Num(); }

	/** Caps push budget during playback when sent audio outruns curves (optional soft throttle; off by default). */
	int32 GetEffectiveIngestPushBudget(int32 ConfigBudget, bool bAllowOverrun = false) const;

	/** Fires when ACE internal playback/sync pipeline starts (UACEAudioCurveSourceComponent::OnAnimationStarted). */
	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamSimpleEvent OnPlaybackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamSimpleEvent OnLipSyncStarted;

	/** Fires when ACE audible playback completes (UACEAudioCurveSourceComponent::OnAnimationEnded), not when HTTP ingest finishes. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamSimpleEvent OnPlaybackEnded;

	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamSimpleEvent OnFinished;

	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamErrorEvent OnError;

private:
	bool ValidateFormat(const TArray<uint8>& PcmBytes, FString& OutError) const;
	void ReportError(const FString& ErrorMessage);
	void UnbindAceDelegates();
	void RegisterAsActiveAceSessionForCharacter();
	void UnregisterActiveAceSessionForCharacter();
	bool IsActiveAceSessionForCharacter() const;
	void CancelDeferredAceUnbind();
	void ScheduleDeferredAceUnbindAfterFinishStream(UWorld* World, double FinishStreamPlatformSeconds);
	void ProcessDeferredAceUnbindTick();
	void LogUtteranceLatencySummaryAtFinishIfEnabled(double FinishPlatformSeconds) const;
	void ApplyGodfreyAcePlaybackPriming(UACEAudioCurveSourceComponent* AceComp);
	void RestoreGodfreyAcePlaybackPrimingIfApplied();
	void LogGodfreyAceStartupCompletionSummary(double FinishPlatformSeconds) const;

	UFUNCTION()
	void HandleAceAnimationStarted();

	UFUNCTION()
	void HandleAceAnimationEnded();

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> TargetCharacter;

	UPROPERTY(Transient)
	TArray<uint8> RollingPcmBytes;

	FName AceProviderName = NAME_None;
	int32 StreamSampleRate = 0;
	int32 StreamNumChannels = 0;
	int64 TotalSamplesSentToAce = 0;
	bool bStreamStarted = false;
	bool bFinished = false;
	bool bLoggedFirstPcmChunk = false;
	bool bBoundAceAnimationStarted = false;
	bool bBoundAceAnimationEnded = false;
	bool bAcePlaybackEndedObserved = false;

	double FirstChunkWorldTimeSeconds = -1.0;
	double FirstChunkPlatformSeconds = -1.0;

	int32 UtteranceOrdinal = 0;
	double ClientRequestT0PlatformSeconds = -1.0;
	double StreamStartPlatformSeconds = -1.0;
	double FirstHttpBodyBytesPlatformSeconds = -1.0;
	double FirstAnimateSubchunkPlatformSeconds = -1.0;
	double FirstOnAnimationStartedPlatformSeconds = -1.0;

	bool bGodfreyAcePrimingApplied = false;
	bool bGodfreySavedAceBufferLength = false;
	float GodfreySavedAceBufferLengthInSeconds = 0.1f;
	bool bGodfreySavedAceMinBlend = false;
	int32 GodfreySavedAceMinBlendShapeSamplesBeforePlay = 1;
	bool bGodfreySavedAceMinCurveLead = false;
	float GodfreySavedAceMinCurveTimestampBeforePlay = 0.f;
	bool bGodfreyAceBufferLengthOverriddenThisUtterance = false;
	bool bGodfreyAceMinBlendOverriddenThisUtterance = false;
	bool bGodfreyAceMinCurveLeadOverriddenThisUtterance = false;
	FGodfreyAceUtteranceStartupMetrics UtteranceStartupMetrics;

	FTimerHandle DeferredAceUnbindTimerHandle;
	TWeakObjectPtr<UWorld> DeferredAceUnbindWorld;
	bool bDeferredAceUnbindActive = false;
	int32 DeferredUnbindUtteranceOrdinal = 0;
	double DeferredUnbindStartPlatformSeconds = 0.0;
	double DeferredUnbindFinishStreamPlatformSeconds = 0.0;
};
