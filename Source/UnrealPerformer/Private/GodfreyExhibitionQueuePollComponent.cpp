#include "GodfreyExhibitionQueuePollComponent.h"

#include "AsyncActionStreamGodfreySpeech.h"
#include "ACEAudioCurveSourceComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyExhibitionQueue, Log, All);

UGodfreyExhibitionQueuePollComponent::UGodfreyExhibitionQueuePollComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGodfreyExhibitionQueuePollComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bPollOnBeginPlay)
	{
		StartPolling();
	}
}

void UGodfreyExhibitionQueuePollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPolling();
	ClearActiveStream();
	Super::EndPlay(EndPlayReason);
}

AActor* UGodfreyExhibitionQueuePollComponent::ResolveCharacterForAce() const
{
	if (IsValid(CharacterForAce))
	{
		return CharacterForAce.Get();
	}

	if (UWorld* World = GetWorld())
	{
		if (!CharacterActorTag.IsNone())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (IsValid(Actor) && Actor->ActorHasTag(CharacterActorTag))
				{
					return Actor;
				}
			}
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (IsValid(Actor) && Actor->GetActorLabel() == TEXT("BP_Godfrey_Performer"))
			{
				return Actor;
			}
		}
	}

	return nullptr;
}

void UGodfreyExhibitionQueuePollComponent::StartPolling()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(PollTimerHandle);
	World->GetTimerManager().SetTimer(
		PollTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyExhibitionQueuePollComponent::PollOnce),
		PollIntervalSeconds,
		true,
		PollIntervalSeconds);

	UE_LOG(LogGodfreyExhibitionQueue, Log,
		TEXT("Exhibition queue poll started (interval=%.2fs, brain=%s)"),
		PollIntervalSeconds,
		*GodfreyBrainBaseUrl);
}

void UGodfreyExhibitionQueuePollComponent::StopPolling()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}
}

void UGodfreyExhibitionQueuePollComponent::ClearActiveStream()
{
	if (ActiveStreamAction)
	{
		ActiveStreamAction = nullptr;
	}
	bStreamInProgress = false;
}

void UGodfreyExhibitionQueuePollComponent::PollOnce()
{
	if (bStreamInProgress)
	{
		return;
	}

	AActor* const AceCharacter = ResolveCharacterForAce();
	if (!AceCharacter)
	{
		UE_LOG(LogGodfreyExhibitionQueue, Warning,
			TEXT("Queue poll: no CharacterForAce (tag %s or label BP_Godfrey_Performer)"),
			*CharacterActorTag.ToString());
		return;
	}

	if (!AceCharacter->FindComponentByClass<UACEAudioCurveSourceComponent>())
	{
		UE_LOG(LogGodfreyExhibitionQueue, Warning,
			TEXT("Queue poll: %s has no ACEAudioCurveSourceComponent"),
			*AceCharacter->GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bStreamInProgress = true;
	ActiveStreamAction = UAsyncActionStreamGodfreySpeech::PullQueuedGodfreySpeechToAudio(
		World,
		GodfreyBrainBaseUrl,
		AceCharacter,
		AceProviderName,
		StreamSampleRate,
		StreamNumChannels);

	if (!ActiveStreamAction)
	{
		UE_LOG(LogGodfreyExhibitionQueue, Error, TEXT("Queue poll: failed to create PullQueuedGodfreySpeechToAudio"));
		bStreamInProgress = false;
		return;
	}

	ActiveStreamAction->OnNoQueue.AddDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandleNoQueue);
	ActiveStreamAction->OnPlaybackStarted.AddDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandlePlaybackStarted);
	ActiveStreamAction->OnFinished.AddDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandleFinished);
	ActiveStreamAction->OnError.AddDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandleError);
	ActiveStreamAction->Activate();
}

void UGodfreyExhibitionQueuePollComponent::HandleNoQueue()
{
	bStreamInProgress = false;
	ActiveStreamAction = nullptr;
}

void UGodfreyExhibitionQueuePollComponent::HandlePlaybackStarted()
{
	AActor* const AceCharacter = ResolveCharacterForAce();
	UE_LOG(LogGodfreyExhibitionQueue, Log,
		TEXT("Queue poll: playback started on %s"),
		AceCharacter ? *AceCharacter->GetName() : TEXT("(unknown)"));
}

void UGodfreyExhibitionQueuePollComponent::HandleFinished()
{
	UE_LOG(LogGodfreyExhibitionQueue, Log, TEXT("Queue poll: stream finished"));
	bStreamInProgress = false;
	ActiveStreamAction = nullptr;
}

void UGodfreyExhibitionQueuePollComponent::HandleError(const FString& ErrorMessage)
{
	UE_LOG(LogGodfreyExhibitionQueue, Error, TEXT("Queue poll: %s"), *ErrorMessage);
	bStreamInProgress = false;
	ActiveStreamAction = nullptr;
}
