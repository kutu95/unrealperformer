#pragma once



#include "UnrealPerformerApi.h"
#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "UnrealPerformerGodfreySettings.generated.h"



/**

 * Project settings for Godfrey PCM → ACE streaming (chunk size, burst override, diagnostics).

 * Edit under Project Settings → Plugins → Test Live Audio (Godfrey), or DefaultEngine.ini
 * [/Script/UnrealPerformer.UnrealPerformerGodfreySettings] (Config=Engine).

 */

UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "Unreal Performer (Godfrey / ACE)"))

class UNREAL_PERFORMER_API UUnrealPerformerGodfreySettings : public UDeveloperSettings

{

	GENERATED_BODY()



public:

	UUnrealPerformerGodfreySettings();



	/**

	 * Upper bound on PCM duration per AnimateFromAudioSamples sub-chunk inside PushPcm16Chunk (clamped 10–80 ms in code).

	 * The HTTP drain path uses the same value so chunk sizes stay aligned end-to-end.

	 */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "10.0", ClampMax = "80.0", UIMin = "10.0", UIMax = "80.0"))

	float AceMaxPcmPushChunkDurationMs = 55.f;



	/** If true, UACEBlueprintLibrary::OverrideA2F3DInferenceMode(true) runs when the game module starts (burst mode). */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest")

	bool bApplyAceBurstInferenceOverrideAtStartup = true;



	/** Max AnimateFromAudioSamples pushes per HTTP drain tick on the game thread. */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "8"))

	int32 GodfreyAcePrePlayPushBudgetPerTick = 6;



	/** When true, after Play() only: soft push-budget throttle when sent audio exceeds curves. Off = fast ingest (recommended for burst A2F). */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest")

	bool bGodfreyAcePaceIngestByCurveCatchUp = false;



	/** During playback: at or above this (sent − maxCurveTs), limit to 1 push per drain tick. */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "0.35", ClampMax = "1.5", UIMin = "0.4", UIMax = "0.8", EditCondition = "bGodfreyAcePaceIngestByCurveCatchUp"))

	float GodfreyAceMaxUnmatchedAudioSeconds = 0.65f;

	/** During playback: at or above this (sent − maxCurveTs), limit to half the configured push budget. */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "0.2", ClampMax = "1.0", UIMin = "0.25", UIMax = "0.5", EditCondition = "bGodfreyAcePaceIngestByCurveCatchUp"))
	float GodfreyAceSoftThrottleMediumUnmatchedSeconds = 0.45f;



	/** One line at FinishStream with utterance-relative timings. */

	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")

	bool bLogUtteranceLatencySummaryAtStreamFinish = true;



	/** At FinishStream: Godfrey-side startup summary (PCM vs OnAnimationStarted). */

	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")

	bool bLogGodfreyAceStartupCompletionSummary = true;



	/** Per AnimateFromAudioSamples sub-chunk: frames, nominal chunk ms, wall ms. */

	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")

	bool bLogPerAnimateChunkWallTime = false;

	/**
	 * Log parallel PCM + ACE audible state (mixer device, AudioComponent play state, procedural queue depth,
	 * underflows). Emits snapshots at parallel start, periodic ticks during playback, and FinishStream.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")
	bool bGodfreyLogAudiblePlaybackDiagnostics = true;



	/** Default silence duration for WarmupAcePipeline / UGodfreyAceWarmupComponent. */

	UPROPERTY(Config, EditAnywhere, Category = "Warmup", meta = (ClampMin = "0.1", ClampMax = "5.0"))

	float WarmupSilenceSeconds = 0.55f;



	/** If true, AllocateA2F3DResources at game startup (LocalA2F-Mark TRT prep). */

	UPROPERTY(Config, EditAnywhere, Category = "Warmup")

	bool bAllocateAceProviderResourcesAtGameStartup = true;



	UPROPERTY(Config, EditAnywhere, Category = "Warmup")

	FName GodfreyAceProviderNameForStartupAllocation = FName(TEXT("LocalA2F-Mark"));



	UPROPERTY(Config, EditAnywhere, Category = "Warmup", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))

	float GodfreyAceWarmupBeginPlayDelaySeconds = 0.2f;



	UPROPERTY(Config, EditAnywhere, Category = "Warmup")

	bool bAllocateAceProviderResourcesBeforeCharacterWarmup = true;



	UPROPERTY(Config, EditAnywhere, Category = "Warmup")

	bool bMuteAceAudioOutputDuringWarmup = true;



	/** Temporarily raise ACE BufferLengthInSeconds during each utterance (restored on finish/stop). */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming")

	bool bApplyGodfreyAceBufferLength = true;



	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (ClampMin = "0.05", ClampMax = "1.5", UIMin = "0.05", UIMax = "1.5"))

	float GodfreyAceBufferLengthSeconds = 0.35f;



	/** If >= 0, overrides MinBlendShapeSamplesBeforePlay for the utterance. Use -1 for ACE component default. */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (ClampMin = "-1", ClampMax = "64", UIMin = "-1", UIMax = "32"))

	int32 GodfreyAceMinBlendShapeSamplesOverride = 16;



	/** If >= 0, overrides MinCurveTimestampSecondsBeforePlay. Use -1 for ACE component default (recommended). */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (ClampMin = "-1", ClampMax = "3", UIMin = "-1", UIMax = "2"))

	float GodfreyAceMinCurveTimestampBeforePlay = 1.05f;

	/**
	 * Burst A2F: block ACE Play() until FinishStream/EndAudioSamples so the full curve batch exists before
	 * audible playback. Off by default — adds startup delay equal to full download + A2F burst on long clips.
	 * Use when lip sync quality matters more than time-to-first-word on long monolithic replies.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming")
	bool bGodfreyAceHoldPlayUntilStreamEnd = false;

	/** While bGodfreyAceHoldPlayUntilStreamEnd is active during ingest, MinCurveTimestampSecondsBeforePlay uses this impossibly high value. */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (ClampMin = "100.0", ClampMax = "100000.0", EditCondition = "bGodfreyAceHoldPlayUntilStreamEnd"))
	float GodfreyAceHoldPlayMinCurveTimestampGate = 99999.f;



	/**

	 * After FinishStream, keep OnAnimationStarted bound for up to this many seconds so late ACE callbacks still fire.

	 */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Lifecycle", meta = (ClampMin = "0.0", ClampMax = "60.0", UIMin = "0.0", UIMax = "30.0"))

	float GodfreyAcePostFinishOnAnimationStartedDelegateGraceSeconds = 10.f;

	/**
	 * Route audible PCM through WavUrl-style USoundWaveProcedural + PlaySound2D at ACE OnAnimationStarted.
	 * ACE internal procedural audio is used for lip-sync curves; this path is the reliable audible output
	 * (matches Test_Live_Audio WavUrlSoundLibrary). When false, only ACE internal playback is used.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming")
	bool bGodfreyUseParallelPcmAudiblePlayback = true;

	/**
	 * When parallel audible playback is active, set ACE Volume=0 after the parallel AudioComponent
	 * confirms IsPlaying (lip-sync curves only on ACE). Off by default so ACE remains audible if
	 * parallel SpawnSound2D fails or starves.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (EditCondition = "bGodfreyUseParallelPcmAudiblePlayback"))
	bool bGodfreyMuteAceWhenParallelAudibleStarts = false;

	/** Upsample 24 kHz brain PCM to 48 kHz before parallel audible playback (matches default AudioMixer platform rate). */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming")
	bool bGodfreyUpsamplePcmToMixerRate = true;

	/**
	 * Start WavUrl-style audible playback at FinishStream (full HTTP buffer, no ACE clock skip).
	 * Matches Test_Live_Audio: download complete -> QueueAudio -> Play. Lip-sync still follows ACE OnAnimationStarted.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (EditCondition = "bGodfreyUseParallelPcmAudiblePlayback"))
	bool bGodfreyPlayAudibleAtFinishStream = true;

	/**
	 * Spawn audible at the player camera with bIsUISound=false (non-UI). Falls back to PlaySound2D if spawn fails.
	 * Use when UISound procedural playback is inaudible in PIE (buffer drains but speakers stay silent).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (EditCondition = "bGodfreyUseParallelPcmAudiblePlayback"))
	bool bGodfreyAudibleSpawnAtPlayerLocation = true;

	virtual FName GetCategoryName() const override;

};


