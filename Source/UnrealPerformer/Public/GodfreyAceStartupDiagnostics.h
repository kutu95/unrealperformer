#pragma once

#include "CoreMinimal.h"

/**
 * Per-utterance startup timing collected on the game thread in UGodfreyPcmStreamSession (PCM → ACE ingest side).
 * ACE internal state (IDLE / STARTING / STARTED / IN_PROGRESS) and AudioComponent::Play() are logged separately
 * when ace.GodfreyStartupTiming is enabled — see ACEAudioCurveSourceComponent.cpp comments.
 *
 * Why AudioComponent::Play() is not the same as "animation started":
 * - Play() kicks the audio mixer to consume the procedural queue; lip-sync curves are driven from a playback clock.
 * - ACE sets AnimState STARTING while buffering, then STARTED only after HandlePlaybackFraction reports a playback
 *   time that looks consistent with received audio (guards engine garbage before the source initializes).
 * - OnAnimationStarted fires on the next TickComponent after STARTED, promoting to IN_PROGRESS — so there is
 *   usually at least one frame between Play() and the delegate.
 *
 * Why buffering (BufferLengthInSeconds + MinBlendShapeSamplesBeforePlay) improves stability:
 * - TryStartProceduralPlayback waits until enough audio is queued and (by default) blend-shape frames exist
 *   before Play(), reducing the chance the mixer runs ahead of A2F curve delivery.
 *
 * Why lip sync can lag audible speech at utterance start (even when audio is already playing):
 * - GetCurveOutputs used to require AnimState STARTED/IN_PROGRESS; audio can play in STARTING while the face
 *   receives no weights until HandlePlaybackFraction validates the mixer clock (often hundreds of ms).
 * - A2F may deliver audio before enough blend-shape frames exist; playback can run ahead of curve timestamps.
 * - Enable ace.GodfreyStartupTiming=1 and compare [ACE sync] First AudioComponent Play vs First curve weights applied.
 */
struct FGodfreyAceUtteranceStartupMetrics
{
	int32 UtteranceOrdinal = 0;
	/** Wall clock when UGodfreyPcmStreamSession::StartStream succeeded (ACE session for this utterance begins). */
	double UtteranceT0PlatformSeconds = 0.0;
	double FirstPcmChunkPlatformSeconds = -1.0;
	double FirstAnimateFromAudioCallPlatformSeconds = -1.0;
	/** When UACEAudioCurveSourceComponent::OnAnimationStarted fired (STARTED → IN_PROGRESS on ACE tick). */
	double AceOnAnimationStartedPlatformSeconds = -1.0;
	bool bAceOnAnimationStartedObserved = false;
};
