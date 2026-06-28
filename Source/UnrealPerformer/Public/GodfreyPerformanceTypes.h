#pragma once

#include "CoreMinimal.h"
#include "GodfreyPerformanceTypes.generated.h"

/** High-level behavioural / conversational state for Captain Godfrey performance orchestration (body, gaze, montages — not ACE curves). */
UENUM(BlueprintType)
enum class EGodfreyPerformanceState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	Listening UMETA(DisplayName = "Listening"),
	Thinking UMETA(DisplayName = "Thinking"),
	Speaking UMETA(DisplayName = "Speaking"),
	Emphasising UMETA(DisplayName = "Emphasising"),
	Serious UMETA(DisplayName = "Serious"),
	Amused UMETA(DisplayName = "Amused"),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FGodfreyPerformanceStateChangedEvent,
	EGodfreyPerformanceState,
	NewState,
	EGodfreyPerformanceState,
	PreviousState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreyUtteranceLifecycleEvent);

/** Zero-parameter moment hooks for montages / AnimBP (listening, thinking, speaking edges, mood pulses). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreyPerformerSimpleEvent);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FGodfreyPerformerCueEvent,
	const FString&,
	CueType,
	const FString&,
	CueValue,
	const FString&,
	RawCue);
