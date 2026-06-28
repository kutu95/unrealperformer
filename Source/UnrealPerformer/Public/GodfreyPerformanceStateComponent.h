#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyPerformanceTypes.h"
#include "GodfreyPerformanceStateComponent.generated.h"

/**
 * Godfrey Performer v1 — Blueprint-facing behaviour event bus for exhibition characters.
 *
 * How to use from Blueprint (recommended wiring):
 * - Add this component to BP_Godfrey / BP_Gavin (or any actor passed as CharacterForAce to StreamGodfreySpeechToAudio).
 * - The Godfrey Brain async action already calls NotifyUtteranceStarted / NotifyUtteranceEnded / NotifyPerformanceCue on this component when present.
 * - Bind OnPerformanceStateChanged for AnimBP / layered state machines; bind OnListeningStarted, OnSpeakingStarted, etc. for one-shot montages or additive slots.
 * - For STT / local UX, call BeginListening / BeginThinking from your UI or voice subsystem.
 * - When bAutoSpeakingStateFromUtterance is true (default), NotifyUtteranceStarted/Ended from the speech stream drive BeginSpeaking/EndSpeaking so the animation bridge receives OnSpeakingStarted without Blueprint wiring.
 * - Performance cues: with bRoutePerformanceCuesToStates enabled (default), known cue "type" strings from the Brain JSON are mapped to Begin / Trigger helpers; unknown types still raise OnPerformanceCueReceived so you can branch in BP.
 *
 * This component does not drive ACE, PCM streaming, or Control Rig — keep procedural face/body in MetaHuman layers; react here with montages, look targets, and attention logic.
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyPerformanceStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyPerformanceStateComponent();

	// --- Explicit v1 orchestration API (preferred names for Blueprint graphs) ---

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void BeginListening();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void BeginThinking();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void BeginSpeaking();

	/** Leaves Speaking (no-op if not Speaking). Fires OnSpeakingEnded; default transition is Idle — bind and call BeginListening if you want mic-open posture instead. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void EndSpeaking();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void ReturnToIdle();

	/** Enters Emphasising; if already Emphasising, still broadcasts OnEmphasisTriggered so montages can retrigger. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void TriggerEmphasis();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void TriggerAmused();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void TriggerSerious();

	// --- Original state API (kept for compatibility; forwards into the same state machine) ---

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	bool TrySetPerformanceState(EGodfreyPerformanceState NewState);

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterIdle();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterListening();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterThinking();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterSpeaking();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterEmphasising();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterSerious();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterAmused();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void ResetToIdle();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void NotifyUtteranceStarted();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void NotifyUtteranceEnded();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void NotifyPerformanceCue(const FString& CueType, const FString& CueValue, const FString& RawCue);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Performance")
	EGodfreyPerformanceState GetPerformanceState() const { return PerformanceState; }

	/** When true, NotifyPerformanceCue maps well-known Brain cue types to BeginX / TriggerX helpers (see cpp). Unknown types are only logged and forwarded via OnPerformanceCueReceived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer")
	bool bRoutePerformanceCuesToStates = true;

	/**
	 * When true, ACE/stream utterance hooks also drive the performance Speaking state (BeginSpeaking on start, EndSpeaking on end).
	 * Keeps UGodfreyPerformerAnimationBridgeComponent in sync during real playback without Blueprint wiring on BP_Gavin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer")
	bool bAutoSpeakingStateFromUtterance = true;

	// --- Delegates (behaviour event bus) ---

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformanceStateChangedEvent OnPerformanceStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnListeningStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnThinkingStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnSpeakingStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnSpeakingEnded;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnReturnedToIdle;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnEmphasisTriggered;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnAmusedTriggered;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnSeriousTriggered;

	/** Raw cue from Godfrey Brain (type/value/raw JSON fragment). Always fired from NotifyPerformanceCue after routing pass. */
	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerCueEvent OnPerformanceCueReceived;

	/** PCM / lipsync utterance hooks (from async stream); distinct from performance Speaking state — wire both if you want mouth open on audio and body state separately. */
	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performance")
	FGodfreyUtteranceLifecycleEvent OnGodfreyUtteranceStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performance")
	FGodfreyUtteranceLifecycleEvent OnGodfreyUtteranceEnded;

protected:
	virtual void BeginPlay() override;

private:
	void ApplyPerformanceState(EGodfreyPerformanceState NewState);
	void DispatchEnteredStateDelegates(EGodfreyPerformanceState NewState, EGodfreyPerformanceState PreviousState);
	static FString NormalizeCueToken(const FString& In);
	bool TryConsumePerformanceCueForRouting(const FString& CueType, const FString& CueValue);

	UPROPERTY(Transient)
	EGodfreyPerformanceState PerformanceState = EGodfreyPerformanceState::Idle;
};
