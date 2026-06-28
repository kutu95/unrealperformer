#include "GodfreyPerformanceStateComponent.h"

#include "GodfreyPerformanceLog.h"
#include "GameFramework/Actor.h"

namespace
{
static bool GodfreyCueTokenContainsKeyword(const FString& Token, const TCHAR* Keyword)
{
	if (Token.IsEmpty() || !Keyword)
	{
		return false;
	}
	return Token.Contains(Keyword, ESearchCase::IgnoreCase);
}
} // namespace

UGodfreyPerformanceStateComponent::UGodfreyPerformanceStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGodfreyPerformanceStateComponent::BeginPlay()
{
	Super::BeginPlay();
	PerformanceState = EGodfreyPerformanceState::Idle;
	if (AActor* const Owner = GetOwner())
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: BeginPlay owner=%s"), *Owner->GetName());
	}
	else
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: BeginPlay (no owner)"));
	}
}

void UGodfreyPerformanceStateComponent::BeginListening()
{
	EnterListening();
}

void UGodfreyPerformanceStateComponent::BeginThinking()
{
	EnterThinking();
}

void UGodfreyPerformanceStateComponent::BeginSpeaking()
{
	EnterSpeaking();
}

void UGodfreyPerformanceStateComponent::EndSpeaking()
{
	if (PerformanceState != EGodfreyPerformanceState::Speaking)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformer: EndSpeaking ignored (not Speaking)."));
		return;
	}
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: EndSpeaking -> Idle"));
	TrySetPerformanceState(EGodfreyPerformanceState::Idle);
}

void UGodfreyPerformanceStateComponent::ReturnToIdle()
{
	EnterIdle();
}

void UGodfreyPerformanceStateComponent::TriggerEmphasis()
{
	if (PerformanceState == EGodfreyPerformanceState::Emphasising)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformer: TriggerEmphasis retrigger (already Emphasising)."));
		OnEmphasisTriggered.Broadcast();
		return;
	}
	EnterEmphasising();
}

void UGodfreyPerformanceStateComponent::TriggerAmused()
{
	if (PerformanceState == EGodfreyPerformanceState::Amused)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformer: TriggerAmused retrigger."));
		OnAmusedTriggered.Broadcast();
		return;
	}
	EnterAmused();
}

void UGodfreyPerformanceStateComponent::TriggerSerious()
{
	if (PerformanceState == EGodfreyPerformanceState::Serious)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformer: TriggerSerious retrigger."));
		OnSeriousTriggered.Broadcast();
		return;
	}
	EnterSerious();
}

bool UGodfreyPerformanceStateComponent::TrySetPerformanceState(const EGodfreyPerformanceState NewState)
{
	if (NewState == PerformanceState)
	{
		return false;
	}
	ApplyPerformanceState(NewState);
	return true;
}

void UGodfreyPerformanceStateComponent::EnterIdle()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Idle);
}

void UGodfreyPerformanceStateComponent::EnterListening()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Listening);
}

void UGodfreyPerformanceStateComponent::EnterThinking()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Thinking);
}

void UGodfreyPerformanceStateComponent::EnterSpeaking()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Speaking);
}

void UGodfreyPerformanceStateComponent::EnterEmphasising()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Emphasising);
}

void UGodfreyPerformanceStateComponent::EnterSerious()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Serious);
}

void UGodfreyPerformanceStateComponent::EnterAmused()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Amused);
}

void UGodfreyPerformanceStateComponent::ResetToIdle()
{
	const EGodfreyPerformanceState Previous = PerformanceState;
	if (Previous == EGodfreyPerformanceState::Idle)
	{
		return;
	}
	ApplyPerformanceState(EGodfreyPerformanceState::Idle);
}

void UGodfreyPerformanceStateComponent::NotifyUtteranceStarted()
{
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: NotifyUtteranceStarted"));
	OnGodfreyUtteranceStarted.Broadcast();

	if (bAutoSpeakingStateFromUtterance)
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: auto utterance -> BeginSpeaking"));
		BeginSpeaking();
	}
}

void UGodfreyPerformanceStateComponent::NotifyUtteranceEnded()
{
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: NotifyUtteranceEnded"));
	OnGodfreyUtteranceEnded.Broadcast();

	if (bAutoSpeakingStateFromUtterance)
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: auto utterance -> EndSpeaking"));
		EndSpeaking();
	}
}

void UGodfreyPerformanceStateComponent::NotifyPerformanceCue(const FString& CueType, const FString& CueValue,
	const FString& RawCue)
{
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: cue type=\"%s\" value=\"%s\" raw_len=%d"), *CueType,
		*CueValue, RawCue.Len());
	OnPerformanceCueReceived.Broadcast(CueType, CueValue, RawCue);

	if (bRoutePerformanceCuesToStates)
	{
		const bool bRouted = TryConsumePerformanceCueForRouting(CueType, CueValue);
		if (!bRouted)
		{
			UE_LOG(LogGodfreyPerformance, Verbose,
				TEXT("GodfreyPerformer: cue not matched by built-in routing; handle in Blueprint from OnPerformanceCueReceived."));
		}
	}
}

FString UGodfreyPerformanceStateComponent::NormalizeCueToken(const FString& In)
{
	return In.TrimStartAndEnd().ToLower();
}

bool UGodfreyPerformanceStateComponent::TryConsumePerformanceCueForRouting(const FString& CueType,
	const FString& CueValue)
{
	const FString T = NormalizeCueToken(CueType);
	const FString V = NormalizeCueToken(CueValue);

	auto RouteFromTokens = [this, &T, &V]() -> bool
	{
		if (!V.IsEmpty())
		{
			if (GodfreyCueTokenContainsKeyword(V, TEXT("emphas")) || GodfreyCueTokenContainsKeyword(V, TEXT("stress"))
				|| V == TEXT("beat") || V == TEXT("punch"))
			{
				TriggerEmphasis();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(V, TEXT("serious")) || GodfreyCueTokenContainsKeyword(V, TEXT("stern"))
				|| GodfreyCueTokenContainsKeyword(V, TEXT("somber")))
			{
				TriggerSerious();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(V, TEXT("amus")) || GodfreyCueTokenContainsKeyword(V, TEXT("humor"))
				|| GodfreyCueTokenContainsKeyword(V, TEXT("laugh")) || GodfreyCueTokenContainsKeyword(V, TEXT("smile")))
			{
				TriggerAmused();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(V, TEXT("listen")))
			{
				BeginListening();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(V, TEXT("think")))
			{
				BeginThinking();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(V, TEXT("speak")) || GodfreyCueTokenContainsKeyword(V, TEXT("talk")))
			{
				BeginSpeaking();
				return true;
			}
			if (V == TEXT("idle") || GodfreyCueTokenContainsKeyword(V, TEXT("neutral")))
			{
				ReturnToIdle();
				return true;
			}
		}

		if (!T.IsEmpty())
		{
			if (GodfreyCueTokenContainsKeyword(T, TEXT("emphas")) || T == TEXT("stress") || T == TEXT("beat")
				|| T == TEXT("punch"))
			{
				TriggerEmphasis();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(T, TEXT("serious")) || T == TEXT("stern"))
			{
				TriggerSerious();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(T, TEXT("amus")) || GodfreyCueTokenContainsKeyword(T, TEXT("humor")))
			{
				TriggerAmused();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(T, TEXT("listen")))
			{
				BeginListening();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(T, TEXT("think")))
			{
				BeginThinking();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(T, TEXT("speak")) || GodfreyCueTokenContainsKeyword(T, TEXT("talk")))
			{
				BeginSpeaking();
				return true;
			}
			if (T == TEXT("idle"))
			{
				ReturnToIdle();
				return true;
			}
		}
		return false;
	};

	const bool bHit = RouteFromTokens();
	if (bHit)
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: routed cue (type=\"%s\" value=\"%s\")"), *T, *V);
	}
	return bHit;
}

void UGodfreyPerformanceStateComponent::ApplyPerformanceState(const EGodfreyPerformanceState NewState)
{
	const EGodfreyPerformanceState Previous = PerformanceState;
	PerformanceState = NewState;
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: performance state %d -> %d"), static_cast<int32>(Previous),
		static_cast<int32>(NewState));

	OnPerformanceStateChanged.Broadcast(PerformanceState, Previous);

	if (Previous == EGodfreyPerformanceState::Speaking && NewState != EGodfreyPerformanceState::Speaking)
	{
		OnSpeakingEnded.Broadcast();
	}

	DispatchEnteredStateDelegates(NewState, Previous);
}

void UGodfreyPerformanceStateComponent::DispatchEnteredStateDelegates(const EGodfreyPerformanceState NewState,
	const EGodfreyPerformanceState PreviousState)
{
	switch (NewState)
	{
	case EGodfreyPerformanceState::Idle:
		if (PreviousState != EGodfreyPerformanceState::Idle)
		{
			OnReturnedToIdle.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Listening:
		if (PreviousState != EGodfreyPerformanceState::Listening)
		{
			OnListeningStarted.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Thinking:
		if (PreviousState != EGodfreyPerformanceState::Thinking)
		{
			OnThinkingStarted.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Speaking:
		if (PreviousState != EGodfreyPerformanceState::Speaking)
		{
			OnSpeakingStarted.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Emphasising:
		if (PreviousState != EGodfreyPerformanceState::Emphasising)
		{
			OnEmphasisTriggered.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Serious:
		if (PreviousState != EGodfreyPerformanceState::Serious)
		{
			OnSeriousTriggered.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Amused:
		if (PreviousState != EGodfreyPerformanceState::Amused)
		{
			OnAmusedTriggered.Broadcast();
		}
		break;
	default:
		break;
	}
}
