#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyDirectSpeechComponent.generated.h"

class UAsyncActionStreamGodfreySpeech;
class UGodfreyPerformanceStateComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreyDirectSpeechSimpleEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGodfreyDirectSpeechErrorEvent, const FString&, ErrorMessage);

/**
 * Option 2 — direct Godfrey Brain streaming from Unreal (no exhibition queue poll).
 *
 * POST { text, sampleRate, numChannels } to /api/godfrey/speak/stream-pcm, PCM → ACE on CharacterForAce.
 * Add to BP_GodfreyApiTest (or kiosk controller), set CharacterForAce to BP_Gavin, disable the old PullQueued timer.
 *
 * Brain must be running at GodfreyBrainBaseUrl (default http://localhost:3000); no browser UI required.
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyDirectSpeechComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyDirectSpeechComponent();

	/** Send visitor / operator text to Godfrey Brain and stream the reply into ACE. Returns false if busy or text empty. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Direct Speech")
	bool AskGodfrey(const FString& PromptText);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Direct Speech")
	bool IsStreaming() const { return bIsStreaming; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech")
	FString GodfreyBrainBaseUrl = TEXT("http://localhost:3000");

	/** MetaHuman / actor with UACEAudioCurveSourceComponent. If unset, uses owner when it has ACE, else CharacterActorTag lookup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech")
	TObjectPtr<AActor> CharacterForAce = nullptr;

	/** When CharacterForAce is unset, first actor in the world with this tag (e.g. GodfreyCharacter on BP_Gavin). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech")
	FName CharacterActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech")
	FName AceProviderName = FName(TEXT("LocalA2F-Mark"));

	/** Match exhibition TTS / ACE warmup (24000 Hz mono). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech", meta = (ClampMin = "8000", ClampMax = "48000"))
	int32 StreamSampleRate = 24000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech", meta = (ClampMin = "1", ClampMax = "2"))
	int32 StreamNumChannels = 1;

	/** Enter Thinking on CharacterForAce when a prompt is submitted (before PCM arrives). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech")
	bool bBeginThinkingOnSubmit = true;

	/** After a successful reply, return performer to Listening (kiosk mic-open posture). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech")
	bool bReturnToListeningAfterReply = true;

	/** Press G (PIE) to submit DefaultTestPrompt — for quick tests without Blueprint wiring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech|Dev")
	bool bEnableDevKeyboardSubmit = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech|Dev", meta = (EditCondition = "bEnableDevKeyboardSubmit"))
	FString DefaultTestPrompt = TEXT("Tell me about your voyage to the New World.");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Direct Speech|Dev")
	bool bAutoSubmitTestPromptOnBeginPlay = false;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Direct Speech")
	FGodfreyDirectSpeechSimpleEvent OnStreamPlaybackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Direct Speech")
	FGodfreyDirectSpeechSimpleEvent OnStreamFinished;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Direct Speech")
	FGodfreyDirectSpeechErrorEvent OnStreamError;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AActor* ResolveCharacterForAce() const;
	void NotifyPerformerThinking() const;
	void NotifyPerformerListening() const;
	void BindDevKeyboardIfNeeded();
	void UnbindDevKeyboard();

	UFUNCTION()
	void HandleDevSubmitPressed();
	void StartStreamForPrompt(const FString& TrimmedPrompt);

	UFUNCTION()
	void HandleStreamPlaybackStarted();

	UFUNCTION()
	void HandleStreamLipSyncStarted();

	UFUNCTION()
	void HandleStreamFinished();

	UFUNCTION()
	void HandleStreamError(const FString& ErrorMessage);

	UPROPERTY(Transient)
	TObjectPtr<UAsyncActionStreamGodfreySpeech> ActiveStreamAction = nullptr;

	bool bIsStreaming = false;
	int32 DevSubmitKeyBindingIndex = INDEX_NONE;
};
