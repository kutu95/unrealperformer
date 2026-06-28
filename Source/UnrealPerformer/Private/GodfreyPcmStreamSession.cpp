#include "GodfreyPcmStreamSession.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

#include "ACERuntimeModule.h"
#include "ACEBlueprintLibrary.h"
#include "ACEAudioCurveSourceComponent.h"
#include "UnrealPerformerGodfreySettings.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "Components/AudioComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#if WITH_EDITOR
#include "Settings/LevelEditorPlaySettings.h"
#endif
#include "Sound/SoundGroups.h"
#include "Sound/SoundWaveProcedural.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyPcmStream, Log, All);

namespace
{
static int32 GGodfreyUtteranceCounter = 0;

/** Bumped when parallel-audible startup logic changes — grep logs for this string to confirm binary matches source. */
static constexpr const TCHAR* GGodfreyParallelAudibleLogicStamp = TEXT("godfrey-parallel-audible-2026-06-28-v9");

/** PrimaryVolume = TransientPrimaryVolume * FApp::GetVolumeMultiplier(). AppMult=0 when the editor is unfocused (UnfocusedVolumeMultiplier default 0). */
static bool TryRestorePieAudibilityIfSilent(UWorld* World, const TCHAR* Context)
{
	if (!World || !World->IsPlayInEditor())
	{
		return false;
	}

	const FAudioDeviceHandle DeviceHandle = World->GetAudioDevice();
	FAudioDevice* AudioDevice = DeviceHandle.GetAudioDevice();
	if (!AudioDevice)
	{
		return false;
	}

	const float PrimaryVol = AudioDevice->GetPrimaryVolume();
	const float TransientVol = AudioDevice->GetTransientPrimaryVolume();
	const float AppMult = FApp::GetVolumeMultiplier();
	const bool bDeviceMuted = AudioDevice->IsAudioDeviceMuted();

	if (PrimaryVol > KINDA_SMALL_NUMBER && !bDeviceMuted)
	{
		return false;
	}

	bool bRestored = false;

	if (AppMult <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogGodfreyPcmStream, Error,
			TEXT("[Godfrey audible] FApp::GetVolumeMultiplier() is 0 at '%s' (editor unfocused or UnfocusedVolumeMultiplier=0). PrimaryVol=%.3f TransientVol=%.3f. "
			     "Restoring app volume to 1 for Godfrey playback — click the PIE viewport so it has focus, or set [Audio] UnfocusedVolumeMultiplier=1 in DefaultEngine.ini."),
			Context,
			PrimaryVol,
			TransientVol);
		FApp::SetVolumeMultiplier(1.f);
		bRestored = true;
	}

	if (TransientVol <= KINDA_SMALL_NUMBER)
	{
		AudioDevice->SetTransientPrimaryVolume(1.f);
		bRestored = true;
	}

	if (!bRestored)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible] PIE output still silent at '%s' (PrimaryVol=%.3f TransientVol=%.3f AppMult=%.3f DeviceMuted=%d)."),
			Context,
			PrimaryVol,
			TransientVol,
			AppMult,
			bDeviceMuted ? 1 : 0);
	}

	return bRestored;
}

static void GodfreyAudibleSelfTest()
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* Candidate = Context.World())
			{
				if (Candidate->WorldType == EWorldType::PIE || Candidate->WorldType == EWorldType::Game)
				{
					World = Candidate;
					break;
				}
			}
		}
	}

	if (!World)
	{
		UE_LOG(LogGodfreyPcmStream, Warning, TEXT("godfrey.AudibleSelfTest: no PIE/game world — start PIE first."));
		return;
	}

	TryRestorePieAudibilityIfSilent(World, TEXT("AudibleSelfTest"));

	constexpr int32 SampleRate = 48000;
	constexpr float DurationSec = 1.5f;
	constexpr float FrequencyHz = 440.f;
	const int32 NumFrames = FMath::RoundToInt(SampleRate * DurationSec);
	TArray<uint8> Pcm;
	Pcm.SetNumUninitialized(NumFrames * sizeof(int16));
	int16* Samples = reinterpret_cast<int16*>(Pcm.GetData());
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		const float T = static_cast<float>(FrameIndex) / static_cast<float>(SampleRate);
		Samples[FrameIndex] = static_cast<int16>(32767.f * 0.35f * FMath::Sin(2.f * UE_PI * FrequencyHz * T));
	}

	USoundWaveProcedural* Sound = NewObject<USoundWaveProcedural>(World);
	Sound->SetSampleRate(SampleRate);
	Sound->NumChannels = 1;
	Sound->Duration = DurationSec;
	Sound->TotalSamples = NumFrames;
	Sound->RawPCMDataSize = Pcm.Num();
	Sound->bLooping = false;
	Sound->SoundGroup = SOUNDGROUP_Default;
	Sound->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	Sound->QueueAudio(Pcm.GetData(), Pcm.Num());

	FVector PlayLocation = FVector::ZeroVector;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		FRotator PlayRotation;
		PC->GetPlayerViewPoint(PlayLocation, PlayRotation);
	}

	// One procedural wave must not feed two AudioComponents — that races the PCM queue and can crash AudioMixer.
	if (UAudioComponent* AC = UGameplayStatics::SpawnSoundAtLocation(
			World,
			Sound,
			PlayLocation,
			FRotator::ZeroRotator,
			1.f,
			1.f,
			0.f,
			nullptr,
			nullptr,
			true))
	{
		AC->bIsUISound = false;
		AC->bAllowSpatialization = false;
		AC->SetVolumeMultiplier(1.f);
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("godfrey.AudibleSelfTest: SpawnSoundAtLocation AC=%p IsPlaying=%d Loc=%s (~%.1fs, %s)."),
			AC,
			AC->IsPlaying() ? 1 : 0,
			*PlayLocation.ToString(),
			DurationSec,
			GGodfreyParallelAudibleLogicStamp);
	}
	else
	{
		UE_LOG(LogGodfreyPcmStream, Warning, TEXT("godfrey.AudibleSelfTest: SpawnSoundAtLocation failed."));
	}
}

static FAutoConsoleCommand CCmdGodfreyAudibleSelfTest(
	TEXT("godfrey.AudibleSelfTest"),
	TEXT("PIE: play 440Hz test tone via SpawnSoundAtLocation (single procedural player). If you hear nothing, the issue is editor/OS audio routing, not Godfrey PCM."),
	FConsoleCommandDelegate::CreateStatic(&GodfreyAudibleSelfTest));

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

static void ComputePcm16PeakRms(const TArray<uint8>& PcmBytes, int32 NumChannels, int16& OutPeak, float& OutRms)
{
	OutPeak = 0;
	OutRms = 0.f;
	const int32 FrameSize = NumChannels * static_cast<int32>(sizeof(int16));
	if (PcmBytes.Num() < FrameSize || (PcmBytes.Num() % FrameSize) != 0)
	{
		return;
	}

	const int32 NumSamples = PcmBytes.Num() / static_cast<int32>(sizeof(int16));
	const int16* Samples = reinterpret_cast<const int16*>(PcmBytes.GetData());
	int64 SumSquares = 0;
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		const int16 Sample = Samples[SampleIndex];
		OutPeak = static_cast<int16>(FMath::Max(static_cast<int32>(OutPeak), FMath::Abs(static_cast<int32>(Sample))));
		SumSquares += static_cast<int64>(Sample) * static_cast<int64>(Sample);
	}
	OutRms = (NumSamples > 0) ? FMath::Sqrt(static_cast<float>(SumSquares) / static_cast<float>(NumSamples)) : 0.f;
}

static const TCHAR* AudioComponentPlayStateToString(const EAudioComponentPlayState PlayState)
{
	switch (PlayState)
	{
	case EAudioComponentPlayState::Playing: return TEXT("Playing");
	case EAudioComponentPlayState::Stopped: return TEXT("Stopped");
	case EAudioComponentPlayState::Paused: return TEXT("Paused");
	case EAudioComponentPlayState::FadingIn: return TEXT("FadingIn");
	case EAudioComponentPlayState::FadingOut: return TEXT("FadingOut");
	default: return TEXT("Unknown");
	}
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
	CancelAudibleDiagnosticsTimer();
	UnbindParallelAudibleUnderflowDelegate();
	StopParallelAudiblePlayback(true);
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
				Prior->StopParallelAudiblePlayback(true);
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

void UGodfreyPcmStreamSession::LogAudiblePlaybackDiagnostics(const TCHAR* ContextLabel) const
{
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyLogAudiblePlaybackDiagnostics)
	{
		return;
	}

	AActor* Character = TargetCharacter.Get();
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	const Audio::FDeviceId DeviceId = World ? World->GetAudioDevice().GetDeviceID() : Audio::FDeviceId(INDEX_NONE);

	int32 MixerSampleRate = 0;
	int32 MixerOutputChannels = 0;
	if (World)
	{
		if (const Audio::FMixerDevice* Mixer = FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(World))
		{
			MixerSampleRate = Mixer->GetDeviceSampleRate();
			MixerOutputChannels = Mixer->GetDeviceOutputChannels();
		}
	}

	UACEAudioCurveSourceComponent* AceComp = Character ? Character->FindComponentByClass<UACEAudioCurveSourceComponent>() : nullptr;
	const float AceVolume = AceComp ? AceComp->Volume : -1.f;
	const bool bAceProceduralPlaying = AceComp ? AceComp->IsProceduralAudioPlaying() : false;
	const float AcePlaybackWallSec = AceComp ? AceComp->GetProceduralPlaybackWallClockSeconds() : -1.f;
	const float AceMaxCurveTs = AceComp ? AceComp->GetMaxReceivedCurveTimestamp() : -1.f;

	const UAudioComponent* ParallelAC = ParallelAudibleAudioComponent;
	const bool bParallelValid = IsValid(ParallelAC);
	const bool bParallelIsPlaying = bParallelValid && ParallelAC->IsPlaying();
	const EAudioComponentPlayState ParallelPlayState = bParallelValid ? ParallelAC->GetPlayState() : EAudioComponentPlayState::Stopped;
	const float ParallelVolMult = bParallelValid ? ParallelAC->VolumeMultiplier : -1.f;
	const bool bParallelUISound = bParallelValid && ParallelAC->bIsUISound;
	const bool bParallelSpatial = bParallelValid && ParallelAC->bAllowSpatialization;
	const bool bParallelAutoDestroy = bParallelValid && ParallelAC->bAutoDestroy;
	const USoundBase* ParallelSound = ParallelAudibleWave;

	int32 ProceduralAvailBytes = 0;
	int32 ProceduralWaveSampleRate = 0;
	int32 ProceduralWaveChannels = 0;
	float ProceduralWaveDuration = -1.f;
	if (ParallelAudibleWave)
	{
		ProceduralAvailBytes = ParallelAudibleWave->GetAvailableAudioByteCount();
		ProceduralWaveSampleRate = ParallelAudibleWave->GetSampleRateForCurrentPlatform();
		ProceduralWaveChannels = ParallelAudibleWave->NumChannels;
		ProceduralWaveDuration = ParallelAudibleWave->Duration;
	}

	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	const int32 EffectiveParallelSampleRate = GetParallelAudibleEffectiveSampleRate();
	const float ParallelQueuedAudioSec = (FrameSize > 0 && EffectiveParallelSampleRate > 0)
		? static_cast<float>(ParallelAudibleQueuedBytes) / (static_cast<float>(EffectiveParallelSampleRate) * static_cast<float>(FrameSize))
		: 0.f;
	const float ProceduralAvailAudioSec = (FrameSize > 0 && ProceduralWaveSampleRate > 0)
		? static_cast<float>(ProceduralAvailBytes) / (static_cast<float>(ProceduralWaveSampleRate) * static_cast<float>(FrameSize))
		: 0.f;

	bool bAudioDeviceMuted = false;
	float PrimaryVolume = -1.f;
	float TransientPrimaryVolume = -1.f;
	const float AppVolumeMultiplier = FApp::GetVolumeMultiplier();
#if WITH_EDITOR
	const bool bEnableGameSound = GetDefault<ULevelEditorPlaySettings>()->EnableGameSound;
#else
	const bool bEnableGameSound = true;
#endif
	if (World)
	{
		if (const FAudioDeviceHandle DeviceHandle = World->GetAudioDevice())
		{
			if (FAudioDevice* AudioDevice = DeviceHandle.GetAudioDevice())
			{
				bAudioDeviceMuted = AudioDevice->IsAudioDeviceMuted();
				PrimaryVolume = AudioDevice->GetPrimaryVolume();
				TransientPrimaryVolume = AudioDevice->GetTransientPrimaryVolume();
			}
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] Utterance=%d | %s | PIE=%d Finished=%d"),
		UtteranceOrdinal,
		ContextLabel,
		(World && World->IsPlayInEditor()) ? 1 : 0,
		bFinished ? 1 : 0);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] %s | Mixer DeviceId=%u MixerSR=%d MixerOutCh=%d UpsampleToMixer=%d StreamSR=%d StreamCh=%d DeviceMuted=%d PrimaryVol=%.3f TransientVol=%.3f AppMult=%.3f EnableGameSound=%d"),
		ContextLabel,
		static_cast<uint32>(DeviceId),
		MixerSampleRate,
		MixerOutputChannels,
		GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyUpsamplePcmToMixerRate ? 1 : 0,
		StreamSampleRate,
		StreamNumChannels,
		bAudioDeviceMuted ? 1 : 0,
		PrimaryVolume,
		TransientPrimaryVolume,
		AppVolumeMultiplier,
		bEnableGameSound ? 1 : 0);

	if (PrimaryVolume <= KINDA_SMALL_NUMBER && !bAudioDeviceMuted)
	{
		if (AppVolumeMultiplier <= KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("[Godfrey audible diag] %s | AppMult=0 (editor unfocused): click the PIE viewport or set UnfocusedVolumeMultiplier=1 in DefaultEngine.ini."),
				ContextLabel);
		}
		else
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("[Godfrey audible diag] %s | PrimaryVol~0: check PIE viewport volume slider and Editor Preferences → Play → Enable Game Sound."),
				ContextLabel);
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] %s | ACE Volume=%.3f AceProceduralPlaying=%d AcePlaybackWallSec=%.3f AceMaxCurveTs=%.3f AceMutedForParallel=%d"),
		ContextLabel,
		AceVolume,
		bAceProceduralPlaying ? 1 : 0,
		AcePlaybackWallSec,
		AceMaxCurveTs,
		bAceVolumeMutedForParallelLipSync ? 1 : 0);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] %s | Parallel active=%d started=%d AC=%p IsPlaying=%d PlayState=%s VolMult=%.3f UISound=%d Spatial=%d AutoDestroy=%d Sound=%p"),
		ContextLabel,
		bParallelAudibleActive ? 1 : 0,
		bParallelAudiblePlaybackStarted ? 1 : 0,
		ParallelAC,
		bParallelIsPlaying ? 1 : 0,
		AudioComponentPlayStateToString(ParallelPlayState),
		ParallelVolMult,
		bParallelUISound ? 1 : 0,
		bParallelSpatial ? 1 : 0,
		bParallelAutoDestroy ? 1 : 0,
		ParallelSound);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] %s | Procedural queuedBytes=%d (~%.2fs) availBytes=%d (~%.2fs) waveSR=%d waveCh=%d waveDur=%.2f queueCalls=%d underflows=%d"),
		ContextLabel,
		ParallelAudibleQueuedBytes,
		ParallelQueuedAudioSec,
		ProceduralAvailBytes,
		ProceduralAvailAudioSec,
		ProceduralWaveSampleRate,
		ProceduralWaveChannels,
		ProceduralWaveDuration,
		ParallelAudibleQueueCallCount,
		ParallelProceduralUnderflowCount);

	if (ParallelAC && bParallelAudiblePlaybackStarted && !bParallelIsPlaying)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible diag] %s | Parallel path reports started but AudioComponent IsPlaying=0 (PlayState=%s)."),
			ContextLabel,
			AudioComponentPlayStateToString(ParallelPlayState));
	}

	if (bParallelAudibleActive && bParallelAudiblePlaybackStarted && ProceduralAvailBytes <= 0 && ParallelAudibleQueuedBytes > 0)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible diag] %s | Procedural FIFO drained (availBytes=0) while game-thread queuedBytes=%d — possible underflow/starvation."),
			ContextLabel,
			ParallelAudibleQueuedBytes);
	}

	if (RollingPcmBytes.Num() > 0)
	{
		int16 RollingPeak = 0;
		float RollingRms = 0.f;
		ComputePcm16PeakRms(RollingPcmBytes, StreamNumChannels, RollingPeak, RollingRms);
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("[Godfrey audible diag] %s | RollingPcmBytes=%d streamPeak=%d streamRms=%.1f (source SR=%d before upsample)"),
			ContextLabel,
			RollingPcmBytes.Num(),
			static_cast<int32>(RollingPeak),
			RollingRms,
			StreamSampleRate);
		if (RollingPeak == 0)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("[Godfrey audible diag] %s | Entire rolling PCM buffer is silent (peak=0) — check brain stream / ElevenLabs output."),
				ContextLabel);
		}
	}
}

void UGodfreyPcmStreamSession::ScheduleAudibleDiagnosticsTimer(UWorld* World)
{
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyLogAudiblePlaybackDiagnostics || !World)
	{
		return;
	}

	CancelAudibleDiagnosticsTimer();
	AudibleDiagnosticsWorld = World;
	AudibleDiagnosticsTickCount = 0;
	World->GetTimerManager().SetTimer(
		AudibleDiagnosticsTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPcmStreamSession::AudibleDiagnosticsTimerTick),
		2.0f,
		true);
}

void UGodfreyPcmStreamSession::CancelAudibleDiagnosticsTimer()
{
	if (UWorld* World = AudibleDiagnosticsWorld.Get())
	{
		World->GetTimerManager().ClearTimer(AudibleDiagnosticsTimerHandle);
	}
	AudibleDiagnosticsWorld.Reset();
	AudibleDiagnosticsTimerHandle.Invalidate();
}

void UGodfreyPcmStreamSession::AudibleDiagnosticsTimerTick()
{
	++AudibleDiagnosticsTickCount;
	if (UWorld* World = AudibleDiagnosticsWorld.Get())
	{
		TryRestorePieAudibilityIfSilent(World, TEXT("periodic-tick"));
	}
	const FString Label = FString::Printf(TEXT("periodic-tick-%d"), AudibleDiagnosticsTickCount);
	LogAudiblePlaybackDiagnostics(*Label);

	if (bFinished || !bParallelAudiblePlaybackStarted)
	{
		CancelAudibleDiagnosticsTimer();
	}
}

void UGodfreyPcmStreamSession::BindParallelAudibleUnderflowDelegate()
{
	if (!ParallelAudibleWave)
	{
		return;
	}

	ParallelAudibleWave->OnSoundWaveProceduralUnderflow.Unbind();
	ParallelAudibleWave->OnSoundWaveProceduralUnderflow.BindUObject(this, &UGodfreyPcmStreamSession::HandleParallelProceduralUnderflow);
}

void UGodfreyPcmStreamSession::UnbindParallelAudibleUnderflowDelegate()
{
	if (ParallelAudibleWave)
	{
		ParallelAudibleWave->OnSoundWaveProceduralUnderflow.Unbind();
	}
}

void UGodfreyPcmStreamSession::HandleParallelProceduralUnderflow(USoundWaveProcedural* Wave, int32 SamplesRequired)
{
	++ParallelProceduralUnderflowCount;
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyLogAudiblePlaybackDiagnostics)
	{
		return;
	}

	const int32 AvailBytes = Wave ? Wave->GetAvailableAudioByteCount() : 0;
	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	const int32 SamplesRequiredBytes = (FrameSize > 0) ? (SamplesRequired * FrameSize) : 0;
	const bool bLikelyStarvation = AvailBytes < SamplesRequiredBytes;
	if (bLikelyStarvation && ParallelProceduralUnderflowCount <= 10)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible diag] procedural underflow #%d | Utterance=%d SamplesRequired=%d availBytes=%d queuedBytes(game)=%d started=%d likelyStarvation=1"),
			ParallelProceduralUnderflowCount,
			UtteranceOrdinal,
			SamplesRequired,
			AvailBytes,
			ParallelAudibleQueuedBytes,
			bParallelAudiblePlaybackStarted ? 1 : 0);
	}
	else if (bLikelyStarvation && ParallelProceduralUnderflowCount == 11)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible diag] procedural underflow | Utterance=%d — suppressing further starvation warnings (count>10)."),
			UtteranceOrdinal);
	}
	else
	{
		UE_LOG(LogGodfreyPcmStream, Verbose,
			TEXT("[Godfrey audible diag] procedural underflow #%d | Utterance=%d SamplesRequired=%d availBytes=%d queuedBytes(game)=%d started=%d likelyStarvation=0"),
			ParallelProceduralUnderflowCount,
			UtteranceOrdinal,
			SamplesRequired,
			AvailBytes,
			ParallelAudibleQueuedBytes,
			bParallelAudiblePlaybackStarted ? 1 : 0);
	}
}

void UGodfreyPcmStreamSession::PrepareFreshParallelAudibleWave(AActor* Character)
{
	UnbindParallelAudibleUnderflowDelegate();
	ParallelAudibleWave = nullptr;

	if (!Character)
	{
		return;
	}

	EnsureParallelAudibleWave(Character);
}

void UGodfreyPcmStreamSession::AbortParallelAudiblePlaybackForAceResync(bool bRestoreAceVolume)
{
	CancelAudibleDiagnosticsTimer();

	if (ParallelAudibleAudioComponent)
	{
		ParallelAudibleAudioComponent->Stop();
		ParallelAudibleAudioComponent->DestroyComponent();
		ParallelAudibleAudioComponent = nullptr;
	}

	if (ParallelAudibleWave)
	{
		ParallelAudibleWave->ResetAudio();
	}

	ParallelAudibleQueuedBytes = 0;
	bParallelAudiblePlaybackStarted = false;
	ParallelAudibleQueueCallCount = 0;
	ParallelProceduralUnderflowCount = 0;

	if (bRestoreAceVolume && bSavedAceVolumeForParallelMute)
	{
		if (AActor* Character = TargetCharacter.Get())
		{
			if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
			{
				AceComp->Volume = SavedAceVolumeBeforeParallelMute;
			}
		}
		bSavedAceVolumeForParallelMute = false;
	}

	bAceVolumeMutedForParallelLipSync = false;
}

void UGodfreyPcmStreamSession::EnsureParallelAudibleWave(AActor* Character)
{
	if (!Character || ParallelAudibleWave)
	{
		return;
	}

	// Match Test_Live_Audio WavUrlSoundLibrary: transient outer, native rate, finite duration set at queue time.
	ParallelAudibleWave = NewObject<USoundWaveProcedural>(GetTransientPackage());
	if (!ParallelAudibleWave)
	{
		UE_LOG(LogGodfreyPcmStream, Warning, TEXT("Godfrey utterance %d: failed to allocate parallel USoundWaveProcedural."), UtteranceOrdinal);
		return;
	}

	ParallelAudibleWave->SetSampleRate(GetParallelAudibleEffectiveSampleRate());
	ParallelAudibleWave->NumChannels = StreamNumChannels;
	ParallelAudibleWave->Duration = 0.f;
	ParallelAudibleWave->bLooping = false;
	ParallelAudibleWave->Volume = 1.f;
	ParallelAudibleWave->SoundGroup = SOUNDGROUP_Default;
	ParallelAudibleWave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	BindParallelAudibleUnderflowDelegate();
}

void UGodfreyPcmStreamSession::UpdateParallelAudibleWaveDuration()
{
	if (!ParallelAudibleWave || ParallelAudibleQueuedBytes <= 0)
	{
		return;
	}

	const int32 EffectiveSampleRate = GetParallelAudibleEffectiveSampleRate();
	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	if (EffectiveSampleRate <= 0 || FrameSize <= 0)
	{
		return;
	}

	const int32 NumFrames = ParallelAudibleQueuedBytes / FrameSize;
	ParallelAudibleWave->Duration = static_cast<float>(NumFrames) / static_cast<float>(EffectiveSampleRate);
	ParallelAudibleWave->TotalSamples = NumFrames * StreamNumChannels;
	ParallelAudibleWave->RawPCMDataSize = ParallelAudibleQueuedBytes;
	if (bFinished)
	{
		ParallelAudibleWave->bLooping = false;
	}
}

void UGodfreyPcmStreamSession::MuteAceVolumeForParallelLipSyncOnly()
{
	if (bAceVolumeMutedForParallelLipSync)
	{
		return;
	}

	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		return;
	}

	UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		return;
	}

	if (!bSavedAceVolumeForParallelMute)
	{
		SavedAceVolumeBeforeParallelMute = AceComp->Volume;
		bSavedAceVolumeForParallelMute = true;
	}

	AceComp->Volume = 0.f;
	bAceVolumeMutedForParallelLipSync = true;
}

void UGodfreyPcmStreamSession::QueueParallelAudiblePcm(const TArray<uint8>& PcmBytes)
{
	if (!bParallelAudibleActive || !ParallelAudibleWave || PcmBytes.Num() <= 0)
	{
		return;
	}

	TArray<uint8> AudiblePcm;
	int32 AudibleSampleRate = StreamSampleRate;
	UpsamplePcm16MonoForAudiblePlayback(PcmBytes, StreamSampleRate, AudiblePcm, AudibleSampleRate);
	// Sample rate is fixed in EnsureParallelAudibleWave — do not call SetSampleRate after Play(); that resets procedural playback.

	ParallelAudibleWave->QueueAudio(AudiblePcm.GetData(), AudiblePcm.Num());
	ParallelAudibleQueuedBytes += AudiblePcm.Num();
	if (bFinished)
	{
		UpdateParallelAudibleWaveDuration();
	}
	else if (ParallelAudibleWave)
	{
		ParallelAudibleWave->Duration = INDEFINITELY_LOOPING_DURATION;
	}
	++ParallelAudibleQueueCallCount;

	if (!bLoggedFirstParallelQueue)
	{
		bLoggedFirstParallelQueue = true;
		int16 Peak = 0;
		float Rms = 0.f;
		ComputePcm16PeakRms(AudiblePcm, StreamNumChannels, Peak, Rms);
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: first parallel PCM queue. ChunkBytes=%d AudibleBytes=%d AudibleSR=%d Peak=%d Rms=%.1f TotalQueued=%d (brain may send ~80ms lead silence first)"),
			UtteranceOrdinal,
			PcmBytes.Num(),
			AudiblePcm.Num(),
			AudibleSampleRate,
			static_cast<int32>(Peak),
			Rms,
			ParallelAudibleQueuedBytes);
		LogAudiblePlaybackDiagnostics(TEXT("first-parallel-queue"));
	}
	else if (!bLoggedFirstNonSilentParallelQueue)
	{
		int16 Peak = 0;
		float Rms = 0.f;
		ComputePcm16PeakRms(AudiblePcm, StreamNumChannels, Peak, Rms);
		if (Peak > 0)
		{
			bLoggedFirstNonSilentParallelQueue = true;
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("Godfrey utterance %d: first non-silent parallel PCM queue at call #%d. ChunkBytes=%d Peak=%d Rms=%.1f TotalQueued=%d"),
				UtteranceOrdinal,
				ParallelAudibleQueueCallCount,
				PcmBytes.Num(),
				static_cast<int32>(Peak),
				Rms,
				ParallelAudibleQueuedBytes);
		}
	}
	else if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyLogAudiblePlaybackDiagnostics
		&& (ParallelAudibleQueueCallCount % 100) == 0)
	{
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: parallel PCM queue milestone #%d TotalQueuedBytes=%d availProcedural=%d"),
			UtteranceOrdinal,
			ParallelAudibleQueueCallCount,
			ParallelAudibleQueuedBytes,
			ParallelAudibleWave->GetAvailableAudioByteCount());
	}
}

void UGodfreyPcmStreamSession::TryStartParallelAudiblePlayback(bool bIgnoreBufferThreshold, bool bAceSyncStart)
{
	if (!bParallelAudibleActive)
	{
		return;
	}

	if (bParallelAudiblePlaybackStarted && !bAceSyncStart)
	{
		return;
	}

	if (bParallelAudiblePlaybackStarted && bAceSyncStart)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: aborting early parallel audible play — resyncing to ACE OnAnimationStarted (%s)."),
			UtteranceOrdinal,
			GGodfreyParallelAudibleLogicStamp);
		AbortParallelAudiblePlaybackForAceResync(true);
	}

	AActor* Character = TargetCharacter.Get();
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: TryStartParallel aborted — no world."),
			UtteranceOrdinal);
		return;
	}

	PrepareFreshParallelAudibleWave(Character);
	if (!ParallelAudibleWave)
	{
		if (bIgnoreBufferThreshold || bAceSyncStart)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("Godfrey utterance %d: TryStartParallel skipped — failed to allocate wave."),
				UtteranceOrdinal);
		}
		return;
	}

	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	if (FrameSize <= 0)
	{
		if (bIgnoreBufferThreshold)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("Godfrey utterance %d: TryStartParallel aborted — invalid FrameSize."),
				UtteranceOrdinal);
		}
		return;
	}

	const int32 EffectiveSampleRate = GetParallelAudibleEffectiveSampleRate();
	if (EffectiveSampleRate <= 0)
	{
		if (bIgnoreBufferThreshold)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("Godfrey utterance %d: TryStartParallel aborted — invalid EffectiveSampleRate."),
				UtteranceOrdinal);
		}
		return;
	}

	// First play: prime procedural FIFO from RollingPcmBytes (ingest does not QueueAudio until after Play).
	int16 RollingPeak = 0;
	float RollingRms = 0.f;
	ComputePcm16PeakRms(RollingPcmBytes, StreamNumChannels, RollingPeak, RollingRms);
	if (RollingPcmBytes.Num() < FrameSize)
	{
		if (!bIgnoreBufferThreshold)
		{
			return;
		}
	}
	else if (RollingPeak == 0)
	{
		if (!bIgnoreBufferThreshold)
		{
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("Godfrey utterance %d: deferring parallel audible Play until non-silent PCM exists in rolling buffer (rollingBytes=%d)."),
				UtteranceOrdinal,
				RollingPcmBytes.Num());
			return;
		}
	}

	ParallelAudibleQueuedBytes = 0;

	int32 PcmStartOffset = 0;
	if (bAceSyncStart)
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			const float AcePlaybackSec = AceComp->GetProceduralPlaybackWallClockSeconds();
			if (AcePlaybackSec > KINDA_SMALL_NUMBER && StreamSampleRate > 0)
			{
				const int32 SkipFrames = FMath::Clamp(
					FMath::FloorToInt(AcePlaybackSec * static_cast<float>(StreamSampleRate)),
					0,
					RollingPcmBytes.Num() / FrameSize);
				PcmStartOffset = SkipFrames * FrameSize;
				if (PcmStartOffset > 0)
				{
					UE_LOG(LogGodfreyPcmStream, Log,
						TEXT("Godfrey utterance %d: ACE-sync parallel audible — skipping %.3fs (%d bytes) to match ACE playback clock."),
						UtteranceOrdinal,
						AcePlaybackSec,
						PcmStartOffset);
				}
			}
		}
	}

	const int32 RollingBytesToQueue = RollingPcmBytes.Num() - PcmStartOffset;
	if (RollingBytesToQueue >= FrameSize)
	{
		TArray<uint8> AudiblePcm;
		int32 AudibleSampleRate = StreamSampleRate;
		const TArrayView<const uint8> RollingSlice(RollingPcmBytes.GetData() + PcmStartOffset, RollingBytesToQueue);
		TArray<uint8> RollingSliceCopy(RollingSlice.GetData(), RollingBytesToQueue);
		UpsamplePcm16MonoForAudiblePlayback(RollingSliceCopy, StreamSampleRate, AudiblePcm, AudibleSampleRate);
		if (AudiblePcm.Num() >= FrameSize)
		{
			const int32 NumFrames = AudiblePcm.Num() / FrameSize;
			ParallelAudibleWave->SetSampleRate(AudibleSampleRate);
			ParallelAudibleWave->NumChannels = StreamNumChannels;
			ParallelAudibleWave->bLooping = false;
			ParallelAudibleWave->SoundGroup = SOUNDGROUP_Default;
			ParallelAudibleWave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
			if (bFinished)
			{
				ParallelAudibleWave->Duration = static_cast<float>(NumFrames) / static_cast<float>(AudibleSampleRate);
				ParallelAudibleWave->TotalSamples = NumFrames * StreamNumChannels;
				ParallelAudibleWave->RawPCMDataSize = AudiblePcm.Num();
			}
			else
			{
				ParallelAudibleWave->Duration = INDEFINITELY_LOOPING_DURATION;
				ParallelAudibleWave->TotalSamples = 0;
				ParallelAudibleWave->RawPCMDataSize = 0;
			}
			ParallelAudibleWave->QueueAudio(AudiblePcm.GetData(), AudiblePcm.Num());
			ParallelAudibleQueuedBytes = AudiblePcm.Num();
		}
	}

	if (ParallelAudibleQueuedBytes < FrameSize)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: parallel audible Play aborted — no primed PCM (rollingBytes=%d queuedAudibleBytes=%d rollingPeak=%d)."),
			UtteranceOrdinal,
			RollingPcmBytes.Num(),
			ParallelAudibleQueuedBytes,
			static_cast<int32>(RollingPeak));
		return;
	}

	if (!bIgnoreBufferThreshold)
	{
		const float BufferSec = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAceBufferLengthSeconds;
		const int32 MinBytes = FMath::Max(FrameSize, FMath::RoundToInt(BufferSec * static_cast<float>(EffectiveSampleRate)) * FrameSize);
		if (ParallelAudibleQueuedBytes < MinBytes)
		{
			return;
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: priming parallel audible at ACE sync (%s). rollingBytes=%d pcmOffset=%d audibleQueuedBytes=%d rollingPeak=%d waveDuration=%.3fs finished=%d"),
		UtteranceOrdinal,
		GGodfreyParallelAudibleLogicStamp,
		RollingPcmBytes.Num(),
		PcmStartOffset,
		ParallelAudibleQueuedBytes,
		static_cast<int32>(RollingPeak),
		ParallelAudibleWave ? ParallelAudibleWave->Duration : 0.f,
		bFinished ? 1 : 0);

	if (ParallelAudibleAudioComponent)
	{
		ParallelAudibleAudioComponent->Stop();
		ParallelAudibleAudioComponent->DestroyComponent();
		ParallelAudibleAudioComponent = nullptr;
	}

	if (bFinished)
	{
		UnbindParallelAudibleUnderflowDelegate();
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	TryRestorePieAudibilityIfSilent(World, TEXT("TryStartParallelAudiblePlayback"));
	bool bSpawnedAudible = false;
	if (Settings->bGodfreyAudibleSpawnAtPlayerLocation)
	{
		FVector PlayLocation = Character ? Character->GetActorLocation() : FVector::ZeroVector;
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FRotator PlayRotation;
			PC->GetPlayerViewPoint(PlayLocation, PlayRotation);
		}

		ParallelAudibleAudioComponent = UGameplayStatics::SpawnSoundAtLocation(
			World,
			ParallelAudibleWave,
			PlayLocation,
			FRotator::ZeroRotator,
			1.f,
			1.f,
			0.f,
			nullptr,
			nullptr,
			false);

		if (ParallelAudibleAudioComponent)
		{
			ParallelAudibleAudioComponent->bIsUISound = false;
			ParallelAudibleAudioComponent->bAllowSpatialization = false;
			ParallelAudibleAudioComponent->SetVolumeMultiplier(1.f);
			bSpawnedAudible = true;
		}
	}

	if (!bSpawnedAudible)
	{
		UGameplayStatics::PlaySound2D(World, ParallelAudibleWave);
		ParallelAudibleAudioComponent = nullptr;
	}

	bParallelAudiblePlaybackStarted = true;

	if (Settings->bGodfreyMuteAceWhenParallelAudibleStarts)
	{
		MuteAceVolumeForParallelLipSyncOnly();
	}

	const float QueuedAudioSec = static_cast<float>(ParallelAudibleQueuedBytes) / (static_cast<float>(EffectiveSampleRate) * static_cast<float>(FrameSize));
	const bool bParallelIsPlaying = ParallelAudibleAudioComponent ? ParallelAudibleAudioComponent->IsPlaying() : false;
	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: audible play (%s). spawnAtPlayer=%d AC=%p IsPlaying=%d QueuedBytes=%d SR=%d ~%.2fs waveDur=%.3fs ACE mute=%d."),
		UtteranceOrdinal,
		GGodfreyParallelAudibleLogicStamp,
		Settings->bGodfreyAudibleSpawnAtPlayerLocation ? 1 : 0,
		ParallelAudibleAudioComponent.Get(),
		bParallelIsPlaying ? 1 : 0,
		ParallelAudibleQueuedBytes,
		EffectiveSampleRate,
		QueuedAudioSec,
		ParallelAudibleWave ? ParallelAudibleWave->Duration : 0.f,
		bAceVolumeMutedForParallelLipSync ? 1 : 0);

	LogAudiblePlaybackDiagnostics(TEXT("parallel-play-started"));
	if (AActor* CharacterForTimer = TargetCharacter.Get())
	{
		ScheduleAudibleDiagnosticsTimer(CharacterForTimer->GetWorld());
	}
}

int32 UGodfreyPcmStreamSession::GetParallelAudibleEffectiveSampleRate() const
{
	constexpr int32 MixerSampleRate = 48000;
	if (StreamSampleRate <= 0)
	{
		return MixerSampleRate;
	}

	if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyUpsamplePcmToMixerRate
		&& StreamSampleRate < MixerSampleRate
		&& (MixerSampleRate % StreamSampleRate) == 0)
	{
		return MixerSampleRate;
	}

	return StreamSampleRate;
}

void UGodfreyPcmStreamSession::StopParallelAudiblePlayback(bool bRestoreAceVolume)
{
	CancelAudibleDiagnosticsTimer();
	UnbindParallelAudibleUnderflowDelegate();

	if (ParallelAudibleAudioComponent)
	{
		ParallelAudibleAudioComponent->Stop();
		ParallelAudibleAudioComponent->DestroyComponent();
		ParallelAudibleAudioComponent = nullptr;
	}

	if (ParallelAudibleWave)
	{
		ParallelAudibleWave->ResetAudio();
		ParallelAudibleWave = nullptr;
	}
	bParallelAudiblePlaybackStarted = false;
	ParallelAudibleQueuedBytes = 0;

	if (bRestoreAceVolume && bSavedAceVolumeForParallelMute)
	{
		if (AActor* Character = TargetCharacter.Get())
		{
			if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
			{
				AceComp->Volume = SavedAceVolumeBeforeParallelMute;
			}
		}
		bSavedAceVolumeForParallelMute = false;
	}

	bAceVolumeMutedForParallelLipSync = false;
	bParallelAudibleActive = false;
}

void UGodfreyPcmStreamSession::UpsamplePcm16MonoForAudiblePlayback(
	const TArray<uint8>& SourcePcm,
	int32 SourceSampleRate,
	TArray<uint8>& OutPcm,
	int32& OutSampleRate) const
{
	OutPcm = SourcePcm;
	OutSampleRate = SourceSampleRate;

	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyUpsamplePcmToMixerRate || SourceSampleRate <= 0)
	{
		return;
	}

	constexpr int32 MixerSampleRate = 48000;
	if (SourceSampleRate >= MixerSampleRate || (MixerSampleRate % SourceSampleRate) != 0)
	{
		return;
	}

	const int32 UpsampleFactor = MixerSampleRate / SourceSampleRate;
	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	if (FrameSize <= 0 || SourcePcm.Num() < FrameSize || (SourcePcm.Num() % FrameSize) != 0)
	{
		return;
	}

	const int32 NumFrames = SourcePcm.Num() / FrameSize;
	const int16* Src = reinterpret_cast<const int16*>(SourcePcm.GetData());
	OutPcm.SetNumUninitialized(NumFrames * UpsampleFactor * FrameSize);
	int16* Dst = reinterpret_cast<int16*>(OutPcm.GetData());

	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		for (int32 ChannelIndex = 0; ChannelIndex < StreamNumChannels; ++ChannelIndex)
		{
			const int16 Sample = Src[FrameIndex * StreamNumChannels + ChannelIndex];
			for (int32 RepeatIndex = 0; RepeatIndex < UpsampleFactor; ++RepeatIndex)
			{
				Dst[(FrameIndex * UpsampleFactor + RepeatIndex) * StreamNumChannels + ChannelIndex] = Sample;
			}
		}
	}

	OutSampleRate = MixerSampleRate;
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

	if (!bParallelAudiblePlaybackStarted)
	{
		TryStartParallelAudiblePlayback(true, false);
	}

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

	LogAudiblePlaybackDiagnostics(TEXT("OnAnimationStarted"));

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

	LogAudiblePlaybackDiagnostics(TEXT("OnAnimationEnded"));
	StopParallelAudiblePlayback(true);

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
	StopParallelAudiblePlayback(false);

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
	bParallelAudiblePlaybackStarted = false;
	ParallelAudibleQueuedBytes = 0;
	ParallelAudibleQueueCallCount = 0;
	ParallelProceduralUnderflowCount = 0;
	AudibleDiagnosticsTickCount = 0;
	bLoggedFirstParallelQueue = false;
	bLoggedFirstNonSilentParallelQueue = false;
	bParallelAudibleActive = false;
	bAceVolumeMutedForParallelLipSync = false;
	bSavedAceVolumeForParallelMute = false;

	ApplyGodfreyAcePlaybackPriming(AceComp);

	const bool bUseParallelAudible = GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyUseParallelPcmAudiblePlayback;
	if (bUseParallelAudible)
	{
		EnsureParallelAudibleWave(CharacterForAce);
		if (ParallelAudibleWave)
		{
			bParallelAudibleActive = true;
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("Godfrey utterance %d: parallel PCM audible path armed (%s; WavUrl PlaySound2D at ACE OnAnimationStarted)."),
				UtteranceOrdinal,
				GGodfreyParallelAudibleLogicStamp);
		}
		else
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("Godfrey utterance %d: parallel PCM wave allocation failed — falling back to ACE internal audible path."),
				UtteranceOrdinal);
		}
	}

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
		TEXT("Godfrey utterance %d: fresh session — ACE OnAnimationStarted/OnAnimationEnded bound; audible=%s Character=%s"),
		UtteranceOrdinal,
		bParallelAudibleActive ? TEXT("WavUrl PlaySound2D after OnAnimationStarted") : TEXT("ACE internal AudioComponent after Play()"),
		*CharacterForAce->GetName());

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Stream started. UtteranceOrdinal=%d SampleRate=%d Channels=%d Provider=%s Character=%s ACE_BufferLengthSec=%.4f ACE_Volume=%.3f ParallelAudible=%d StreamT0=%.6f ClientReqT0=%.6f"),
		UtteranceOrdinal,
		StreamSampleRate,
		StreamNumChannels,
		*AceProviderName.ToString(),
		*CharacterForAce->GetName(),
		AceComp->BufferLengthInSeconds,
		AceComp->Volume,
		bParallelAudibleActive ? 1 : 0,
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

	if (bParallelAudibleActive)
	{
		if (bParallelAudiblePlaybackStarted)
		{
			QueueParallelAudiblePcm(PcmBytes);
		}
	}

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

	LogAudiblePlaybackDiagnostics(TEXT("FinishStream"));

	RestoreGodfreyAcePlaybackPrimingIfApplied();

	UWorld* CharacterWorld = nullptr;
	if (AActor* Character = TargetCharacter.Get())
	{
		CharacterWorld = Character->GetWorld();
	}

	const float DelegateGraceSeconds = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAcePostFinishOnAnimationStartedDelegateGraceSeconds;
	bFinished = true;

	if (bParallelAudibleActive
		&& GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyPlayAudibleAtFinishStream
		&& !bParallelAudiblePlaybackStarted)
	{
		TryStartParallelAudiblePlayback(true, false);
	}

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
	StopParallelAudiblePlayback(true);
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
