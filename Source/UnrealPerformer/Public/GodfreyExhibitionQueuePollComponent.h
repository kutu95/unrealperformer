#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyExhibitionQueuePollComponent.generated.h"

class UAsyncActionStreamGodfreySpeech;

/**
 * Exhibition queue poll — mirrors legacy BP_GodfreyApiTest timer (~1s).
 * GET /api/exhibition/unreal-tts-status → when ready, PullQueuedGodfreySpeechToAudio → ACE on CharacterForAce.
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyExhibitionQueuePollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyExhibitionQueuePollComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Exhibition Queue")
	FString GodfreyBrainBaseUrl = TEXT("http://localhost:3000");

	/** MetaHuman with UACEAudioCurveSourceComponent. If unset, uses CharacterActorTag or BP_Godfrey_Performer label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Exhibition Queue")
	TObjectPtr<AActor> CharacterForAce = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Exhibition Queue")
	FName CharacterActorTag = FName(TEXT("GodfreyCharacter"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Exhibition Queue")
	FName AceProviderName = FName(TEXT("LocalA2F-Mark"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Exhibition Queue", meta = (ClampMin = "8000", ClampMax = "48000"))
	int32 StreamSampleRate = 24000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Exhibition Queue", meta = (ClampMin = "1", ClampMax = "2"))
	int32 StreamNumChannels = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Exhibition Queue", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float PollIntervalSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Exhibition Queue")
	bool bPollOnBeginPlay = true;

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Exhibition Queue")
	void StartPolling();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Exhibition Queue")
	void StopPolling();

	UFUNCTION(BlueprintPure, Category = "Godfrey|Exhibition Queue")
	bool IsStreamActive() const { return bStreamInProgress; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AActor* ResolveCharacterForAce() const;
	void PollOnce();
	void ClearActiveStream();

	UFUNCTION()
	void HandleNoQueue();
	UFUNCTION()
	void HandlePlaybackStarted();
	UFUNCTION()
	void HandleFinished();
	UFUNCTION()
	void HandleError(const FString& ErrorMessage);

	FTimerHandle PollTimerHandle;
	UPROPERTY(Transient)
	TObjectPtr<UAsyncActionStreamGodfreySpeech> ActiveStreamAction;
	bool bStreamInProgress = false;
};
