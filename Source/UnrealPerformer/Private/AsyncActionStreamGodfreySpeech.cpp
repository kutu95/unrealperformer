#include "AsyncActionStreamGodfreySpeech.h"



#include "GodfreyPcmStreamSession.h"

#include "GodfreyPerformanceStateComponent.h"

#include "UnrealPerformerGodfreySettings.h"

#include "ACEAudioCurveSourceComponent.h"



#include "Async/Async.h"

#include "Dom/JsonObject.h"

#include "Dom/JsonValue.h"

#include "HAL/PlatformTime.h"

#include "HAL/UnrealMemory.h"

#include "HttpModule.h"

#include "Interfaces/IHttpRequest.h"

#include "Interfaces/IHttpResponse.h"

#include "Misc/ScopeLock.h"

#include "Serialization/JsonSerializer.h"

#include "Serialization/JsonWriter.h"

#include "Serialization/JsonReader.h"

#include "Engine/Engine.h"
#include "EngineUtils.h"



DEFINE_LOG_CATEGORY_STATIC(LogGodfreySpeechStreamNode, Log, All);



namespace

{

FString NormalizeGodfreyBaseUrl(const FString& InUrl)

{

	FString Url = InUrl;

	while (Url.EndsWith(TEXT("/")))

	{

		Url.LeftChopInline(1, EAllowShrinking::No);

	}

	return Url;

}

AActor* ResolveExhibitionCharacterForAce(UObject* WorldContextObject, AActor* ExplicitCharacter)

{

	if (IsValid(ExplicitCharacter))

	{

		return ExplicitCharacter;

	}

	UWorld* const World = GEngine

		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)

		: nullptr;

	if (!World)

	{

		return nullptr;

	}

	static const FName GodfreyCharacterTag(TEXT("GodfreyCharacter"));

	for (TActorIterator<AActor> It(World); It; ++It)

	{

		AActor* const Actor = *It;

		if (IsValid(Actor) && Actor->ActorHasTag(GodfreyCharacterTag))

		{

			return Actor;

		}

	}

	return nullptr;

}

}



void UAsyncActionStreamGodfreySpeech::LogGodfreyPerformanceEventsFromStatusJson(const TSharedPtr<FJsonObject>& JsonObj)

{

	if (!JsonObj.IsValid())

	{

		return;

	}

	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;

	if (!JsonObj->TryGetArrayField(TEXT("performanceEvents"), Events) || Events == nullptr || Events->Num() == 0)

	{

		UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("Godfrey performance events: none"));

		return;

	}

	UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("Godfrey performance events received:"));

	for (const TSharedPtr<FJsonValue>& Entry : *Events)

	{

		if (!Entry.IsValid() || Entry->Type != EJson::Object)

		{

			continue;

		}

		const TSharedPtr<FJsonObject> EvObj = Entry->AsObject();

		if (!EvObj.IsValid())

		{

			continue;

		}

		FString Type;

		FString Value;

		FString Raw;

		EvObj->TryGetStringField(TEXT("type"), Type);

		EvObj->TryGetStringField(TEXT("value"), Value);

		EvObj->TryGetStringField(TEXT("raw"), Raw);

		UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("- %s: %s (%s)"), *Type, *Value, *Raw);

		UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("Broadcasting performance cue: type=%s value=%s raw=%s"), *Type, *Value, *Raw);

		OnPerformanceCue.Broadcast(Type, Value, Raw);

		TryForwardPerformanceCueToPerformer(Type, Value, Raw);

	}

}



UAsyncActionStreamGodfreySpeech* UAsyncActionStreamGodfreySpeech::StreamGodfreySpeechToAudio(

	UObject* InWorldContextObject,

	const FString& InPromptText,

	const FString& InGodfreyBrainBaseUrl,

	AActor* InCharacterForAce,

	FName InProviderName,

	int32 InSampleRate,

	int32 InNumChannels)

{

	UAsyncActionStreamGodfreySpeech* Action = NewObject<UAsyncActionStreamGodfreySpeech>();

	Action->WorldContextObject = InWorldContextObject;

	Action->PromptText = InPromptText;

	Action->GodfreyBrainBaseUrl = InGodfreyBrainBaseUrl;

	Action->CharacterForAce = InCharacterForAce;

	Action->ProviderName = InProviderName;

	Action->SampleRate = InSampleRate;

	Action->NumChannels = InNumChannels;

	Action->bPullQueuedMode = false;

	Action->RegisterWithGameInstance(InWorldContextObject);

	return Action;

}



UAsyncActionStreamGodfreySpeech* UAsyncActionStreamGodfreySpeech::PullQueuedGodfreySpeechToAudio(

	UObject* InWorldContextObject,

	const FString& InGodfreyBrainBaseUrl,

	AActor* InCharacterForAce,

	FName InProviderName,

	int32 InSampleRate,

	int32 InNumChannels)

{

	UAsyncActionStreamGodfreySpeech* Action = NewObject<UAsyncActionStreamGodfreySpeech>();

	Action->WorldContextObject = InWorldContextObject;

	Action->GodfreyBrainBaseUrl = InGodfreyBrainBaseUrl;

	Action->CharacterForAce = InCharacterForAce;

	Action->ProviderName = InProviderName;

	Action->SampleRate = InSampleRate > 0 ? InSampleRate : 24000;

	Action->NumChannels = InNumChannels > 0 ? InNumChannels : 1;

	Action->bPullQueuedMode = true;

	Action->RegisterWithGameInstance(InWorldContextObject);

	return Action;

}



void UAsyncActionStreamGodfreySpeech::Activate()

{

	bDidForwardUtteranceStartedToPerformer = false;

	bDidForwardUtteranceEndedToPerformer = false;

	if (!WorldContextObject)

	{

		FailAndStop(TEXT("Godfrey speech async: invalid WorldContextObject."));

		return;

	}

	if (GodfreyBrainBaseUrl.IsEmpty())

	{

		FailAndStop(TEXT("Godfrey speech async: GodfreyBrainBaseUrl is empty."));

		return;

	}

	if (!CharacterForAce)

	{

		CharacterForAce = ResolveExhibitionCharacterForAce(WorldContextObject, nullptr);

		if (CharacterForAce)

		{

			UE_LOG(LogGodfreySpeechStreamNode, Log,

				TEXT("Resolved CharacterForAce via GodfreyCharacter tag -> %s"),

				*CharacterForAce->GetName());

		}

	}



	if (bPullQueuedMode)

	{

		if (!CharacterForAce)

		{

			FailAndStop(TEXT("PullQueuedGodfreySpeechToAudio: CharacterForAce is required."));

			return;

		}

		const FString StatusUrl = BuildExhibitionTtsStatusUrl();

		UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("PullQueuedGodfreySpeechToAudio: exhibition TTS status poll started GET %s"), *StatusUrl);

		StartExhibitionTtsStatusGet();

		return;

	}



	if (PromptText.IsEmpty())

	{

		FailAndStop(TEXT("StreamGodfreySpeechToAudio: PromptText is empty."));

		return;

	}



	StreamSession = NewObject<UGodfreyPcmStreamSession>(this);

	if (!StreamSession)

	{

		FailAndStop(TEXT("StreamGodfreySpeechToAudio: failed to create PCM stream session."));

		return;

	}



	const double ClientRequestT0 = FPlatformTime::Seconds();

	StreamSession->SetClientRequestT0PlatformSeconds(ClientRequestT0);



	StreamSession->OnPlaybackStarted.AddDynamic(this, &UAsyncActionStreamGodfreySpeech::HandleSessionPlaybackStarted);

	StreamSession->OnLipSyncStarted.AddDynamic(this, &UAsyncActionStreamGodfreySpeech::HandleSessionLipSyncStarted);

	StreamSession->OnPlaybackEnded.AddDynamic(this, &UAsyncActionStreamGodfreySpeech::HandleSessionPlaybackEnded);

	StreamSession->OnError.AddDynamic(this, &UAsyncActionStreamGodfreySpeech::HandleSessionError);



	if (!StreamSession->StartStream(WorldContextObject, CharacterForAce, ProviderName, SampleRate, NumChannels))

	{

		FailAndStop(TEXT("StreamGodfreySpeechToAudio: failed to start PCM stream session."));

		return;

	}



	UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("Stream start requested. BaseUrl=%s Provider=%s SampleRate=%d Channels=%d ClientRequestT0=%.6f"),

		*GodfreyBrainBaseUrl, *ProviderName.ToString(), SampleRate, NumChannels, ClientRequestT0);



	StartSpeakStreamPcmPost();

}



FString UAsyncActionStreamGodfreySpeech::BuildStreamUrl() const

{

	return NormalizeGodfreyBaseUrl(GodfreyBrainBaseUrl) + TEXT("/api/godfrey/speak/stream-pcm");

}



FString UAsyncActionStreamGodfreySpeech::BuildExhibitionTtsStatusUrl() const

{

	return NormalizeGodfreyBaseUrl(GodfreyBrainBaseUrl) + TEXT("/api/exhibition/unreal-tts-status");

}



void UAsyncActionStreamGodfreySpeech::StartExhibitionTtsStatusGet()

{

	FHttpModule& Http = FHttpModule::Get();

	ActiveRequest = Http.CreateRequest();

	ActiveRequest->SetURL(BuildExhibitionTtsStatusUrl());

	ActiveRequest->SetVerb(TEXT("GET"));

	ActiveRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));

	ActiveRequest->SetTimeout(60.f);



	const TWeakObjectPtr<UAsyncActionStreamGodfreySpeech> WeakThis(this);

	ActiveRequest->OnProcessRequestComplete().BindLambda([WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)

	{

		if (UAsyncActionStreamGodfreySpeech* Strong = WeakThis.Get())

		{

			Strong->HandleExhibitionTtsStatusCompleted(Req, Res, bOk);

		}

	});



	if (!ActiveRequest->ProcessRequest())

	{

		FailAndStop(TEXT("PullQueuedGodfreySpeechToAudio: failed to start TTS status GET."));

	}

}



void UAsyncActionStreamGodfreySpeech::HandleExhibitionTtsStatusCompleted(

	FHttpRequestPtr /*HttpRequest*/, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)

{

	ActiveRequest.Reset();



	if (bDidFinish)

	{

		return;

	}



	if (!bConnectedSuccessfully || !HttpResponse.IsValid())

	{

		FailAndStop(TEXT("PullQueuedGodfreySpeechToAudio: TTS status GET failed (no response)."));

		return;

	}



	const int32 Code = HttpResponse->GetResponseCode();

	if (Code < 200 || Code >= 300)

	{

		if (Code == 409)

		{

			UE_LOG(LogGodfreySpeechStreamNode, Error, TEXT("PullQueuedGodfreySpeechToAudio: TTS status GET returned HTTP 409 Conflict."));

		}

		FailAndStop(FString::Printf(TEXT("PullQueuedGodfreySpeechToAudio: TTS status GET failed HTTP=%d"), Code));

		return;

	}



	const FString BodyStr = HttpResponse->GetContentAsString();

	TSharedPtr<FJsonObject> JsonObj;

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);

	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())

	{

		FailAndStop(TEXT("PullQueuedGodfreySpeechToAudio: TTS status response JSON parse failed."));

		return;

	}



	const bool bReady = JsonObj->HasField(TEXT("ready")) && JsonObj->GetBoolField(TEXT("ready"));

	if (!bReady)

	{

		UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("No queued Godfrey speech."));

		bDidFinish = true;

		OnNoQueue.Broadcast();

		SetReadyToDestroy();

		return;

	}



	LogGodfreyPerformanceEventsFromStatusJson(JsonObj);



	FString ReqId;

	if (!JsonObj->TryGetStringField(TEXT("requestId"), ReqId) || ReqId.IsEmpty())

	{

		FailAndStop(TEXT("PullQueuedGodfreySpeechToAudio: queue ready but requestId missing or empty."));

		return;

	}



	QueuedTtsRequestId = ReqId;

	UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("PullQueuedGodfreySpeechToAudio: Queue ready, requestId=%s"), *QueuedTtsRequestId);



	{

		FScopeLock Lock(&HttpBodyLock);

		HttpBodyAccum.Reset();

	}

	PendingBytes.Reset();

	bHttpBodyDrainPending = false;

	bSawFirstHttpProgress = false;

	bLoggedFormat = false;

	FirstHttpProgressPlatformSeconds = -1.0;

	FirstStreamDelegatePlatformSeconds = -1.0;

	FirstGameThreadPcmPlatformSeconds = -1.0;

	HttpStreamChunkCount = 0;

	TotalHttpBodyBytesReceived = 0;

	AnimateFromAudioPushCount = 0;



	const double ClientRequestT0 = FPlatformTime::Seconds();

	StreamSession = NewObject<UGodfreyPcmStreamSession>(this);

	if (!StreamSession)

	{

		FailAndStop(TEXT("PullQueuedGodfreySpeechToAudio: failed to create PCM stream session."));

		return;

	}



	StreamSession->SetClientRequestT0PlatformSeconds(ClientRequestT0);

	StreamSession->OnPlaybackStarted.AddDynamic(this, &UAsyncActionStreamGodfreySpeech::HandleSessionPlaybackStarted);

	StreamSession->OnLipSyncStarted.AddDynamic(this, &UAsyncActionStreamGodfreySpeech::HandleSessionLipSyncStarted);

	StreamSession->OnPlaybackEnded.AddDynamic(this, &UAsyncActionStreamGodfreySpeech::HandleSessionPlaybackEnded);

	StreamSession->OnError.AddDynamic(this, &UAsyncActionStreamGodfreySpeech::HandleSessionError);



	if (!StreamSession->StartStream(WorldContextObject, CharacterForAce, ProviderName, SampleRate, NumChannels))

	{

		FailAndStop(TEXT("PullQueuedGodfreySpeechToAudio: failed to start PCM stream session."));

		return;

	}



	UE_LOG(LogGodfreySpeechStreamNode, Log,

		TEXT("PullQueuedGodfreySpeechToAudio: stream-pcm ttsOnly POST started url=%s requestId=%s sampleRate=%d numChannels=%d"),

		*BuildStreamUrl(),

		*QueuedTtsRequestId,

		SampleRate,

		NumChannels);



	StartSpeakStreamPcmPost();

}



void UAsyncActionStreamGodfreySpeech::StartSpeakStreamPcmPost()

{

	FHttpModule& Http = FHttpModule::Get();

	ActiveRequest = Http.CreateRequest();

	const FString Url = BuildStreamUrl();



	TSharedPtr<FJsonObject> BodyObject = MakeShared<FJsonObject>();

	if (bPullQueuedMode)

	{

		BodyObject->SetBoolField(TEXT("ttsOnly"), true);

		BodyObject->SetStringField(TEXT("requestId"), QueuedTtsRequestId);

		BodyObject->SetNumberField(TEXT("sampleRate"), SampleRate);

		BodyObject->SetNumberField(TEXT("numChannels"), NumChannels);

	}

	else

	{

		BodyObject->SetStringField(TEXT("text"), PromptText);

		BodyObject->SetNumberField(TEXT("sampleRate"), SampleRate);

		BodyObject->SetNumberField(TEXT("numChannels"), NumChannels);

	}



	FString BodyString;

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);

	FJsonSerializer::Serialize(BodyObject.ToSharedRef(), Writer);



	ActiveRequest->SetURL(Url);

	ActiveRequest->SetVerb(TEXT("POST"));

	ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	ActiveRequest->SetHeader(TEXT("Accept"), TEXT("audio/L16, application/octet-stream, audio/wav"));

	ActiveRequest->SetContentAsString(BodyString);

	ActiveRequest->SetTimeout(180.f);



	const double HttpRequestStartSeconds = FPlatformTime::Seconds();



	ActiveRequest->OnRequestProgress64().BindLambda([this](FHttpRequestPtr /*Request*/, uint64 /*BytesSent*/, uint64 BytesReceived)

	{

		if (!this || bDidFinish)

		{

			return;

		}

		HandleRequestProgress64(BytesReceived);

	});



	ActiveRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr /*Request*/, FHttpResponsePtr Response, bool bConnectedSuccessfully)

	{

		if (!this || bDidFinish)

		{

			return;

		}

		const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;

		const FString CompletionError = (!bConnectedSuccessfully || !Response.IsValid())

			? TEXT("HTTP stream request failed or response invalid.")

			: FString();

		HandleRequestCompleted(bConnectedSuccessfully, ResponseCode, CompletionError);

	});



	const TWeakObjectPtr<UAsyncActionStreamGodfreySpeech> WeakThis(this);

	const bool bStreamDelegateOk = ActiveRequest->SetResponseBodyReceiveStreamDelegateV2(

		FHttpRequestStreamDelegateV2::CreateLambda([WeakThis, HttpRequestStartSeconds](void* Ptr, int64& Length)

		{

			const int64 InLength = Length;

			if (InLength <= 0 || !Ptr)

			{

				return;

			}



			UAsyncActionStreamGodfreySpeech* Strong = WeakThis.Get();

			if (!Strong || Strong->bDidFinish)

			{

				return;

			}



			if (Strong->FirstStreamDelegatePlatformSeconds < 0.0)

			{

				Strong->FirstStreamDelegatePlatformSeconds = FPlatformTime::Seconds();

				UE_LOG(LogGodfreySpeechStreamNode, Log,

					TEXT("HTTP body stream: first Serialize callback (HTTP thread). ChunkBytes=%lld PlatformTime=%.6f DeltaFromRequestStart=%.3fs"),

					InLength,

					Strong->FirstStreamDelegatePlatformSeconds,

					Strong->FirstStreamDelegatePlatformSeconds - HttpRequestStartSeconds);

			}



			++Strong->HttpStreamChunkCount;

			Strong->TotalHttpBodyBytesReceived += InLength;



			TArray<uint8> Local;

			Local.SetNumUninitialized(static_cast<int32>(InLength));

			FMemory::Memcpy(Local.GetData(), Ptr, InLength);



			{

				FScopeLock Lock(&Strong->HttpBodyLock);

				Strong->HttpBodyAccum.Append(Local);

			}



			Strong->ScheduleHttpBodyDrain();

		}));



	if (!bStreamDelegateOk)

	{

		FailAndStop(bPullQueuedMode

			? TEXT("PullQueuedGodfreySpeechToAudio: SetResponseBodyReceiveStreamDelegateV2 failed (streaming receive not available).")

			: TEXT("StreamGodfreySpeechToAudio: SetResponseBodyReceiveStreamDelegateV2 failed (streaming receive not available)."));

		return;

	}



	if (!ActiveRequest->ProcessRequest())

	{

		FailAndStop(bPullQueuedMode

			? TEXT("PullQueuedGodfreySpeechToAudio: failed to start stream-pcm POST.")

			: TEXT("StreamGodfreySpeechToAudio: failed to start HTTP request."));

		return;

	}



	UE_LOG(LogGodfreySpeechStreamNode, Log,

		TEXT("HTTP stream request dispatched with incremental body receive (GetContent will stay empty per engine contract). RequestStartPlatform=%.6f"),

		HttpRequestStartSeconds);

}



void UAsyncActionStreamGodfreySpeech::HandleRequestProgress64(uint64 BytesReceived)

{

	if (!bSawFirstHttpProgress)

	{

		bSawFirstHttpProgress = true;

		FirstHttpProgressPlatformSeconds = FPlatformTime::Seconds();

		UE_LOG(LogGodfreySpeechStreamNode, Log,

			TEXT("HTTP OnRequestProgress64: first progress tick. BytesReceived=%llu PlatformTime=%.6f (progress can lead body; body uses stream delegate)"),

			BytesReceived,

			FirstHttpProgressPlatformSeconds);

	}

}



void UAsyncActionStreamGodfreySpeech::ScheduleHttpBodyDrain()

{

	bool Expected = false;

	if (!bHttpBodyDrainPending.compare_exchange_strong(Expected, true))

	{

		return;

	}



	const TWeakObjectPtr<UAsyncActionStreamGodfreySpeech> WeakThis(this);

	AsyncTask(ENamedThreads::GameThread, [WeakThis]()

	{

		if (UAsyncActionStreamGodfreySpeech* Strong = WeakThis.Get())

		{

			Strong->ProcessHttpBodyFifo_GameThread();

		}

	});

}



bool UAsyncActionStreamGodfreySpeech::HasAlignedPcmPending() const

{

	const int32 FrameSize = NumChannels * static_cast<int32>(sizeof(int16));

	if (FrameSize <= 0)

	{

		return false;

	}

	return PendingBytes.Num() >= FrameSize;

}



void UAsyncActionStreamGodfreySpeech::ProcessHttpBodyFifo_GameThread()

{

	if (bDidFinish)

	{

		bHttpBodyDrainPending = false;

		return;

	}



	{

		FScopeLock Lock(&HttpBodyLock);

		if (HttpBodyAccum.Num() > 0)

		{

			if (FirstGameThreadPcmPlatformSeconds < 0.0)

			{

				FirstGameThreadPcmPlatformSeconds = FPlatformTime::Seconds();

				UE_LOG(LogGodfreySpeechStreamNode, Log,

					TEXT("HTTP->GameThread: first body batch drained to PCM pipeline. BatchBytes=%d PlatformTime=%.6f HttpChunksSoFar=%d TotalHttpBytes=%lld"),

					HttpBodyAccum.Num(),

					FirstGameThreadPcmPlatformSeconds,

					HttpStreamChunkCount,

					TotalHttpBodyBytesReceived);

				if (StreamSession)

				{

					StreamSession->NotifyFirstHttpBodyBytesPlatformSeconds(FirstGameThreadPcmPlatformSeconds);

				}

				if (bPullQueuedMode)

				{

					UE_LOG(LogGodfreySpeechStreamNode, Log,

						TEXT("PullQueuedGodfreySpeechToAudio: PCM first bytes received. BatchBytes=%d"),

						HttpBodyAccum.Num());

				}

			}



			PendingBytes.Append(HttpBodyAccum);

			HttpBodyAccum.Reset();

		}

	}



	if (!bLoggedFormat && ActiveRequest.IsValid())

	{

		if (const FHttpResponsePtr Response = ActiveRequest->GetResponse())

		{

			bLoggedFormat = true;

			UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("Stream response format. HTTP=%d Content-Type=%s"),

				Response->GetResponseCode(),

				*Response->GetContentType());

		}

	}



	const int32 PushBudget = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAcePrePlayPushBudgetPerTick;

	int32 PushBudgetMutable = FMath::Max(1, PushBudget);
	if (StreamSession)
	{
		PushBudgetMutable = StreamSession->GetEffectiveIngestPushBudget(PushBudgetMutable, false);
	}

	ProcessPendingPcmBytes(false, &PushBudgetMutable);



	bHttpBodyDrainPending = false;



	if (bDidFinish)

	{

		return;

	}



	bool bReschedule = HasAlignedPcmPending();

	{

		FScopeLock Lock(&HttpBodyLock);

		if (HttpBodyAccum.Num() > 0)

		{

			bReschedule = true;

		}

	}



	if (bHttpCompleteAwaitingFinish)

	{

		TryFinishStreamAfterHttpComplete();

	}



	if (!bDidFinish && (bReschedule || bHttpCompleteAwaitingFinish))

	{

		ScheduleHttpBodyDrain();

	}

}



void UAsyncActionStreamGodfreySpeech::TryFinishStreamAfterHttpComplete()

{

	if (!bHttpCompleteAwaitingFinish || bDidFinish)

	{

		return;

	}



	{

		FScopeLock Lock(&HttpBodyLock);

		if (HttpBodyAccum.Num() > 0)

		{

			return;

		}

	}



	if (HasAlignedPcmPending())

	{

		return;

	}



	bHttpCompleteAwaitingFinish = false;



	const double EndPlatformSeconds = FPlatformTime::Seconds();

	UE_LOG(LogGodfreySpeechStreamNode, Log,

		TEXT("HTTP complete: calling FinishStream (EndAudioSamples). PlatformTime=%.6f TotalHttpBodyBytes=%lld HttpSerializeCallbacks=%d AnimateFromAudioPushes=%d"),

		EndPlatformSeconds,

		TotalHttpBodyBytesReceived,

		HttpStreamChunkCount,

		AnimateFromAudioPushCount);



	FString FinishError;

	if (!StreamSession || !StreamSession->FinishStream(FinishError))

	{

		FailAndStop(FinishError.IsEmpty() ? TEXT("FinishStream failed.") : FinishError);

		return;

	}



	if (bPullQueuedMode)

	{

		bAwaitingPlaybackBeforeFinish = true;

		UE_LOG(LogGodfreySpeechStreamNode, Log,

			TEXT("PullQueuedGodfreySpeechToAudio: HTTP ingest finished (%lld bytes); waiting for ACE playback before completing (prevents queue overlap)."),

			TotalHttpBodyBytesReceived);

		return;

	}



	CompletePullQueuedActionAfterPlayback();

}



void UAsyncActionStreamGodfreySpeech::CompletePullQueuedActionAfterPlayback()

{

	if (bDidFinish)

	{

		return;

	}



	bDidFinish = true;

	bAwaitingPlaybackBeforeFinish = false;



	UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("Stream finished successfully. TotalHttpBodyBytes=%lld"), TotalHttpBodyBytesReceived);



	OnFinished.Broadcast();

	SetReadyToDestroy();

}



void UAsyncActionStreamGodfreySpeech::ProcessPendingPcmBytes(bool bFlushFinal, int32* OptMaxPushesRemaining)

{

	if (!StreamSession)

	{

		return;

	}



	const int32 FrameSize = NumChannels * static_cast<int32>(sizeof(int16));

	if (FrameSize <= 0)

	{

		FailAndStop(TEXT("Invalid frame size while processing stream PCM."));

		return;

	}

	if (OptMaxPushesRemaining)

	{

		*OptMaxPushesRemaining = StreamSession->GetEffectiveIngestPushBudget(*OptMaxPushesRemaining, bFlushFinal);

	}



	while (true)

	{

		if (OptMaxPushesRemaining && *OptMaxPushesRemaining <= 0)

		{

			break;

		}



		const int32 AlignedTotal = (PendingBytes.Num() / FrameSize) * FrameSize;

		if (AlignedTotal <= 0)

		{

			break;

		}

		// Cap each PushPcm16Chunk so ACE ingest stays near real-time; duration from AceMaxPcmPushChunkDurationMs (clamped 10–80 ms).

		const float ChunkMs = GetDefault<UUnrealPerformerGodfreySettings>()->AceMaxPcmPushChunkDurationMs;

		const float ClampedMs = FMath::Clamp(ChunkMs, 10.f, 80.f);

		const int32 FramesPerChunk = FMath::Max(1, FMath::RoundToInt(static_cast<float>(SampleRate) * (ClampedMs / 1000.f)));

		const int32 MaxChunkBytes = FMath::Max(FrameSize, FramesPerChunk * FrameSize);

		const int32 ChunkBytes = FMath::Min(AlignedTotal, MaxChunkBytes);



		TArray<uint8> Chunk;

		Chunk.Append(PendingBytes.GetData(), ChunkBytes);

		PendingBytes.RemoveAt(0, ChunkBytes, EAllowShrinking::No);



		FString PushError;

		if (!StreamSession->PushPcm16Chunk(Chunk, PushError))

		{

			FailAndStop(FString::Printf(TEXT("PushPcm16Chunk failed: %s"), *PushError));

			return;

		}



		++AnimateFromAudioPushCount;

		UE_LOG(LogGodfreySpeechStreamNode, Verbose, TEXT("PCM pushed. PushIndex=%d ChunkBytes=%d RemainingPending=%d"),

			AnimateFromAudioPushCount, Chunk.Num(), PendingBytes.Num());

		if (OptMaxPushesRemaining)

		{

			--(*OptMaxPushesRemaining);

		}

	}



	if (bFlushFinal && PendingBytes.Num() > 0)

	{

		FailAndStop(FString::Printf(TEXT("Stream ended with unaligned trailing bytes: %d"), PendingBytes.Num()));

	}

}



void UAsyncActionStreamGodfreySpeech::HandleRequestCompleted(bool bConnectedSuccessfully, int32 ResponseCode, const FString& CompletionError)

{

	if (!CompletionError.IsEmpty())

	{

		FailAndStop(CompletionError);

		return;

	}



	if (!bConnectedSuccessfully || ResponseCode < 200 || ResponseCode >= 300)

	{

		if (bPullQueuedMode && ResponseCode == 409)

		{

			UE_LOG(LogGodfreySpeechStreamNode, Error,

				TEXT("PullQueuedGodfreySpeechToAudio: stream-pcm POST returned HTTP 409 Conflict (e.g. stale requestId or double consume)."));

		}

		FailAndStop(FString::Printf(TEXT("%sStream request failed. HTTP=%d"),

			bPullQueuedMode ? TEXT("PullQueuedGodfreySpeechToAudio: ") : TEXT(""),

			ResponseCode));

		return;

	}



	{

		FScopeLock Lock(&HttpBodyLock);

		if (HttpBodyAccum.Num() > 0)

		{

			const int32 MergedTrailingBytes = HttpBodyAccum.Num();

			PendingBytes.Append(HttpBodyAccum);

			HttpBodyAccum.Reset();

			UE_LOG(LogGodfreySpeechStreamNode, Log,

				TEXT("HTTP complete: merged %d trailing bytes from HttpBodyAccum into PCM pipeline."),

				MergedTrailingBytes);

		}

	}



	bHttpCompleteAwaitingFinish = true;

	ScheduleHttpBodyDrain();

	TryFinishStreamAfterHttpComplete();

}



void UAsyncActionStreamGodfreySpeech::FailAndStop(const FString& ErrorMessage)

{

	if (bDidFinish)

	{

		return;

	}

	bDidFinish = true;



	UE_LOG(LogGodfreySpeechStreamNode, Error, TEXT("%s"), *ErrorMessage);



	if (ActiveRequest.IsValid())

	{

		ActiveRequest->CancelRequest();

		ActiveRequest.Reset();

	}



	{

		FScopeLock Lock(&HttpBodyLock);

		HttpBodyAccum.Reset();

	}

	bHttpBodyDrainPending = false;



	if (StreamSession)

	{

		StreamSession->StopStream();

	}



	TryForwardUtteranceEndedToPerformerIfNeeded();



	OnError.Broadcast(ErrorMessage);

	SetReadyToDestroy();

}



void UAsyncActionStreamGodfreySpeech::HandleSessionPlaybackStarted()

{

	if (bPullQueuedMode)

	{

		UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("PullQueuedGodfreySpeechToAudio: playback started."));

	}

	TryForwardUtteranceStartedToPerformerIfNeeded();

	OnPlaybackStarted.Broadcast();

}



void UAsyncActionStreamGodfreySpeech::HandleSessionLipSyncStarted()

{

	TryForwardUtteranceStartedToPerformerIfNeeded();

	OnLipSyncStarted.Broadcast();

}



void UAsyncActionStreamGodfreySpeech::HandleSessionPlaybackEnded()

{

	if (bPullQueuedMode)

	{

		UE_LOG(LogGodfreySpeechStreamNode, Log, TEXT("PullQueuedGodfreySpeechToAudio: ACE playback ended."));

	}

	TryForwardUtteranceEndedToPerformerIfNeeded();

	if (bPullQueuedMode && bAwaitingPlaybackBeforeFinish)
	{
		CompletePullQueuedActionAfterPlayback();
	}

}



void UAsyncActionStreamGodfreySpeech::HandleSessionError(const FString& ErrorMessage)

{

	TryForwardUtteranceEndedToPerformerIfNeeded();

	OnError.Broadcast(ErrorMessage);

}



void UAsyncActionStreamGodfreySpeech::TryForwardUtteranceStartedToPerformerIfNeeded()

{

	if (bDidForwardUtteranceStartedToPerformer)

	{

		return;

	}

	AActor* const Ch = CharacterForAce.Get();

	if (!IsValid(Ch))

	{

		return;

	}

	if (UGodfreyPerformanceStateComponent* const Perf = Ch->FindComponentByClass<UGodfreyPerformanceStateComponent>())

	{

		bDidForwardUtteranceStartedToPerformer = true;

		Perf->NotifyUtteranceStarted();

	}

}



void UAsyncActionStreamGodfreySpeech::TryForwardUtteranceEndedToPerformerIfNeeded()

{

	if (bDidForwardUtteranceEndedToPerformer)

	{

		return;

	}

	bDidForwardUtteranceEndedToPerformer = true;

	AActor* const Ch = CharacterForAce.Get();

	if (!IsValid(Ch))

	{

		return;

	}

	if (UGodfreyPerformanceStateComponent* const Perf = Ch->FindComponentByClass<UGodfreyPerformanceStateComponent>())

	{

		Perf->NotifyUtteranceEnded();

	}

}



void UAsyncActionStreamGodfreySpeech::TryForwardPerformanceCueToPerformer(const FString& CueType, const FString& CueValue, const FString& RawCue)

{

	AActor* const Ch = CharacterForAce.Get();

	if (!IsValid(Ch))

	{

		return;

	}

	if (UGodfreyPerformanceStateComponent* const Perf = Ch->FindComponentByClass<UGodfreyPerformanceStateComponent>())

	{

		Perf->NotifyPerformanceCue(CueType, CueValue, RawCue);

	}

}


