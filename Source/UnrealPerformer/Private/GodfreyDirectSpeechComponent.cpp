#include "GodfreyDirectSpeechComponent.h"

#include "Components/InputComponent.h"
#include "AsyncActionStreamGodfreySpeech.h"
#include "ACEAudioCurveSourceComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GodfreyPerformanceStateComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyDirectSpeech, Log, All);

UGodfreyDirectSpeechComponent::UGodfreyDirectSpeechComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGodfreyDirectSpeechComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bEnableDevKeyboardSubmit)
	{
		BindDevKeyboardIfNeeded();
	}

	if (bAutoSubmitTestPromptOnBeginPlay && !DefaultTestPrompt.IsEmpty())
	{
		AskGodfrey(DefaultTestPrompt);
	}
}

void UGodfreyDirectSpeechComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindDevKeyboard();
	ActiveStreamAction = nullptr;
	bIsStreaming = false;
	Super::EndPlay(EndPlayReason);
}

AActor* UGodfreyDirectSpeechComponent::ResolveCharacterForAce() const
{
	if (IsValid(CharacterForAce))
	{
		return CharacterForAce.Get();
	}

	if (AActor* const Owner = GetOwner())
	{
		if (Owner->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			return Owner;
		}
	}

	if (!CharacterActorTag.IsNone())
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* const Actor = *It;
				if (IsValid(Actor) && Actor->ActorHasTag(CharacterActorTag))
				{
					return Actor;
				}
			}
		}
	}

	return nullptr;
}

void UGodfreyDirectSpeechComponent::NotifyPerformerThinking() const
{
	if (!bBeginThinkingOnSubmit)
	{
		return;
	}

	if (AActor* const Ch = ResolveCharacterForAce())
	{
		if (UGodfreyPerformanceStateComponent* Perf = Ch->FindComponentByClass<UGodfreyPerformanceStateComponent>())
		{
			Perf->BeginThinking();
		}
	}
}

void UGodfreyDirectSpeechComponent::NotifyPerformerListening() const
{
	if (!bReturnToListeningAfterReply)
	{
		return;
	}

	if (AActor* const Ch = ResolveCharacterForAce())
	{
		if (UGodfreyPerformanceStateComponent* Perf = Ch->FindComponentByClass<UGodfreyPerformanceStateComponent>())
		{
			Perf->BeginListening();
		}
	}
}

bool UGodfreyDirectSpeechComponent::AskGodfrey(const FString& PromptText)
{
	const FString Trimmed = PromptText.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		UE_LOG(LogGodfreyDirectSpeech, Warning, TEXT("AskGodfrey: prompt text is empty."));
		return false;
	}

	if (bIsStreaming)
	{
		UE_LOG(LogGodfreyDirectSpeech, Warning, TEXT("AskGodfrey: already streaming; ignoring new prompt."));
		return false;
	}

	if (!ResolveCharacterForAce())
	{
		const FString Err = TEXT("AskGodfrey: CharacterForAce is not set and could not be resolved.");
		UE_LOG(LogGodfreyDirectSpeech, Error, TEXT("%s"), *Err);
		OnStreamError.Broadcast(Err);
		return false;
	}

	if (GodfreyBrainBaseUrl.IsEmpty())
	{
		const FString Err = TEXT("AskGodfrey: GodfreyBrainBaseUrl is empty.");
		UE_LOG(LogGodfreyDirectSpeech, Error, TEXT("%s"), *Err);
		OnStreamError.Broadcast(Err);
		return false;
	}

	StartStreamForPrompt(Trimmed);
	return true;
}

void UGodfreyDirectSpeechComponent::StartStreamForPrompt(const FString& TrimmedPrompt)
{
	AActor* const AceCharacter = ResolveCharacterForAce();
	UWorld* const World = GetWorld();
	if (!World || !AceCharacter)
	{
		return;
	}

	bIsStreaming = true;
	NotifyPerformerThinking();

	UE_LOG(LogGodfreyDirectSpeech, Log,
		TEXT("AskGodfrey: direct stream POST text_len=%d character=%s url=%s"),
		TrimmedPrompt.Len(),
		*AceCharacter->GetName(),
		*GodfreyBrainBaseUrl);

	ActiveStreamAction = UAsyncActionStreamGodfreySpeech::StreamGodfreySpeechToAudio(
		World,
		TrimmedPrompt,
		GodfreyBrainBaseUrl,
		AceCharacter,
		AceProviderName,
		StreamSampleRate,
		StreamNumChannels);

	if (!ActiveStreamAction)
	{
		bIsStreaming = false;
		const FString Err = TEXT("AskGodfrey: failed to create StreamGodfreySpeechToAudio action.");
		UE_LOG(LogGodfreyDirectSpeech, Error, TEXT("%s"), *Err);
		OnStreamError.Broadcast(Err);
		return;
	}

	ActiveStreamAction->OnPlaybackStarted.AddDynamic(this, &UGodfreyDirectSpeechComponent::HandleStreamPlaybackStarted);
	ActiveStreamAction->OnLipSyncStarted.AddDynamic(this, &UGodfreyDirectSpeechComponent::HandleStreamLipSyncStarted);
	ActiveStreamAction->OnFinished.AddDynamic(this, &UGodfreyDirectSpeechComponent::HandleStreamFinished);
	ActiveStreamAction->OnError.AddDynamic(this, &UGodfreyDirectSpeechComponent::HandleStreamError);
	ActiveStreamAction->Activate();
}

void UGodfreyDirectSpeechComponent::HandleStreamPlaybackStarted()
{
	OnStreamPlaybackStarted.Broadcast();
}

void UGodfreyDirectSpeechComponent::HandleStreamLipSyncStarted()
{
	// Lip sync hook available for Blueprint; playback start is the primary kiosk signal.
}

void UGodfreyDirectSpeechComponent::HandleStreamFinished()
{
	if (!bIsStreaming)
	{
		return;
	}

	bIsStreaming = false;
	ActiveStreamAction = nullptr;

	UE_LOG(LogGodfreyDirectSpeech, Log, TEXT("AskGodfrey: stream finished."));
	OnStreamFinished.Broadcast();
	NotifyPerformerListening();
}

void UGodfreyDirectSpeechComponent::HandleStreamError(const FString& ErrorMessage)
{
	if (!bIsStreaming && ActiveStreamAction == nullptr)
	{
		return;
	}

	bIsStreaming = false;
	ActiveStreamAction = nullptr;

	UE_LOG(LogGodfreyDirectSpeech, Error, TEXT("AskGodfrey: stream error: %s"), *ErrorMessage);
	OnStreamError.Broadcast(ErrorMessage);
	NotifyPerformerListening();
}

void UGodfreyDirectSpeechComponent::BindDevKeyboardIfNeeded()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		Owner->AutoReceiveInput = EAutoReceiveInput::Player0;
		Owner->EnableInput(PC);
	}

	if (!Owner->InputComponent)
	{
		return;
	}

	FInputKeyBinding Binding(FInputChord(EKeys::G), IE_Pressed);
	Binding.KeyDelegate.BindDelegate(this, &UGodfreyDirectSpeechComponent::HandleDevSubmitPressed);
	DevSubmitKeyBindingIndex = Owner->InputComponent->KeyBindings.Add(MoveTemp(Binding));
}

void UGodfreyDirectSpeechComponent::UnbindDevKeyboard()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->InputComponent || DevSubmitKeyBindingIndex == INDEX_NONE)
	{
		DevSubmitKeyBindingIndex = INDEX_NONE;
		return;
	}

	if (Owner->InputComponent->KeyBindings.IsValidIndex(DevSubmitKeyBindingIndex))
	{
		Owner->InputComponent->KeyBindings.RemoveAt(DevSubmitKeyBindingIndex);
	}
	DevSubmitKeyBindingIndex = INDEX_NONE;
}

void UGodfreyDirectSpeechComponent::HandleDevSubmitPressed()
{
	if (DefaultTestPrompt.IsEmpty())
	{
		UE_LOG(LogGodfreyDirectSpeech, Warning, TEXT("Dev key G: DefaultTestPrompt is empty."));
		return;
	}

	UE_LOG(LogGodfreyDirectSpeech, Log, TEXT("Dev key G: submitting DefaultTestPrompt."));
	AskGodfrey(DefaultTestPrompt);
}
