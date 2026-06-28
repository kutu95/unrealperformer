#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyPerformanceTypes.h"
#if WITH_EDITOR
#include "Containers/Ticker.h"
#endif
#include "GodfreyPerformerAnimationBridgeComponent.generated.h"

class UAnimMontage;
class UGodfreyPerformanceStateComponent;
class USkeletalMeshComponent;

/**
 * Godfrey Performer v2/v3 — animation bridge for subtle body presence (MetaHuman Body mesh, additive montages).
 *
 * Subscribes to UGodfreyPerformanceStateComponent on the same actor. v3 adds: montage deduplication, emphasis cooldown,
 * optional looping idle-breath montage, read-only idle oscillators for AnimBP wiring, and optional soft actor yaw toward
 * an attention target (no IK, no Control Rig, no face graph changes). ACE / A2F / streaming stay on existing paths.
 *
 * Montage setup (assets): use additive upper-body slot tracks and layered blend per bone on the Body AnimBP; assign only
 * TargetSkeletalMesh to the body mesh — do not drive Face mesh montages from here.
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyPerformerAnimationBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyPerformerAnimationBridgeComponent();

	virtual void InitializeComponent() override;
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	bool EditorShirtDiagnosticTickerPoll(float DeltaTime);
	void EnsureEditorShirtDiagnosticTicker();
	void RemoveEditorShirtDiagnosticTicker();
	FTSTicker::FDelegateHandle EditorShirtDiagnosticTickerHandle;
#endif
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Montage playback (safe no-ops when mesh / AnimInstance / montage missing) ---

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayListeningBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayThinkingBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlaySpeakingStartBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlaySpeakingIdleBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayEmphasisBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayAmusedBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlaySeriousBehaviour();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void PlayReturnToIdleBehaviour();

	/** Stops SpeakingIdleMontage on the target mesh if it is currently active (blend 0.25s). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void StopSpeakingBehaviour();

	/** Call after toggling bEnableIdleMicroMotion / bEnableAttentionTargetFollow at runtime so tick starts/stops. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	void RefreshBehaviourTick();

	/** If TargetSkeletalMesh is unset, find owner mesh whose name contains BodyMeshNameHint and not FaceMeshNameExclude. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge")
	bool ResolveTargetBodyMesh();

	/** Sets CurrentAttentionTarget and logs; intended for visitor / prop tracking (soft yaw only). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|Attention")
	void SetCurrentAttentionTarget(AActor* NewTarget);

	/** Logs visibility + leader-pose state for every owner skeletal mesh (Body vs Torso/Legs/Feet). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	void LogSkeletalMeshPropagationReport() const;

	/** Wire Torso/Legs/Feet (and other followers) to copy bone pose from the resolved Body mesh. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	int32 WireClothingMeshesToBodyLeaderPose();

	/** Debug: hide clothing meshes and/or force the Body mesh visible to isolate whether Body is animating. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug")
	void ApplyBodyMotionDebugVisibility(bool bHideClothingMeshes, bool bForceBodyMeshVisible);

	// --- Animation targets (assign Body / compatible mesh with AnimBP) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge")
	TObjectPtr<USkeletalMeshComponent> TargetSkeletalMesh;

	/** When TargetSkeletalMesh is unset at BeginPlay, auto-pick a body mesh (MetaHuman "Body", not Face). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge")
	bool bAutoResolveMetaHumanBodyMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge")
	FString BodyMeshNameHint = TEXT("Body");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge")
	FString FaceMeshNameExclude = TEXT("Face");

	/** MetaHuman clothing meshes that should copy animated bone pose from Body (leader pose). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	bool bAutoWireClothingLeaderPoseToBody = false;

	/**
	 * When false (default), MetaHumanComponentUE owns Torso/Legs/Feet — bridge only drives Body montages.
	 * Enable only for non-MetaHuman test bodies or after deliberate debugging.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	bool bManageMetaHumanGarmentsAtRuntime = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation")
	TArray<FName> ClothingFollowerMeshNames = { FName(TEXT("Torso")), FName(TEXT("Legs")), FName(TEXT("Feet")) };

	/** Applied at BeginPlay when true — hides Torso/Legs/Feet so only Body is visible for motion diagnosis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug")
	bool bDebugHideClothingMeshesAtBeginPlay = false;

	/** Applied at BeginPlay when true — forces Body mesh visible even if BP_Gavin hid it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug")
	bool bDebugForceBodyMeshVisibleAtBeginPlay = false;

	/** Log Torso/Body/CopyPose state when zooming, tick/visibility changes, or bone mismatch (Output Log: ShirtDiag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug")
	bool bLogMetaHumanShirtDiagnostics = true;

	/** Minimum seconds between periodic shirt diagnostic snapshots (state-change snapshots always log). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug",
		meta = (ClampMin = "0.05", ClampMax = "5"))
	float ShirtDiagnosticMinLogInterval = 0.25f;

	/** World-space pelvis delta above this (cm) triggers EXPLOSION_SUSPECT warning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|MeshPropagation|Debug",
		meta = (ClampMin = "10", ClampMax = "500"))
	float ShirtExplosionPelvisDeltaThresholdCm = 75.f;

	/**
	 * When auto-assigning placeholder montages, prefer large/obvious clips (wave/gesture) over subtle foot/calf loops.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	bool bPreferObviousPlaceholderAnimations = true;

	/**
	 * When montage slots are empty, scan PlaceholderMontageSearchPath for a compatible AnimSequence / AnimMontage
	 * on the body skeleton and build temporary dynamic montages (exhibition placeholder pass).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	bool bAutoAssignPlaceholderMontages = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	FName PlaceholderMontageSearchPath = FName(TEXT("/Game/MetaHumans"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	FName PlaceholderMontageSlotName = FName(TEXT("DefaultSlot"));

	/** When true, rebuild montages missing a DefaultSlot track at runtime (CreateSlotAnimationAsDynamicMontage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	bool bAutoRemapMontagesToBodySlot = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> PlaceholderMontageOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> ListeningEnterMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> ThinkingMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> SpeakingStartMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> SpeakingIdleMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> EmphasisMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> AmusedMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> SeriousMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages")
	TObjectPtr<UAnimMontage> ReturnToIdleMontage;

	/** Optional subtle loop (additive) played when returning to idle attention; stopped when listening/thinking/speaking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|Montages|v3 Idle")
	TObjectPtr<UAnimMontage> IdleBreathingMontage;

	// --- v3 tuning (AnimBP + montage pacing) ---

	/** Scales play rate for speaking start / idle montages when restarted (1 = default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3", meta = (ClampMin = "0.25", ClampMax = "2.5"))
	float SpeakingMotionIntensity = 0.55f;

	/** Loop SpeakingIdleMontage for the whole utterance (section loops back on itself after Montage_Play). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bLoopSpeakingIdleMontage = true;

	/** When true (default), skip SpeakingStart if unset or same as idle — avoids one-shot full gesture at utterance start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bPreferSpeakingIdleLoopOnly = true;

	/** Multiplier for idle micro-motion oscillators (AnimBP curves). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3", meta = (ClampMin = "0", ClampMax = "3"))
	float IdleBreathingIntensity = 1.f;

	/** Max yaw delta (degrees) applied toward CurrentAttentionTarget from the cached exhibit yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0", ClampMax = "45"))
	float AttentionOffsetStrength = 16.f;

	/** Minimum seconds between emphasis montage plays (delegate still fires; see bFireBridgeEmphasisOnCooldownSkip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3", meta = (ClampMin = "0", ClampMax = "10"))
	float GestureCooldownSeconds = 0.85f;

	/** If true, OnBridgeEmphasis still broadcasts when emphasis montage is skipped by cooldown. If false, cooldown is fully silent to Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bFireBridgeEmphasisOnCooldownSkip = false;

	/** When true, short listening/thinking/return montages are not restarted if already playing on the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bDeduplicateActiveMontagePlays = true;

	/** Updates IdleBreathingWave / IdlePostureSwayWave each tick for AnimBP (no mesh writes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bEnableIdleMicroMotion = true;

	/** Soft actor yaw toward CurrentAttentionTarget; best for stationary exhibit roots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3")
	bool bEnableAttentionTargetFollow = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention")
	TObjectPtr<AActor> CurrentAttentionTarget;

	/** Interpolation speed for attention yaw (higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Bridge|v3|Attention", meta = (ClampMin = "0.1", ClampMax = "20"))
	float AttentionInterpSpeed = 3.f;

	// --- Mirrored state (updated from performer events; for AnimBP / UI reads) ---

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsListening = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsThinking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsSpeaking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsSerious = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	bool bIsAmused = false;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	EGodfreyPerformanceState CurrentPerformanceState = EGodfreyPerformanceState::Idle;

	/** Simple scalar; emphasis bumps it when not on cooldown. */
	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State")
	float GestureIntensity = 1.f;

	/** ~sin wave for subtle breathing drive in AnimBP (updated when bEnableIdleMicroMotion). */
	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State|v3 Idle")
	float IdleBreathingWave = 0.f;

	/** Secondary wave for posture sway / weight shift blend in AnimBP. */
	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Performer|Bridge|State|v3 Idle")
	float IdlePostureSwayWave = 0.f;

	// --- Bridge delegates (secondary event bus for animation Blueprints) ---

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeListening;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeThinking;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeSpeakingStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeSpeakingEnded;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeReturnedToIdle;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeEmphasis;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeAmused;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerSimpleEvent OnBridgeSerious;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Bridge")
	FGodfreyPerformerCueEvent OnBridgeCueReceived;

private:
	void TryBindPerformerState();
	void UnbindPerformerState();
	void RefreshMirroredPerformanceState();
	void UpdatePerformerTickEnabled();

	bool PlayMontageIfPossible(UAnimMontage* Montage, const TCHAR* ContextLabel, float PlayRate = 1.f,
		bool bRestartIfAlreadyPlaying = true, bool bLoopMontage = false);

	UAnimInstance* ResolveAnimInstance(const TCHAR* ContextLabel) const;

	void StopIdleBreathingMontageIfActive();
	void TryStartIdleBreathingMontage();
	void PlaySpeakingIdleInternal(bool bRestartIfAlreadyPlaying);

	void LogMontageSetupStatus() const;
	bool ShouldAutoResolveBodyMesh() const;
	void InitAssignedBodyAnimClassOnly();
	void EnsureMontageAnimInstanceReady();
	void StabilizeClothingLeaderPoseMeshes();
	void TryStabilizeClothingForEditorViewport();
	/** False while sibling Body/Torso/Legs/Feet are still registering — avoid TickComponent before bRegistered. */
	bool AreMetaHumanGarmentMeshesRegistered() const;
	void DeferredClothingStabilize();
	void RefreshClothingPoseAfterStabilize();
	void MaintainClothingLeaderPose();
	bool IsBodyMontagePlaying() const;
	/** True when owner has UMetaHumanComponentUE — stock MetaHuman owns clothing tick / leader pose. */
	bool UsesMetaHumanNativeClothingPipeline() const;
	/** Clothing leader-pose hacks only for non-MetaHuman performers (e.g. mismatched Godfrey test body). */
	bool ShouldManageClothingLeaderPose() const;
	bool IsEditorViewportWorld() const;
	/** True when bridge may alter MetaHuman garment meshes (tick, visibility, leader pose). */
	bool ShouldManageMetaHumanGarmentsAtRuntime() const;
	/** False when bAutoActivate is off — no editor LOD pin, garment refresh, or mesh overrides. */
	bool IsBridgeActiveForMetaHumanIntervention() const;
	/** Torso/shirt (or any follower) uses mesh post-process CopyPoseFromMesh — needs ticking Body source. */
	bool HasMetaHumanGarmentPostProcessMesh() const;
	/** Hidden Body must always tick so garment post-process CopyPoseFromMesh has fresh bones when Torso zooms in. */
	void EnsureMetaHumanBodyTicksForClothingPostProcess();
	void EnsureMetaHumanCopyPoseBodySource();
	void MaintainMetaHumanBodyTickForClothing();
	void MaintainMetaHumanCopyPoseBodySource();
	void ApplyMetaHumanClothingTickPrerequisites(USkeletalMeshComponent* Body);
	void WireClothingPostProcessCopyPoseSource(USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment,
		bool bLogWiringResult = true);
	/** Mirror MetaHumanComponentUE::PostConnectAnimBPVariables for garment post-process (editor has no BeginPlay). */
	void ApplyMetaHumanGarmentPostProcessVariables(USkeletalMeshComponent* Garment, UAnimInstance* PostProcessInstance);
	/** MetaHuman shirt PP uses CopyPose bUseAttachedParent when Source Mesh Component is unset — Torso must attach to Body. */
	void EnsureMetaHumanCopyPoseGarmentAttachedToBody(USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment);
	void RefreshMetaHumanGarmentPoses(USkeletalMeshComponent* Body);
	void EnsureMetaHumanFaceVisible();
	/** After MetaHumanComponentUE::BeginPlay — refresh garment poses without re-InitAnim (preserves CopyPoseFromMesh pins). */
	void DeferredMetaHumanClothingRefresh();
	void ScheduleMetaHumanClothingRefreshPasses();
	void ScheduleEditorCopyPoseStabilize();
	void ClearPostProcessGarmentLeaderPose(USkeletalMeshComponent* Garment);
	void MaybeLogMetaHumanShirtDiagnostics(const TCHAR* TriggerReason, bool bForce = false);
	void LogMetaHumanShirtDiagnosticSnapshot(const TCHAR* TriggerReason) const;
#if WITH_EDITOR
	/** Pin LODSync + garment meshes to LOD 0 in editor viewport when garment bridge is off (prevents zoom shirt explosion). */
	void PinEditorViewportMetaHumanLOD();
	void StabilizeEditorTorsoOnViewportChange();
	/** Undo editor-only LOD / leader-pose experiments and restore MetaHuman CopyPose garment path. */
	void RestoreEditorMetaHumanViewportDefaults();
	void RestoreEditorTorsoCopyPoseGarment(USkeletalMeshComponent* Body, USkeletalMeshComponent* Torso);
	void ApplyEditorTorsoCopyPoseOnlyOverrides(UAnimInstance* PostProcessInstance) const;
	void ReapplyEditorGarmentPreviewSettings();
#endif
	bool HasClothingFollowerMeshesOnBody() const;
	bool TryAssignPlaceholderMontages();
	UAnimMontage* MakeOrGetPlaceholderMontage(UAnimSequence* Sequence, const TCHAR* Label, int32 LoopCount = 1);
	UAnimMontage* ResolveMontageForBodySlot(UAnimMontage* Montage, const TCHAR* ContextLabel);
	UAnimMontage* ResolveLoopedBodySlotMontage(UAnimMontage* Montage, const TCHAR* ContextLabel);
	void MaintainSpeakingIdleMontage();
	void OnSpeakingIdleMontageEnded(UAnimMontage* EndedMontage, bool bInterrupted);
	void BindSpeakingIdleMontageEndDelegate(UAnimInstance* AnimInst, UAnimMontage* PlayMontage);
	UAnimMontage* ResolvePlaceholderMontageAsset();

	USkeletalMeshComponent* FindFollowerMeshByComponentName(FName MeshName) const;
	int32 ScoreAnimAssetNameForObviousTest(const FString& AssetName) const;

	void UpdateAttentionRotation(float DeltaTime);

	UFUNCTION()
	void HandleListeningStarted();

	UFUNCTION()
	void HandleThinkingStarted();

	UFUNCTION()
	void HandleSpeakingStarted();

	UFUNCTION()
	void HandleSpeakingEnded();

	UFUNCTION()
	void HandleReturnedToIdle();

	UFUNCTION()
	void HandleEmphasisTriggered();

	UFUNCTION()
	void HandleAmusedTriggered();

	UFUNCTION()
	void HandleSeriousTriggered();

	UFUNCTION()
	void HandlePerformanceCueReceived(const FString& CueType, const FString& CueValue, const FString& RawCue);

	UPROPERTY(Transient)
	TObjectPtr<UGodfreyPerformanceStateComponent> PerformerState;

	float IdleMicroTimeSeconds = 0.f;
	double LastEmphasisMontageWorldTimeSeconds = -1.e10;
	bool bHasCachedExhibitYaw = false;
	float CachedExhibitYawDegrees = 0.f;

	/** Montage timeline length (GetPlayLength) for Montage_GetPosition rewind. */
	float SpeakingIdleMontageCycleSeconds = 0.f;
	/** Wall-clock cycle length from Montage_Play return (GetPlayLength / playRate). */
	float SpeakingIdleMontageWallCycleSeconds = 0.f;
	double SpeakingIdleCycleStartWorldTime = -1.0;
	TObjectPtr<UAnimMontage> ActiveSpeakingIdlePlayMontage;
	FOnMontageEnded SpeakingIdleMontageEndedDelegate;
	double LastSpeakingIdleCycleRewindLogTime = -1.e10;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimMontage>> GeneratedPlaceholderMontages;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UAnimMontage>, TObjectPtr<UAnimMontage>> BodySlotRemappedMontages;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UAnimMontage>, TObjectPtr<UAnimMontage>> LoopedBodySlotMontages;

	bool bMetaHumanClothingTickPrerequisitesApplied = false;
	bool bMetaHumanClothingRefreshPassesScheduled = false;
	bool bEditorCopyPoseStabilizeScheduled = false;
	bool bLoggedEditorViewportLODPin = false;
	uint8 MetaHumanClothingRefreshPassCount = 0;
	uint8 MetaHumanGarmentRegisterWaitFrames = 0;
	static constexpr uint8 MetaHumanMaxGarmentRegisterWaitFrames = 30;
	static constexpr uint8 MetaHumanMaxClothingRefreshPasses = 5;

	mutable int32 LastLoggedBodyTickOpt = -1;
	mutable int32 LastLoggedTorsoTickOpt = -1;
	mutable int32 LastLoggedBodyVisible = -1;
	mutable int32 LastLoggedTorsoVisible = -1;
	mutable int32 LastLoggedBodyHiddenInGame = -1;
	mutable int32 LastLoggedBodyLOD = -1;
	mutable int32 LastLoggedTorsoLOD = -1;
	mutable double LastShirtDiagnosticLogTimeSeconds = -1.e10;
	mutable float LastEditorViewDistanceToTorso = -1.f;
	mutable float LastEditorViewFOV = -1.f;
	mutable float LastLoggedPelvisDeltaCm = -1.f;
	mutable bool bLoggedCopyPoseWireForTorso = false;
};
