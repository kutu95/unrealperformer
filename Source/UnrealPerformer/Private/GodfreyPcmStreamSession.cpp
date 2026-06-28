#include "GodfreyPcmStreamSession.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

#include "ACERuntimeModule.h"
#include "ACEBlueprintLibrary.h"
#include "ACEAudioCurveSourceComponent.h"
#include "UnrealPerformerGodfreySettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyPcmStream, Log, All);

namespace
{
static int32 GGodfreyUtteranceCounter = 0;

/** One active ingest/playback session per character; superseded sessions must unbind ACE delegates. */
static TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UGodfreyPcmStreamSession>> GActiveGodfreyAceSessionByCharacter;

static int32 ComputeMaxPcmBytesPerAceSubChunk(int32 SampleRate, int32 NumChannels, float ChunkDurationMs)
{
	const int32 FrameSize = NumChannels * static_cast<int32>(sizeof(int16));
	if (FrameSize <= 0 || SampleRate <= 0)
	{
		return FrameSize;
	}
	const float ClampedMs = FMath::Clamp(ChunkDurationMs, 10.f, 80.f);
	const int32 FramesPerSubChunk = FMath::Max(1, FMath::RoundToInt(static_cast<float>(SampleRate) * (ClampedMs / 1000.f)));
	const int32 UnalignedBytes = FramesPerSubChunk * FrameSize;
	return FMath::Max(FrameSize, (UnalignedBytes / FrameSize) * FrameSize);
}
} // namespace

void UGodfreyPcmStreamSession::SetClientRequestT0PlatformSeconds(double PlatformSeconds)
{
	ClientRequestT0PlatformSeconds = PlatformSeconds;
}

void UGodfreyPcmStreamSession::NotifyFirstHttpBodyBytesPlatformSeconds(double PlatformSeconds)
{
	if (FirstHttpBodyBytesPlatformSeconds < 0.0)
	{
		FirstHttpBodyBytesPlatformSeconds = PlatformSeconds;
	}
}

void UGodfreyPcmStreamSession::BeginDestroy()
{
	CancelDeferredAceUnbind();
	UnregisterActiveAceSessionForCharacter();
	UnbindAceDelegates();
	Super::BeginDestroy();
}

void UGodfreyPcmStreamSession::RegisterAsActiveAceSessionForCharacter()
{
	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Character);
	if (const TWeakObjectPtr<UGodfreyPcmStreamSession>* Existing = GActiveGodfreyAceSessionByCharacter.Find(Key))
	{
		if (UGodfreyPcmStreamSession* Prior = Existing->Get())
		{
			if (Prior != this)
			{
				UE_LOG(LogGodfreyPcmStream, Warning,
					TEXT("Godfrey utterance %d: superseding prior utterance %d on %s — unbinding stale ACE delegates to prevent cross-talk."),
					UtteranceOrdinal,
					Prior->UtteranceOrdinal,
					*Character->GetName());
				Prior->CancelDeferredAceUnbind();
				Prior->UnbindAceDelegates();
			}
		}
	}

	GActiveGodfreyAceSessionByCharacter.Add(Key, this);
}

void UGodfreyPcmStreamSession::UnregisterActiveAceSessionForCharacter()
{
	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Character);
	if (const TWeakObjectPtr<UGodfreyPcmStreamSession>* Existing = GActiveGodfreyAceSessionByCharacter.Find(Key))
	{
		if (Existing->Get() == this)
		{
			GActiveGodfreyAceSessionByCharacter.Remove(Key);
		}
	}
}

bool UGodfreyPcmStreamSession::IsActiveAceSessionForCharacter() const
{
	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		return false;
	}

	const TWeakObjectPtr<AActor> Key(Character);
	if (const TWeakObjectPtr<UGodfreyPcmStreamSession>* Existing = GActiveGodfreyAceSessionByCharacter.Find(Key))
	{
		return Existing->Get() == this;
	}

	return false;
}

void UGodfreyPcmStreamSession::UnbindAceDelegates()
{
	if (AActor* Character = TargetCharacter.Get())
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			if (bBoundAceAnimationStarted)
			{
				AceComp->OnAnimationStarted.RemoveDynamic(this, &UGodfreyPcmStreamSession::HandleAceAnimationStarted);
			}
			if (bBoundAceAnimationEnded)
			{
				AceComp->OnAnimationEnded.RemoveDynamic(this, &UGodfreyPcmStreamSession::HandleAceAnimationEnded);
			}
		}
	}
	bBoundAceAnimationStarted = false;
	bBoundAceAnimationEnded = false;
}

void UGodfreyPcmStreamSession::CancelDeferredAceUnbind()
{
	if (UWorld* World = DeferredAceUnbindWorld.Get())
	{
		World->GetTimerManager().ClearTimer(DeferredAceUnbindTimerHandle);
	}
	DeferredAceUnbindWorld.Reset();
	DeferredAceUnbindTimerHandle.Invalidate();
	bDeferredAceUnbindActive = false;
}

void UGodfreyPcmStreamSession::ScheduleDeferredAceUnbindAfterFinishStream(UWorld* World, const double FinishStreamPlatformSeconds)
{
	if (!World || (!bBoundAceAnimationStarted && !bBoundAceAnimationEnded))
	{
		return;
	}

	if (UtteranceStartupMetrics.bAceOnAnimationStartedObserved && bAcePlaybackEndedObserved)
	{
		UnbindAceDelegates();
		return;
	}

	CancelDeferredAceUnbind();

	bDeferredAceUnbindActive = true;
	DeferredUnbindUtteranceOrdinal = UtteranceOrdinal;
	DeferredUnbindStartPlatformSeconds = FPlatformTime::Seconds();
	DeferredUnbindFinishStreamPlatformSeconds = FinishStreamPlatformSeconds;
	DeferredAceUnbindWorld = World;

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: ACE delegates left bound until OnAnimationEnded (ingest complete; playback may continue ~%.1fs)."),
		UtteranceOrdinal,
		(StreamSampleRate > 0) ? static_cast<float>(TotalSamplesSentToAce) / static_cast<float>(StreamSampleRate) : 0.f);

	World->GetTimerManager().SetTimer(
		DeferredAceUnbindTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPcmStreamSession::ProcessDeferredAceUnbindTick),
		0.05f,
		true);
}

void UGodfreyPcmStreamSession::ProcessDeferredAceUnbindTick()
{
	if (!bDeferredAceUnbindActive || (!bBoundAceAnimationStarted && !bBoundAceAnimationEnded))
	{
		CancelDeferredAceUnbind();
		return;
	}

	if (UtteranceOrdinal != DeferredUnbindUtteranceOrdinal)
	{
		CancelDeferredAceUnbind();
		return;
	}

	const float GraceSeconds = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAcePostFinishOnAnimationStartedDelegateGraceSeconds;
	const double Now = FPlatformTime::Seconds();

	if (bAcePlaybackEndedObserved)
	{
		CancelDeferredAceUnbind();
		UnbindAceDelegates();
		return;
	}

	const double ExpectedPlaybackSec = (StreamSampleRate > 0)
		? static_cast<double>(TotalSamplesSentToAce) / static_cast<double>(StreamSampleRate)
		: 30.0;
	const double TimeoutSec = FMath::Max(static_cast<double>(GraceSeconds), ExpectedPlaybackSec + 5.0);
	const double Elapsed = Now - DeferredUnbindStartPlatformSeconds;

	if (Elapsed < TimeoutSec)
	{
		return;
	}

	UE_LOG(LogGodfreyPcmStream, Warning,
		TEXT("Godfrey utterance %d: OnAnimationEnded did not arrive within %.1fs (expected playback ~%.1fs); forcing OnPlaybackEnded and unbinding ACE delegates."),
		DeferredUnbindUtteranceOrdinal,
		Elapsed,
		ExpectedPlaybackSec);

	OnPlaybackEnded.Broadcast();
	CancelDeferredAceUnbind();
	UnbindAceDelegates();
}

void UGodfreyPcmStreamSession::ApplyGodfreyAcePlaybackPriming(UACEAudioCurveSourceComponent* AceComp)
{
	if (!AceComp)
	{
		return;
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (Settings->bApplyGodfreyAceBufferLength)
	{
		GodfreySavedAceBufferLengthInSeconds = AceComp->BufferLengthInSeconds;
		bGodfreySavedAceBufferLength = true;
		const float Clamped = FMath::Clamp(Settings->GodfreyAceBufferLengthSeconds, 0.05f, 1.5f);
		AceComp->BufferLengthInSeconds = Clamped;
		bGodfreyAcePrimingApplied = true;
		bGodfreyAceBufferLengthOverriddenThisUtterance = true;
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey ACE priming: BufferLengthInSeconds %.4f -> %.4f (Project Settings / Test Live Audio Godfrey)"),
			GodfreySavedAceBufferLengthInSeconds,
			Clamped);
	}

	if (Settings->GodfreyAceMinBlendShapeSamplesOverride >= 0)
	{
		GodfreySavedAceMinBlendShapeSamplesBeforePlay = AceComp->MinBlendShapeSamplesBeforePlay;
		bGodfreySavedAceMinBlend = true;
		AceComp->MinBlendShapeSamplesBeforePlay = Settings->GodfreyAceMinBlendShapeSamplesOverride;
		bGodfreyAcePrimingApplied = true;
		bGodfreyAceMinBlendOverriddenThisUtterance = true;
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey ACE priming: MinBlendShapeSamplesBeforePlay %d -> %d"),
			GodfreySavedAceMinBlendShapeSamplesBeforePlay,
			AceComp->MinBlendShapeSamplesBeforePlay);
	}

	if (Settings->bGodfreyAceHoldPlayUntilStreamEnd)
	{
		GodfreySavedAceMinCurveTimestampBeforePlay = AceComp->MinCurveTimestampSecondsBeforePlay;
		bGodfreySavedAceMinCurveLead = true;
		AceComp->MinCurveTimestampSecondsBeforePlay = Settings->GodfreyAceHoldPlayMinCurveTimestampGate;
		bGodfreyAcePrimingApplied = true;
		bGodfreyAceMinCurveLeadOverriddenThisUtterance = true;
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey ACE priming: hold Play until stream end ENABLED — MinCurveTimestampSecondsBeforePlay %.4f -> %.1f. ")
			TEXT("Best lip sync on long clips; startup waits for full HTTP download + A2F burst (~seconds on long replies). ")
			TEXT("Disable bGodfreyAceHoldPlayUntilStreamEnd in Project Settings for faster time-to-first-word."),
			GodfreySavedAceMinCurveTimestampBeforePlay,
			AceComp->MinCurveTimestampSecondsBeforePlay);
	}
	else if (Settings->GodfreyAceMinCurveTimestampBeforePlay >= 0.f)
	{
		GodfreySavedAceMinCurveTimestampBeforePlay = AceComp->MinCurveTimestampSecondsBeforePlay;
		bGodfreySavedAceMinCurveLead = true;
		AceComp->MinCurveTimestampSecondsBeforePlay = Settings->GodfreyAceMinCurveTimestampBeforePlay;
		bGodfreyAcePrimingApplied = true;
		bGodfreyAceMinCurveLeadOverriddenThisUtterance = true;
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey ACE priming: early Play — MinCurveTimestampSecondsBeforePlay %.4f -> %.4f (faster startup; extrapolation covers brief curve gaps on long monolithic streams)"),
			GodfreySavedAceMinCurveTimestampBeforePlay,
			AceComp->MinCurveTimestampSecondsBeforePlay);
	}
}

int32 UGodfreyPcmStreamSession::GetEffectiveIngestPushBudget(int32 ConfigBudget, bool bAllowOverrun) const
{
	const int32 ClampedConfigBudget = FMath::Max(1, ConfigBudget);
	if (bAllowOverrun || !bStreamStarted || bFinished)
	{
		return ClampedConfigBudget;
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (!Settings->bGodfreyAcePaceIngestByCurveCatchUp)
	{
		return ClampedConfigBudget;
	}

	const AActor* Character = TargetCharacter.Get();
	if (!Character || StreamSampleRate <= 0)
	{
		return ClampedConfigBudget;
	}

	const UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp || !AceComp->IsProceduralAudioPlaying())
	{
		return ClampedConfigBudget;
	}

	const float SentAudioSec = static_cast<float>(TotalSamplesSentToAce) / static_cast<float>(StreamSampleRate);
	const float MaxCurveTs = AceComp->GetMaxReceivedCurveTimestamp();
	const float UnmatchedSec = SentAudioSec - FMath::Max(0.f, MaxCurveTs);

	const float TightThresholdSec = Settings->GodfreyAceMaxUnmatchedAudioSeconds;
	const float MediumThresholdSec = FMath::Min(
		Settings->GodfreyAceSoftThrottleMediumUnmatchedSeconds,
		TightThresholdSec - 0.05f);

	if (UnmatchedSec >= TightThresholdSec)
	{
		return 1;
	}

	if (UnmatchedSec >= MediumThresholdSec)
	{
		return FMath::Max(1, ClampedConfigBudget / 2);
	}

	return ClampedConfigBudget;
}

void UGodfreyPcmStreamSession::RestoreGodfreyAcePlaybackPrimingIfApplied()
{
	if (!bGodfreyAcePrimingApplied)
	{
		return;
	}

	if (AActor* Character = TargetCharacter.Get())
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			if (bGodfreySavedAceBufferLength)
			{
				AceComp->BufferLengthInSeconds = GodfreySavedAceBufferLengthInSeconds;
				bGodfreySavedAceBufferLength = false;
				UE_LOG(LogGodfreyPcmStream, Verbose, TEXT("Godfrey ACE priming: restored BufferLengthInSeconds to %.4f"), AceComp->BufferLengthInSeconds);
			}
			if (bGodfreySavedAceMinBlend)
			{
				AceComp->MinBlendShapeSamplesBeforePlay = GodfreySavedAceMinBlendShapeSamplesBeforePlay;
				bGodfreySavedAceMinBlend = false;
				UE_LOG(LogGodfreyPcmStream, Verbose, TEXT("Godfrey ACE priming: restored MinBlendShapeSamplesBeforePlay to %d"), AceComp->MinBlendShapeSamplesBeforePlay);
			}
			if (bGodfreySavedAceMinCurveLead)
			{
				AceComp->MinCurveTimestampSecondsBeforePlay = GodfreySavedAceMinCurveTimestampBeforePlay;
				bGodfreySavedAceMinCurveLead = false;
				UE_LOG(LogGodfreyPcmStream, Verbose, TEXT("Godfrey ACE priming: restored MinCurveTimestampSecondsBeforePlay to %.4f"), AceComp->MinCurveTimestampSecondsBeforePlay);
			}
		}
	}

	bGodfreyAcePrimingApplied = false;
}

void UGodfreyPcmStreamSession::HandleAceAnimationStarted()
{
	if (!IsActiveAceSessionForCharacter())
	{
		UE_LOG(LogGodfreyPcmStream, Verbose,
			TEXT("Godfrey utterance %d: ignoring stale OnAnimationStarted (superseded by a newer session on the same character)."),
			UtteranceOrdinal);
		return;
	}

	const double PlatformNow = FPlatformTime::Seconds();
	if (FirstOnAnimationStartedPlatformSeconds < 0.0)
	{
		FirstOnAnimationStartedPlatformSeconds = PlatformNow;
	}
	UtteranceStartupMetrics.AceOnAnimationStartedPlatformSeconds = PlatformNow;
	UtteranceStartupMetrics.bAceOnAnimationStartedObserved = true;

	if (bDeferredAceUnbindActive && bFinished && (DeferredUnbindUtteranceOrdinal == UtteranceOrdinal))
	{
		const double DeltaMs = (PlatformNow - DeferredUnbindFinishStreamPlatformSeconds) * 1000.0;
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: OnAnimationStarted arrived ~%.0f ms after FinishStream (ACE playback/sync trailing HTTP ingest + EndAudioSamples)."),
			UtteranceOrdinal,
			DeltaMs);
		if (GetDefault<UUnrealPerformerGodfreySettings>()->bLogGodfreyAceStartupCompletionSummary)
		{
			LogGodfreyAceStartupCompletionSummary(PlatformNow);
		}
	}

	double WorldNow = -1.0;
	if (const AActor* Character = TargetCharacter.Get())
	{
		if (const UWorld* World = Character->GetWorld())
		{
			WorldNow = World->GetTimeSeconds();
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("ACE playback/sync: OnAnimationStarted (internal AudioComponent path). WorldTime=%.6f PlatformTime=%.6f"),
		WorldNow,
		PlatformNow);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: OnAnimationStarted — ACE reached STARTED->IN_PROGRESS; OnPlaybackStarted/OnLipSyncStarted will broadcast (audible clock active)."),
		UtteranceOrdinal);

	if (FirstChunkPlatformSeconds >= 0.0)
	{
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("ACE timing delta from first PCM chunk: PlatformDelta=%.6fs (chunk at %.6f, animation at %.6f)"),
			PlatformNow - FirstChunkPlatformSeconds,
			FirstChunkPlatformSeconds,
			PlatformNow);
	}

	OnPlaybackStarted.Broadcast();
	OnLipSyncStarted.Broadcast();

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Facial animation pipeline active in sync with ACE audio clock (OnPlaybackStarted + OnLipSyncStarted broadcast)."));
}

void UGodfreyPcmStreamSession::HandleAceAnimationEnded()
{
	if (!IsActiveAceSessionForCharacter())
	{
		UE_LOG(LogGodfreyPcmStream, Verbose,
			TEXT("Godfrey utterance %d: ignoring stale OnAnimationEnded (superseded by a newer session on the same character)."),
			UtteranceOrdinal);
		return;
	}

	bAcePlaybackEndedObserved = true;

	const double PlatformNow = FPlatformTime::Seconds();
	double WorldNow = -1.0;
	if (const AActor* Character = TargetCharacter.Get())
	{
		if (const UWorld* World = Character->GetWorld())
		{
			WorldNow = World->GetTimeSeconds();
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: OnAnimationEnded — ACE playback complete (audible + curves). WorldTime=%.6f PlatformTime=%.6f SamplesSentToACE=%lld"),
		UtteranceOrdinal,
		WorldNow,
		PlatformNow,
		TotalSamplesSentToAce);

	OnPlaybackEnded.Broadcast();

	if (bDeferredAceUnbindActive && bFinished && (DeferredUnbindUtteranceOrdinal == UtteranceOrdinal))
	{
		CancelDeferredAceUnbind();
		UnbindAceDelegates();
	}
}

bool UGodfreyPcmStreamSession::StartStream(UObject* WorldContextObject, AActor* CharacterForAce, FName ProviderName, int32 SampleRate, int32 NumChannels)
{
	if (bStreamStarted && !bFinished)
	{
		ReportError(TEXT("StartStream called while stream is already active."));
		return false;
	}

	if (SampleRate <= 0)
	{
		ReportError(FString::Printf(TEXT("Invalid sample rate: %d"), SampleRate));
		return false;
	}
	if (NumChannels <= 0 || NumChannels > 2)
	{
		ReportError(FString::Printf(TEXT("Unsupported channel count: %d. Only mono/stereo supported."), NumChannels));
		return false;
	}

	if (!CharacterForAce)
	{
		ReportError(TEXT("StartStream: CharacterForAce is required for ACE-only playback."));
		return false;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		ReportError(TEXT("Invalid world context in StartStream."));
		return false;
	}

	UACEAudioCurveSourceComponent* AceComp = CharacterForAce->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		ReportError(FString::Printf(TEXT("StartStream: %s has no UACEAudioCurveSourceComponent (required for ACE audio + curves)."), *CharacterForAce->GetName()));
		return false;
	}

	if (AceComp->Volume <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("UACEAudioCurveSourceComponent::Volume is near zero (%.4f). Audible ACE playback may be silent; increase Volume on the component for speech."), AceComp->Volume);
	}

	CancelDeferredAceUnbind();
	UnbindAceDelegates();

	TargetCharacter = CharacterForAce;
	AceProviderName = ProviderName;
	StreamSampleRate = SampleRate;
	StreamNumChannels = NumChannels;
	TotalSamplesSentToAce = 0;
	RollingPcmBytes.Reset();
	bLoggedFirstPcmChunk = false;
	FirstChunkWorldTimeSeconds = -1.0;
	FirstChunkPlatformSeconds = -1.0;
	UtteranceOrdinal = ++GGodfreyUtteranceCounter;
	StreamStartPlatformSeconds = FPlatformTime::Seconds();
	FirstHttpBodyBytesPlatformSeconds = -1.0;
	FirstAnimateSubchunkPlatformSeconds = -1.0;
	FirstOnAnimationStartedPlatformSeconds = -1.0;
	bStreamStarted = true;
	bFinished = false;
	bGodfreyAcePrimingApplied = false;
	bGodfreySavedAceBufferLength = false;
	bGodfreySavedAceMinBlend = false;
	bGodfreySavedAceMinCurveLead = false;
	bGodfreyAceBufferLengthOverriddenThisUtterance = false;
	bGodfreyAceMinBlendOverriddenThisUtterance = false;
	bGodfreyAceMinCurveLeadOverriddenThisUtterance = false;
	UtteranceStartupMetrics = FGodfreyAceUtteranceStartupMetrics();
	UtteranceStartupMetrics.UtteranceOrdinal = UtteranceOrdinal;
	UtteranceStartupMetrics.UtteranceT0PlatformSeconds = StreamStartPlatformSeconds;
	bAcePlaybackEndedObserved = false;

	ApplyGodfreyAcePlaybackPriming(AceComp);

	const float AceChunkMsCfg = GetDefault<UUnrealPerformerGodfreySettings>()->AceMaxPcmPushChunkDurationMs;
	const int32 AceSubchunkBytes = ComputeMaxPcmBytesPerAceSubChunk(StreamSampleRate, StreamNumChannels, AceChunkMsCfg);
	const int32 FrameSizeLog = StreamNumChannels * static_cast<int32>(sizeof(int16));
	const int32 SubchunkFrames = (FrameSizeLog > 0) ? (AceSubchunkBytes / FrameSizeLog) : 0;
	const float NominalSubchunkAudioMs = (StreamSampleRate > 0 && SubchunkFrames > 0)
		? (1000.f * static_cast<float>(SubchunkFrames) / static_cast<float>(StreamSampleRate))
		: 0.f;
	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: ACE ingest subchunk — AceMaxPcmPushChunkDurationMs=%.1f (config) -> maxBytes=%d frames=%d nominalAudio=%.2f ms per AnimateFromAudioSamples (HTTP drain uses same cap)."),
		UtteranceOrdinal,
		AceChunkMsCfg,
		AceSubchunkBytes,
		SubchunkFrames,
		NominalSubchunkAudioMs);

	AceComp->OnAnimationStarted.AddDynamic(this, &UGodfreyPcmStreamSession::HandleAceAnimationStarted);
	bBoundAceAnimationStarted = true;
	AceComp->OnAnimationEnded.AddDynamic(this, &UGodfreyPcmStreamSession::HandleAceAnimationEnded);
	bBoundAceAnimationEnded = true;

	RegisterAsActiveAceSessionForCharacter();

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: fresh session — ACE OnAnimationStarted/OnAnimationEnded bound; audible output uses ACE internal AudioComponent after Play(). Character=%s"),
		UtteranceOrdinal,
		*CharacterForAce->GetName());

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Stream started (ACE-only playback). UtteranceOrdinal=%d SampleRate=%d Channels=%d Provider=%s Character=%s ACE_BufferLengthSec=%.4f ACE_Volume=%.3f StreamT0=%.6f ClientReqT0=%.6f"),
		UtteranceOrdinal,
		StreamSampleRate,
		StreamNumChannels,
		*AceProviderName.ToString(),
		*CharacterForAce->GetName(),
		AceComp->BufferLengthInSeconds,
		AceComp->Volume,
		StreamStartPlatformSeconds,
		ClientRequestT0PlatformSeconds);

	return true;
}

bool UGodfreyPcmStreamSession::WarmupAcePipeline(AActor* CharacterForAce, FName ProviderName, int32 SampleRate, float SilenceDurationSeconds)
{
	if (!CharacterForAce)
	{
		return false;
	}
	UACEAudioCurveSourceComponent* AceComp = CharacterForAce->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		UE_LOG(LogGodfreyPcmStream, Warning, TEXT("WarmupAcePipeline: no UACEAudioCurveSourceComponent on %s"), *CharacterForAce->GetName());
		return false;
	}
	if (SampleRate <= 0)
	{
		return false;
	}

	const int32 NumMonoSamples = FMath::Max(16, FMath::RoundToInt(SilenceDurationSeconds * static_cast<float>(SampleRate)));
	TArray<int16> Silence;
	Silence.SetNumZeroed(NumMonoSamples);

	const double Wall0 = FPlatformTime::Seconds();
	const bool bAnimOk = FACERuntimeModule::Get().AnimateFromAudioSamples(
		AceComp,
		MakeArrayView(Silence.GetData(), Silence.Num()),
		1,
		SampleRate,
		false,
		TOptional<FAudio2FaceEmotion>(),
		nullptr,
		ProviderName);
	const double Wall1 = FPlatformTime::Seconds();

	const bool bEndOk = FACERuntimeModule::Get().EndAudioSamples(AceComp);
	const double Wall2 = FPlatformTime::Seconds();

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("ACE warmup: silenceSamples=%d provider=%s AnimateWall=%.3fms EndAudioWall=%.3fms TotalWall=%.3fms AnimateOk=%d EndOk=%d (enable ace.PipelineTimingLog 1 for A2XSession detail)"),
		NumMonoSamples,
		*ProviderName.ToString(),
		(Wall1 - Wall0) * 1000.0,
		(Wall2 - Wall1) * 1000.0,
		(Wall2 - Wall0) * 1000.0,
		bAnimOk ? 1 : 0,
		bEndOk ? 1 : 0);

	return bAnimOk && bEndOk;
}

bool UGodfreyPcmStreamSession::ValidateFormat(const TArray<uint8>& PcmBytes, FString& OutError) const
{
	OutError.Reset();
	if (!bStreamStarted || bFinished)
	{
		OutError = TEXT("Stream not active. Call StartStream first.");
		return false;
	}
	if (PcmBytes.Num() <= 0)
	{
		OutError = TEXT("PCM chunk is empty.");
		return false;
	}

	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	if (FrameSize <= 0 || (PcmBytes.Num() % FrameSize) != 0)
	{
		OutError = FString::Printf(TEXT("PCM chunk alignment invalid. Bytes=%d FrameSize=%d"), PcmBytes.Num(), FrameSize);
		return false;
	}
	return true;
}

bool UGodfreyPcmStreamSession::PushPcm16Chunk(const TArray<uint8>& PcmBytes, FString& OutError)
{
	if (!ValidateFormat(PcmBytes, OutError))
	{
		ReportError(OutError);
		return false;
	}

	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		OutError = TEXT("PushPcm16Chunk: character lost.");
		ReportError(OutError);
		return false;
	}

	UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		OutError = TEXT("PushPcm16Chunk: UACEAudioCurveSourceComponent missing.");
		ReportError(OutError);
		return false;
	}

	if (!bLoggedFirstPcmChunk)
	{
		bLoggedFirstPcmChunk = true;
		FirstChunkPlatformSeconds = FPlatformTime::Seconds();
		UtteranceStartupMetrics.FirstPcmChunkPlatformSeconds = FirstChunkPlatformSeconds;
		if (const UWorld* W = Character->GetWorld())
		{
			FirstChunkWorldTimeSeconds = W->GetTimeSeconds();
		}
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("First PCM chunk received for ACE stream. Bytes=%d SampleRate=%d Channels=%d WorldTime=%.6f PlatformTime=%.6f"),
			PcmBytes.Num(),
			StreamSampleRate,
			StreamNumChannels,
			FirstChunkWorldTimeSeconds,
			FirstChunkPlatformSeconds);
	}

	const float ChunkMs = GetDefault<UUnrealPerformerGodfreySettings>()->AceMaxPcmPushChunkDurationMs;
	const int32 MaxBytesPerAceCall = ComputeMaxPcmBytesPerAceSubChunk(StreamSampleRate, StreamNumChannels, ChunkMs);
	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	const int32 TotalBytes = PcmBytes.Num();

	for (int32 OffsetBytes = 0; OffsetBytes < TotalBytes; OffsetBytes += MaxBytesPerAceCall)
	{
		const int32 SubBytes = FMath::Min(MaxBytesPerAceCall, TotalBytes - OffsetBytes);
		check((SubBytes % FrameSize) == 0);
		const int16* SubPtr = reinterpret_cast<const int16*>(PcmBytes.GetData() + OffsetBytes);
		const int32 SubInt16Count = SubBytes / static_cast<int32>(sizeof(int16));
		const int32 SubFrames = SubInt16Count / StreamNumChannels;
		const double ChunkDurationSec = static_cast<double>(SubFrames) / static_cast<double>(StreamSampleRate);

		const double WallBeforeAnimate = FPlatformTime::Seconds();
		if (AceIsPipelineTimingLogEnabled())
		{
			const double DeltaSinceFirstPcmSec = (FirstChunkPlatformSeconds >= 0.0) ? (WallBeforeAnimate - FirstChunkPlatformSeconds) : 0.0;
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("[ACE pipeline] PushPcm16Chunk -> AnimateFromAudioSamples int16Count=%d frames=%d bytes=%d totalSamplesSentBefore=%lld deltaSinceFirstPcm=%.3fs"),
				SubInt16Count,
				SubFrames,
				SubBytes,
				TotalSamplesSentToAce,
				DeltaSinceFirstPcmSec);
		}

		const bool bSent = FACERuntimeModule::Get().AnimateFromAudioSamples(
			AceComp,
			MakeArrayView(SubPtr, SubInt16Count),
			StreamNumChannels,
			StreamSampleRate,
			false,
			TOptional<FAudio2FaceEmotion>(),
			nullptr,
			AceProviderName);

		const double WallAfterAnimate = FPlatformTime::Seconds();
		if (GetDefault<UUnrealPerformerGodfreySettings>()->bLogPerAnimateChunkWallTime)
		{
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("[Godfrey ACE] Utterance=%d Animate subchunk frames=%d samples=%d chunkMs=%.2f wallMs=%.3f ok=%d"),
				UtteranceOrdinal,
				SubFrames,
				SubInt16Count,
				ChunkDurationSec * 1000.0,
				(WallAfterAnimate - WallBeforeAnimate) * 1000.0,
				bSent ? 1 : 0);
		}

		if (AceIsPipelineTimingLogEnabled())
		{
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("[ACE pipeline] PushPcm16Chunk AnimateFromAudioSamples returned wall=%.3fms ok=%d"),
				(WallAfterAnimate - WallBeforeAnimate) * 1000.0,
				bSent ? 1 : 0);
		}

		if (!bSent)
		{
			OutError = FString::Printf(TEXT("ACE rejected audio chunk for provider '%s'."), *AceProviderName.ToString());
			ReportError(OutError);
			return false;
		}

		if (FirstAnimateSubchunkPlatformSeconds < 0.0)
		{
			FirstAnimateSubchunkPlatformSeconds = WallBeforeAnimate;
			UtteranceStartupMetrics.FirstAnimateFromAudioCallPlatformSeconds = WallBeforeAnimate;
		}

		TotalSamplesSentToAce += SubFrames;
	}

	RollingPcmBytes.Append(PcmBytes);

	UE_LOG(LogGodfreyPcmStream, Verbose, TEXT("PCM sent to ACE. ChunkBytes=%d TotalSamplesToACE=%lld BufferedBytes=%d"),
		PcmBytes.Num(),
		TotalSamplesSentToAce,
		RollingPcmBytes.Num());

	return true;
}

bool UGodfreyPcmStreamSession::FinishStream(FString& OutError)
{
	OutError.Reset();
	if (!bStreamStarted || bFinished)
	{
		OutError = TEXT("FinishStream called without an active stream.");
		ReportError(OutError);
		return false;
	}

	const double FinishPlatformSeconds = FPlatformTime::Seconds();

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: FinishStream — flushing ACE tail (EndAudioSamples). SamplesSentToACE=%lld BufferedBytes=%d"),
		UtteranceOrdinal,
		TotalSamplesSentToAce,
		RollingPcmBytes.Num());

	if (AActor* Character = TargetCharacter.Get())
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
			if (Settings->bGodfreyAceHoldPlayUntilStreamEnd && bGodfreyAceMinCurveLeadOverriddenThisUtterance)
			{
				AceComp->MinCurveTimestampSecondsBeforePlay = 0.f;
				UE_LOG(LogGodfreyPcmStream, Log,
					TEXT("Godfrey utterance %d: released hold-play gate (MinCurveTimestampSecondsBeforePlay -> 0) before EndAudioSamples. SamplesSentToACE=%lld"),
					UtteranceOrdinal,
					TotalSamplesSentToAce);
			}

			const double EndAudioSamplesPlatformSeconds = FPlatformTime::Seconds();
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("ACE EndAudioSamples invoked (stream tail / flush; not playback start). PlatformTime=%.6f SamplesSentToACE=%lld"),
				EndAudioSamplesPlatformSeconds,
				TotalSamplesSentToAce);

			const bool bEnded = FACERuntimeModule::Get().EndAudioSamples(AceComp);
			if (!bEnded)
			{
				OutError = TEXT("ACE EndAudioSamples failed.");
				RestoreGodfreyAcePlaybackPrimingIfApplied();
				ReportError(OutError);
				return false;
			}
		}
	}

	RestoreGodfreyAcePlaybackPrimingIfApplied();

	UWorld* CharacterWorld = nullptr;
	if (AActor* Character = TargetCharacter.Get())
	{
		CharacterWorld = Character->GetWorld();
	}

	const float DelegateGraceSeconds = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAcePostFinishOnAnimationStartedDelegateGraceSeconds;
	if (CharacterWorld)
	{
		ScheduleDeferredAceUnbindAfterFinishStream(CharacterWorld, FinishPlatformSeconds);
	}
	else
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: no world at FinishStream — cannot defer ACE delegate unbind; OnPlaybackEnded may not fire."),
			UtteranceOrdinal);
		UnbindAceDelegates();
	}

	bFinished = true;
	UE_LOG(LogGodfreyPcmStream, Log, TEXT("Stream finished. Utterance=%d SamplesSentToACE=%lld BufferedBytes=%d"), UtteranceOrdinal, TotalSamplesSentToAce, RollingPcmBytes.Num());
	LogUtteranceLatencySummaryAtFinishIfEnabled(FinishPlatformSeconds);
	if (FirstOnAnimationStartedPlatformSeconds < 0.0)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: OnAnimationStarted not observed yet at FinishStream snapshot (latency summary shows -1s). ACE may still be promoting playback after EndAudioSamples; ")
				TEXT("if GodfreyAcePostFinishOnAnimationStartedDelegateGraceSeconds > 0, delegate stays bound and a supplemental startup summary may log when ACE fires. Otherwise check BufferLengthInSeconds / MinBlendShapeSamplesBeforePlay and ace.GodfreyStartupTiming=1."),
			UtteranceOrdinal);
	}
	else
	{
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: ACE OnAnimationStarted observed — audible path active for this utterance."),
			UtteranceOrdinal);
	}
	LogGodfreyAceStartupCompletionSummary(FinishPlatformSeconds);
	OnFinished.Broadcast();
	return true;
}

void UGodfreyPcmStreamSession::StopStream()
{
	const double StopWallSeconds = FPlatformTime::Seconds();
	CancelDeferredAceUnbind();
	RestoreGodfreyAcePlaybackPrimingIfApplied();
	if (bStreamStarted && !bAcePlaybackEndedObserved)
	{
		OnPlaybackEnded.Broadcast();
		bAcePlaybackEndedObserved = true;
	}
	UnregisterActiveAceSessionForCharacter();
	UnbindAceDelegates();
	bFinished = true;
	bStreamStarted = false;
	LogGodfreyAceStartupCompletionSummary(StopWallSeconds);
	UE_LOG(LogGodfreyPcmStream, Log, TEXT("Stream stopped (ACE-only; delegates unbound)."));
}

void UGodfreyPcmStreamSession::LogUtteranceLatencySummaryAtFinishIfEnabled(double FinishPlatformSeconds) const
{
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bLogUtteranceLatencySummaryAtStreamFinish)
	{
		return;
	}

	auto DeltaSec = [](double From, double To) -> double
	{
		if (From < 0.0 || To < 0.0)
		{
			return -1.0;
		}
		return To - From;
	};

	const double DtClientToHttp = DeltaSec(ClientRequestT0PlatformSeconds, FirstHttpBodyBytesPlatformSeconds);
	const double DtClientToFirstPcm = DeltaSec(ClientRequestT0PlatformSeconds, FirstChunkPlatformSeconds);
	const double DtClientToFirstAnimate = DeltaSec(ClientRequestT0PlatformSeconds, FirstAnimateSubchunkPlatformSeconds);
	const double DtClientToOnAnimStarted = DeltaSec(ClientRequestT0PlatformSeconds, FirstOnAnimationStartedPlatformSeconds);
	const double DtStreamToFirstPcm = DeltaSec(StreamStartPlatformSeconds, FirstChunkPlatformSeconds);
	const double DtStreamToOnAnimStarted = DeltaSec(StreamStartPlatformSeconds, FirstOnAnimationStartedPlatformSeconds);
	const double DtUtteranceWall = DeltaSec(StreamStartPlatformSeconds, FinishPlatformSeconds);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey latency summary] Utterance=%d | clientT0->firstHttp=%.4fs clientT0->firstPcmChunk=%.4fs clientT0->firstAnimate=%.4fs clientT0->OnAnimationStarted=%.4fs | streamT0->firstPcm=%.4fs streamT0->OnAnimationStarted=%.4fs | utterance_wall=%.4fs | "
			 "(ACE CreateA2FStream / provider send / first blendshape / AudioComponent::Play: ace.PipelineTimingLog 1 on ACERuntime)"),
		UtteranceOrdinal,
		DtClientToHttp,
		DtClientToFirstPcm,
		DtClientToFirstAnimate,
		DtClientToOnAnimStarted,
		DtStreamToFirstPcm,
		DtStreamToOnAnimStarted,
		DtUtteranceWall);
}

void UGodfreyPcmStreamSession::LogGodfreyAceStartupCompletionSummary(const double FinishPlatformSeconds) const
{
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bLogGodfreyAceStartupCompletionSummary)
	{
		return;
	}

	auto MsBetween = [](double From, double To) -> double
	{
		if (From < 0.0 || To < 0.0)
		{
			return -1.0;
		}
		return (To - From) * 1000.0;
	};

	const double T0 = UtteranceStartupMetrics.UtteranceT0PlatformSeconds;
	const double WallMs = (T0 > 0.0 && FinishPlatformSeconds > 0.0) ? (FinishPlatformSeconds - T0) * 1000.0 : -1.0;

	const double MsT0ToFirstPcm = MsBetween(T0, UtteranceStartupMetrics.FirstPcmChunkPlatformSeconds);
	const double MsPcmToAnimate = MsBetween(UtteranceStartupMetrics.FirstPcmChunkPlatformSeconds, UtteranceStartupMetrics.FirstAnimateFromAudioCallPlatformSeconds);
	const double MsT0ToOnAnim = MsBetween(T0, UtteranceStartupMetrics.AceOnAnimationStartedPlatformSeconds);
	const double MsAnimateToOnAnim = MsBetween(UtteranceStartupMetrics.FirstAnimateFromAudioCallPlatformSeconds, UtteranceStartupMetrics.AceOnAnimationStartedPlatformSeconds);

	const bool bFallbackHint = !UtteranceStartupMetrics.bAceOnAnimationStartedObserved;

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey ACE startup summary] Utterance=%d wallMs=%.1f | OnAnimStarted=%s | t0->firstPcmMs=%.1f | firstPcm->firstAnimateMs=%.1f | firstAnimate->OnAnimMs=%.1f | t0->OnAnimMs=%.1f | bufferPriming=%s minBlendOverride=%s | if NO OnAnimStarted: check LogACERuntime for TryStart_Blocked / HoldPlay / ace.GodfreyStartupTiming=1"),
		UtteranceOrdinal,
		WallMs,
		UtteranceStartupMetrics.bAceOnAnimationStartedObserved ? TEXT("yes") : TEXT("NO"),
		MsT0ToFirstPcm,
		MsPcmToAnimate,
		MsAnimateToOnAnim,
		MsT0ToOnAnim,
		bGodfreyAceBufferLengthOverriddenThisUtterance ? TEXT("yes") : TEXT("no"),
		bGodfreyAceMinBlendOverriddenThisUtterance ? TEXT("yes") : TEXT("no"));

	if (bFallbackHint)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey ACE startup summary] Utterance=%d: OnAnimationStarted did NOT fire — correlate with ACE [ACE GodfreyStartup] lines (console: ace.GodfreyStartupTiming 1) and prior warnings in this utterance."),
			UtteranceOrdinal);
	}
}

void UGodfreyPcmStreamSession::ReportError(const FString& ErrorMessage)
{
	UE_LOG(LogGodfreyPcmStream, Error, TEXT("%s"), *ErrorMessage);
	OnError.Broadcast(ErrorMessage);
}
