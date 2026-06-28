#include "GodfreyPerformerAnimationBridgeComponent.h"

#include "GodfreyBodyAnimInstance.h"
#include "GodfreyPerformanceLog.h"
#include "GodfreyPerformanceStateComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "LODSyncInterface.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"
#include "ControlRig.h"
#include "MetaHumanComponentBase.h"
#include "MetaHumanComponentUE.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Components/LODSyncComponent.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#endif

namespace
{
static constexpr float SpeakingIdleMontageBlendOut = 0.25f;
/** One anim-sequence cycle per dynamic speaking montage; section loop + tick rewind sustain speech. */
static constexpr int32 GodfreySpeakingIdleSegmentLoopCount = 1;
static constexpr float GestureIntensityDefault = 1.f;

const TCHAR* AnimTickOptionToString(const EVisibilityBasedAnimTickOption Option)
{
	switch (Option)
	{
	case EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones:
		return TEXT("AlwaysTick");
	case EVisibilityBasedAnimTickOption::AlwaysTickPose:
		return TEXT("AlwaysTickPose");
	case EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered:
		return TEXT("OnlyTickWhenRendered");
	case EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered:
		return TEXT("OnlyTickMontages");
	default:
		return TEXT("Unknown");
	}
}

#if WITH_EDITOR
struct FEditorViewportWatch
{
	float ViewDistance = -1.f;
	float ViewFOV = -1.f;
};

FEditorViewportWatch GetEditorViewportWatch(const USceneComponent* Component)
{
	FEditorViewportWatch Watch;
	if (!Component || !GEditor)
	{
		return Watch;
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient || !ViewportClient->IsPerspective())
	{
		for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
		{
			if (Client && Client->IsVisible() && Client->IsPerspective())
			{
				ViewportClient = Client;
				break;
			}
		}
	}

	if (ViewportClient)
	{
		Watch.ViewDistance = FVector::Dist(ViewportClient->GetViewLocation(), Component->GetComponentLocation());
		Watch.ViewFOV = ViewportClient->ViewFOV;
	}

	return Watch;
}
#endif

bool GetBoneWorldLocationSafe(const USkeletalMeshComponent* Mesh, const FName BoneName, FVector& OutLocation)
{
	OutLocation = FVector::ZeroVector;
	if (!IsValid(Mesh) || !Mesh->GetSkinnedAsset())
	{
		return false;
	}

	const int32 BoneIndex = Mesh->GetBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	OutLocation = Mesh->GetBoneLocation(BoneName, EBoneSpaces::WorldSpace);
	return FMath::IsFinite(OutLocation.X) && FMath::IsFinite(OutLocation.Y) && FMath::IsFinite(OutLocation.Z);
}

bool ReadPostProcessBoolProperty(UAnimInstance* PostProcessInstance, const TCHAR* PropertyName, bool& OutValue)
{
	OutValue = false;
	if (!PostProcessInstance)
	{
		return false;
	}

	return MetaHumanComponentHelpers::GetPropertyValue(PostProcessInstance, FStringView(PropertyName), OutValue);
}

float ComputeMaxWatchBoneDeltaCm(const USkeletalMeshComponent* Body, const USkeletalMeshComponent* Torso)
{
	float MaxDeltaCm = 0.f;
	if (!IsValid(Body) || !IsValid(Torso))
	{
		return MaxDeltaCm;
	}

	static const FName WatchBones[] = {
		FName(TEXT("pelvis")),
		FName(TEXT("spine_05")),
		FName(TEXT("clavicle_l")),
		FName(TEXT("clavicle_r")),
	};
	for (const FName BoneName : WatchBones)
	{
		FVector BodyPos = FVector::ZeroVector;
		FVector TorsoPos = FVector::ZeroVector;
		if (GetBoneWorldLocationSafe(Body, BoneName, BodyPos) && GetBoneWorldLocationSafe(Torso, BoneName, TorsoPos))
		{
			MaxDeltaCm = FMath::Max(MaxDeltaCm, FVector::Dist(BodyPos, TorsoPos));
		}
	}

	return MaxDeltaCm;
}

int32 ReadTorsoPostProcessBoolAsInt(const USkeletalMeshComponent* Torso, const TCHAR* PropertyName)
{
	bool bValue = false;
	if (IsValid(Torso) && Torso->GetPostProcessInstance())
	{
		ReadPostProcessBoolProperty(Torso->GetPostProcessInstance(), PropertyName, bValue);
	}
	return bValue ? 1 : 0;
}

UClass* LoadClothingPostProcessAnimClass()
{
	UClass* ClothingPostProcessClass = StaticLoadClass(
		UAnimInstance::StaticClass(),
		nullptr,
		TEXT("/Game/MetaHumans/Common/Shared/Animation/ABP_Clothing_PostProcess.ABP_Clothing_PostProcess_C"));
	if (!ClothingPostProcessClass)
	{
		ClothingPostProcessClass = StaticLoadClass(
			UAnimInstance::StaticClass(),
			nullptr,
			TEXT("/Game/MetaHumans/Common/Animation/ABP_Clothing_PostProcess.ABP_Clothing_PostProcess_C"));
	}
	return ClothingPostProcessClass;
}

void WireAnimInstanceObjectProperty(UAnimInstance* AnimInstance, const FName PropertyName, UObject* Value)
{
	if (!AnimInstance || !Value)
	{
		return;
	}

	if (FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(AnimInstance->GetClass(), PropertyName))
	{
		ObjectProperty->SetObjectPropertyValue_InContainer(AnimInstance, Value);
	}
}

bool WireAnimInstanceSkeletalMeshComponentPropertyRecursive(UStruct* Struct, void* Container,
	USkeletalMeshComponent* Value, const int32 Depth, FName* OutPropertyName)
{
	if (!Struct || !Container || !Value || Depth > 12)
	{
		return false;
	}

	for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(*It))
		{
			if (!ObjectProperty->PropertyClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
			{
				continue;
			}

			ObjectProperty->SetObjectPropertyValue_InContainer(Container, Value);
			if (OutPropertyName)
			{
				*OutPropertyName = It->GetFName();
			}
			return true;
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(*It))
		{
			void* StructValue = StructProperty->ContainerPtrToValuePtr<void>(Container);
			if (WireAnimInstanceSkeletalMeshComponentPropertyRecursive(
					StructProperty->Struct, StructValue, Value, Depth + 1, OutPropertyName))
			{
				return true;
			}
		}
	}

	return false;
}

bool WireAnimInstanceSkeletalMeshComponentProperty(UAnimInstance* AnimInstance, USkeletalMeshComponent* Value,
	const TArrayView<const FName> PreferredNames, FName* OutPropertyName = nullptr)
{
	if (!AnimInstance || !Value)
	{
		return false;
	}

	auto TryWire = [AnimInstance, Value, OutPropertyName](const FName PropertyName) -> bool
	{
		if (FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(AnimInstance->GetClass(), PropertyName))
		{
			if (!ObjectProperty->PropertyClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
			{
				return false;
			}

			ObjectProperty->SetObjectPropertyValue_InContainer(AnimInstance, Value);
			if (OutPropertyName)
			{
				*OutPropertyName = PropertyName;
			}
			return true;
		}

		return false;
	};

	for (const FName PropertyName : PreferredNames)
	{
		if (TryWire(PropertyName))
		{
			return true;
		}
	}

	if (WireAnimInstanceSkeletalMeshComponentPropertyRecursive(
			AnimInstance->GetClass(), AnimInstance, Value, 0, OutPropertyName))
	{
		return true;
	}

	return false;
}

static bool GarmentMeshHasPostProcessAnim(const USkeletalMeshComponent* Follower)
{
	if (!IsValid(Follower))
	{
		return false;
	}

	if (Follower->GetPostProcessInstance())
	{
		return true;
	}

	const USkeletalMesh* MeshAsset = Follower->GetSkeletalMeshAsset();
	return IsValid(MeshAsset) && MeshAsset->GetPostProcessAnimBlueprint() != nullptr;
}

void ApplySpeakingMontageSectionLoop(UAnimInstance* AnimInst, UAnimMontage* PlayMontage)
{
	if (!AnimInst || !PlayMontage || PlayMontage->CompositeSections.Num() == 0)
	{
		return;
	}

	const FName LoopSection = PlayMontage->CompositeSections[0].SectionName;
	for (const FCompositeSection& Section : PlayMontage->CompositeSections)
	{
		AnimInst->Montage_SetNextSection(Section.SectionName, LoopSection, PlayMontage);
	}
}

static bool IsExcludedFaceMesh(const USkeletalMeshComponent* Mesh, const FString& FaceExclude)
{
	if (!IsValid(Mesh) || FaceExclude.IsEmpty())
	{
		return false;
	}
	return Mesh->GetName().Contains(FaceExclude, ESearchCase::IgnoreCase);
}

static bool HasRenderableSkeletalMeshAsset(const USkeletalMeshComponent* Mesh)
{
	return IsValid(Mesh) && Mesh->GetSkeletalMeshAsset() != nullptr;
}

bool IsDriveableSkeletalMesh(const USkeletalMeshComponent* Mesh)
{
	return IsValid(Mesh) && Mesh->IsRegistered() && HasRenderableSkeletalMeshAsset(Mesh);
}

void ForceSkeletalMeshPoseRefresh(USkeletalMeshComponent* Mesh)
{
	if (!IsDriveableSkeletalMesh(Mesh))
	{
		return;
	}

	Mesh->TickAnimation(0.f, false);
	Mesh->TickComponent(0.f, ELevelTick::LEVELTICK_All, nullptr);
	Mesh->RefreshBoneTransforms(nullptr);
}

void RefreshMetaHumanBodyPoseChain(USkeletalMeshComponent* Body)
{
	if (!IsDriveableSkeletalMesh(Body))
	{
		return;
	}

	Body->TickAnimation(0.f, false);
	Body->TickComponent(0.f, ELevelTick::LEVELTICK_All, nullptr);
	Body->RefreshBoneTransforms(nullptr);
	Body->RefreshFollowerComponents();
}

void RefreshMetaHumanGarmentPostProcessPose(USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment)
{
	if (!IsDriveableSkeletalMesh(Body) || !IsDriveableSkeletalMesh(Garment))
	{
		return;
	}

	RefreshMetaHumanBodyPoseChain(Body);

	Garment->SetDisablePostProcessBlueprint(false);
	Garment->TickAnimation(0.f, false);
	Garment->TickComponent(0.f, ELevelTick::LEVELTICK_All, nullptr);
	Garment->RefreshBoneTransforms(nullptr);
}

static int32 ScoreBodyMeshCandidate(const USkeletalMeshComponent* Mesh, const FString& BodyHint)
{
	if (!HasRenderableSkeletalMeshAsset(Mesh))
	{
		return -1;
	}

	const FString Name = Mesh->GetName();
	if (Name.Equals(BodyHint, ESearchCase::IgnoreCase))
	{
		return 100;
	}
	if (Name.Contains(BodyHint, ESearchCase::IgnoreCase))
	{
		return 50;
	}
	return 1;
}

static bool MontageHasPlayableSlotTrack(const UAnimMontage* Montage, const FName SlotName)
{
	if (!Montage)
	{
		return false;
	}

	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		if (Track.SlotName == SlotName && Track.AnimTrack.AnimSegments.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

static FString DescribeMontageSlotTracks(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return TEXT("(null montage)");
	}

	if (Montage->SlotAnimTracks.Num() == 0)
	{
		return TEXT("(no slot tracks)");
	}

	FString Result;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		if (!Result.IsEmpty())
		{
			Result += TEXT(", ");
		}
		Result += FString::Printf(TEXT("'%s'(%d)"), *Track.SlotName.ToString(), Track.AnimTrack.AnimSegments.Num());
	}
	return Result;
}

static UAnimSequence* ExtractPrimarySequenceFromMontage(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return nullptr;
	}

	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
		{
			if (const UAnimSequenceBase* AnimRef = Segment.GetAnimReference())
			{
				if (UAnimSequence* Sequence = Cast<UAnimSequence>(const_cast<UAnimSequenceBase*>(AnimRef)))
				{
					return Sequence;
				}
			}
		}
	}

	if (const UAnimSequenceBase* FirstRef = Montage->GetFirstAnimReference())
	{
		return Cast<UAnimSequence>(const_cast<UAnimSequenceBase*>(FirstRef));
	}

	return nullptr;
}

void ForEachCopyPoseFromMeshNode(UStruct* Struct, void* Container,
	const TFunctionRef<void(FAnimNode_CopyPoseFromMesh*)>& Func)
{
	if (!Struct || !Container)
	{
		return;
	}

	for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		if (FStructProperty* StructProp = CastField<FStructProperty>(*It))
		{
			void* StructValue = StructProp->ContainerPtrToValuePtr<void>(Container);
			if (StructProp->Struct == FAnimNode_CopyPoseFromMesh::StaticStruct())
			{
				Func(static_cast<FAnimNode_CopyPoseFromMesh*>(StructValue));
			}
			else
			{
				ForEachCopyPoseFromMeshNode(StructProp->Struct, StructValue, Func);
			}
		}
	}
}

int32 WireCopyPoseFromMeshAnimGraphNodes(UAnimInstance* AnimInstance, USkeletalMeshComponent* Body)
{
	if (!AnimInstance || !Body)
	{
		return 0;
	}

	int32 WiredCount = 0;
	ForEachCopyPoseFromMeshNode(AnimInstance->GetClass(), AnimInstance, [&](FAnimNode_CopyPoseFromMesh* Node)
	{
		if (!Node)
		{
			return;
		}

		Node->SourceMeshComponent = Body;
		Node->bUseAttachedParent = true;
		++WiredCount;
	});
	return WiredCount;
}

const FMetaHumanCustomizableBodyPart* GetMetaHumanBodyPartConfig(
	const UMetaHumanComponentUE* MetaHumanComponent, const FName GarmentLabel)
{
	if (!MetaHumanComponent)
	{
		return nullptr;
	}

	if (const FStructProperty* StructProp = FindFProperty<FStructProperty>(
		MetaHumanComponent->GetClass(), GarmentLabel))
	{
		if (StructProp->Struct == FMetaHumanCustomizableBodyPart::StaticStruct())
		{
			return StructProp->ContainerPtrToValuePtr<FMetaHumanCustomizableBodyPart>(MetaHumanComponent);
		}
	}

	return nullptr;
}

void MirrorMetaHumanPostConnectAnimBPVariables(
	const FMetaHumanCustomizableBodyPart& BodyPart, UAnimInstance* AnimInstance)
{
	if (!AnimInstance)
	{
		return;
	}

	MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
		AnimInstance, TEXT("Enable Control Rig"), BodyPart.ControlRigClass.Get() != nullptr);

	if (BodyPart.ControlRigClass)
	{
		MetaHumanComponentHelpers::ConnectVariable<FObjectProperty, TSubclassOf<UControlRig>>(
			AnimInstance, TEXT("Control Rig Class"), BodyPart.ControlRigClass);
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
			AnimInstance, TEXT("Control Rig LOD Threshold"), BodyPart.ControlRigLODThreshold);
	}

	MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
		AnimInstance, TEXT("Enable Rigid Body Simulation"), BodyPart.PhysicsAsset.Get() != nullptr);

	if (BodyPart.PhysicsAsset)
	{
		MetaHumanComponentHelpers::ConnectVariable<FObjectProperty, TObjectPtr<UPhysicsAsset>>(
			AnimInstance, TEXT("Override Physics Asset"), BodyPart.PhysicsAsset);
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
			AnimInstance, TEXT("Rigid Body LOD Threshold"), BodyPart.RigidBodyLODThreshold);
	}
}

void ApplyMetaHumanCopyPoseBodyVisibility(USkeletalMeshComponent* Body, const bool bEditorViewportWorld)
{
	if (!IsValid(Body))
	{
		return;
	}

#if WITH_EDITOR
	Body->SetHiddenInGame(bEditorViewportWorld ? false : true, true);
#else
	Body->SetHiddenInGame(true, true);
#endif
	Body->SetVisibility(true, true);
}
} // namespace

UGodfreyPerformerAnimationBridgeComponent::UGodfreyPerformerAnimationBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bWantsInitializeComponent = true;
}

void UGodfreyPerformerAnimationBridgeComponent::InitializeComponent()
{
	Super::InitializeComponent();
	UpdatePerformerTickEnabled();

#if WITH_EDITOR
	if (IsBridgeActiveForMetaHumanIntervention()
		&& ShouldManageMetaHumanGarmentsAtRuntime()
		&& IsEditorViewportWorld()
		&& UsesMetaHumanNativeClothingPipeline()
		&& HasMetaHumanGarmentPostProcessMesh())
	{
		if (UWorld* World = GetWorld())
		{
			ScheduleEditorCopyPoseStabilize();
		}
	}
	else if (IsBridgeActiveForMetaHumanIntervention()
		&& !ShouldManageMetaHumanGarmentsAtRuntime()
		&& IsEditorViewportWorld()
		&& UsesMetaHumanNativeClothingPipeline())
	{
		PinEditorViewportMetaHumanLOD();
	}
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::ScheduleEditorCopyPoseStabilize()
{
	if (bEditorCopyPoseStabilizeScheduled)
	{
		return;
	}

	bEditorCopyPoseStabilizeScheduled = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
			this, &UGodfreyPerformerAnimationBridgeComponent::TryStabilizeClothingForEditorViewport));
		ScheduleMetaHumanClothingRefreshPasses();
	}
}

void UGodfreyPerformerAnimationBridgeComponent::OnRegister()
{
	Super::OnRegister();
	UpdatePerformerTickEnabled();

#if WITH_EDITOR
	if (IsBridgeActiveForMetaHumanIntervention()
		&& ShouldManageMetaHumanGarmentsAtRuntime()
		&& IsEditorViewportWorld()
		&& UsesMetaHumanNativeClothingPipeline()
		&& HasMetaHumanGarmentPostProcessMesh())
	{
		ScheduleEditorCopyPoseStabilize();
	}
	else if (IsBridgeActiveForMetaHumanIntervention()
		&& !ShouldManageMetaHumanGarmentsAtRuntime()
		&& IsEditorViewportWorld()
		&& UsesMetaHumanNativeClothingPipeline())
	{
		PinEditorViewportMetaHumanLOD();
	}
#endif
}

bool UGodfreyPerformerAnimationBridgeComponent::IsBridgeActiveForMetaHumanIntervention() const
{
	return bAutoActivate;
}

bool UGodfreyPerformerAnimationBridgeComponent::IsEditorViewportWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->WorldType == EWorldType::Editor;
}

bool UGodfreyPerformerAnimationBridgeComponent::UsesMetaHumanNativeClothingPipeline() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	static UClass* MetaHumanComponentClass = nullptr;
	if (!MetaHumanComponentClass)
	{
		MetaHumanComponentClass = LoadClass<UActorComponent>(
			nullptr,
			TEXT("/Script/MetaHumanSDKRuntime.MetaHumanComponentUE"));
	}

	return MetaHumanComponentClass && Owner->FindComponentByClass(MetaHumanComponentClass) != nullptr;
}

bool UGodfreyPerformerAnimationBridgeComponent::ShouldManageClothingLeaderPose() const
{
	return bAutoWireClothingLeaderPoseToBody && !UsesMetaHumanNativeClothingPipeline();
}

bool UGodfreyPerformerAnimationBridgeComponent::ShouldManageMetaHumanGarmentsAtRuntime() const
{
	return bManageMetaHumanGarmentsAtRuntime && UsesMetaHumanNativeClothingPipeline();
}

bool UGodfreyPerformerAnimationBridgeComponent::HasMetaHumanGarmentPostProcessMesh() const
{
	if (!UsesMetaHumanNativeClothingPipeline())
	{
		return false;
	}

	// Shirt copy-pose path applies to Torso only; Feet shoe post-process must not enable it (Erno leader-pose shirt).
	const USkeletalMeshComponent* Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	return GarmentMeshHasPostProcessAnim(Torso);
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMetaHumanFaceVisible()
{
	USkeletalMeshComponent* Face = FindFollowerMeshByComponentName(FName(TEXT("Face")));
	if (!IsValid(Face))
	{
		return;
	}

	Face->SetHiddenInGame(false, true);
	Face->SetVisibility(true, true);
	Face->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Face->bEnableUpdateRateOptimizations = false;

#if WITH_EDITOR
	Face->SetUpdateAnimationInEditor(true);
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMetaHumanCopyPoseGarmentAttachedToBody(
	USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment)
{
	if (!IsDriveableSkeletalMesh(Body) || !IsDriveableSkeletalMesh(Garment) || Garment == Body)
	{
		return;
	}

	if (Garment->GetAttachParent() == Body)
	{
		return;
	}

	Garment->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: attached '%s' -> Body '%s' (CopyPose bUseAttachedParent fallback)."),
		*Garment->GetName(),
		*Body->GetName());
}

void UGodfreyPerformerAnimationBridgeComponent::ClearPostProcessGarmentLeaderPose(USkeletalMeshComponent* Garment)
{
	if (!IsValid(Garment) || !Garment->LeaderPoseComponent.IsValid())
	{
		return;
	}

	const USkinnedMeshComponent* PreviousLeader = Garment->LeaderPoseComponent.Get();
	Garment->SetLeaderPoseComponent(nullptr, false, true);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: cleared leader pose on '%s' (was '%s'; shirt uses post-process CopyPose, not leader)."),
		*Garment->GetName(),
		PreviousLeader ? *PreviousLeader->GetName() : TEXT("(none)"));
}

void UGodfreyPerformerAnimationBridgeComponent::WireClothingPostProcessCopyPoseSource(
	USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment, const bool bLogWiringResult)
{
	if (!IsValid(Body) || !IsValid(Garment))
	{
		return;
	}

	EnsureMetaHumanCopyPoseGarmentAttachedToBody(Body, Garment);

	if (!Garment->GetPostProcessInstance() && Garment->IsRegistered())
	{
		Garment->InitializeAnimScriptInstance(false, false);
	}

	UAnimInstance* const PostProcessInstance = Garment->GetPostProcessInstance();
	if (!PostProcessInstance)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: no post-process AnimInstance on '%s' — cannot wire CopyPose source yet."),
			*Garment->GetName());
		return;
	}

	RefreshMetaHumanBodyPoseChain(Body);

	const int32 AnimGraphNodesWired = WireCopyPoseFromMeshAnimGraphNodes(PostProcessInstance, Body);

	static const FName PreferredPropertyNames[] = {
		TEXT("Source Mesh Component"),
		TEXT("SourceMeshComponent"),
		TEXT("Mesh to Copy"),
		TEXT("MeshToCopy"),
		TEXT("Body Mesh"),
		TEXT("BodyMesh"),
		TEXT("Source Mesh"),
		TEXT("SkeletalMeshToCopy"),
	};

	FName WiredPropertyName;
	const bool bWiredUObjectProperty = WireAnimInstanceSkeletalMeshComponentProperty(
		PostProcessInstance, Body, PreferredPropertyNames, &WiredPropertyName);

	ApplyMetaHumanGarmentPostProcessVariables(Garment, PostProcessInstance);

	Garment->SetDisablePostProcessBlueprint(false);

	if (bLogWiringResult)
	{
		if (AnimGraphNodesWired > 0)
		{
			if (Garment->GetFName() == FName(TEXT("Torso")))
			{
				if (!bLoggedCopyPoseWireForTorso)
				{
					bLoggedCopyPoseWireForTorso = true;
					UE_LOG(LogGodfreyPerformance, Log,
						TEXT("GodfreyPerformerBridge: CopyPose anim graph wired (%d node(s)) on '%s' -> Body '%s' (SourceMeshComponent + bUseAttachedParent)."),
						AnimGraphNodesWired,
						*Garment->GetName(),
						*Body->GetName());
				}
			}
			else
			{
				UE_LOG(LogGodfreyPerformance, Log,
					TEXT("GodfreyPerformerBridge: CopyPose anim graph wired (%d node(s)) on '%s' -> Body '%s' (SourceMeshComponent + bUseAttachedParent)."),
					AnimGraphNodesWired,
					*Garment->GetName(),
					*Body->GetName());
			}
		}
		else if (bWiredUObjectProperty)
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: CopyPose source '%s' on '%s' -> Body '%s'."),
				*WiredPropertyName.ToString(),
				*Garment->GetName(),
				*Body->GetName());
		}
		else
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: CopyPose on '%s' has no UObject source pin — relying on attach-parent Body '%s' (attachParent='%s')."),
				*Garment->GetName(),
				*Body->GetName(),
				Garment->GetAttachParent() ? *Garment->GetAttachParent()->GetName() : TEXT("(none)"));
		}
	}

	RefreshMetaHumanGarmentPostProcessPose(Body, Garment);
}

void UGodfreyPerformerAnimationBridgeComponent::ApplyMetaHumanGarmentPostProcessVariables(
	USkeletalMeshComponent* Garment, UAnimInstance* PostProcessInstance)
{
	if (!IsValid(Garment) || !PostProcessInstance)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const UMetaHumanComponentUE* MetaHumanComponent = Owner->FindComponentByClass<UMetaHumanComponentUE>();
	if (!MetaHumanComponent)
	{
		return;
	}

	const FName GarmentLabel(*Garment->GetName());
	const FMetaHumanCustomizableBodyPart* BodyPartConfig = GetMetaHumanBodyPartConfig(MetaHumanComponent, GarmentLabel);
	if (!BodyPartConfig)
	{
		return;
	}

	FMetaHumanCustomizableBodyPart EffectivePart = *BodyPartConfig;
	if (const UAnimInstance* PPDefault = Cast<UAnimInstance>(PostProcessInstance->GetClass()->GetDefaultObject()))
	{
		if (!EffectivePart.ControlRigClass)
		{
			MetaHumanComponentHelpers::GetPropertyValue(
				const_cast<UAnimInstance*>(PPDefault), TEXTVIEW("Control Rig Class"), EffectivePart.ControlRigClass);
		}

		if (!EffectivePart.PhysicsAsset)
		{
			MetaHumanComponentHelpers::GetPropertyValue(
				const_cast<UAnimInstance*>(PPDefault), TEXTVIEW("Override Physics Asset"), EffectivePart.PhysicsAsset);
		}
	}

	MirrorMetaHumanPostConnectAnimBPVariables(EffectivePart, PostProcessInstance);

#if WITH_EDITOR
	if (IsEditorViewportWorld() && GarmentLabel == FName(TEXT("Torso")))
	{
		ApplyEditorTorsoCopyPoseOnlyOverrides(PostProcessInstance);
	}
#endif
}

bool UGodfreyPerformerAnimationBridgeComponent::AreMetaHumanGarmentMeshesRegistered() const
{
	if (IsValid(TargetSkeletalMesh) && HasRenderableSkeletalMeshAsset(TargetSkeletalMesh)
		&& !TargetSkeletalMesh->IsRegistered())
	{
		return false;
	}

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		const USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		if (!Follower->IsRegistered())
		{
			return false;
		}
	}

	return true;
}

void UGodfreyPerformerAnimationBridgeComponent::RefreshMetaHumanGarmentPoses(USkeletalMeshComponent* Body)
{
	if (!IsDriveableSkeletalMesh(Body))
	{
		return;
	}

	Body->TickAnimation(0.f, false);
	Body->RefreshBoneTransforms();

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsDriveableSkeletalMesh(Follower) || Follower == Body)
		{
			continue;
		}

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			ClearPostProcessGarmentLeaderPose(Follower);
			WireClothingPostProcessCopyPoseSource(Body, Follower);
			ForceSkeletalMeshPoseRefresh(Follower);
		}
		else if (Follower->LeaderPoseComponent.Get() == Body)
		{
			Follower->UpdateFollowerComponent();
		}
		else
		{
			ForceSkeletalMeshPoseRefresh(Follower);
		}
	}

	Body->RefreshFollowerComponents();
}

void UGodfreyPerformerAnimationBridgeComponent::ScheduleMetaHumanClothingRefreshPasses()
{
	if (bMetaHumanClothingRefreshPassesScheduled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bMetaHumanClothingRefreshPassesScheduled = true;
	MetaHumanClothingRefreshPassCount = 0;

	FTimerDelegate Delegate = FTimerDelegate::CreateUObject(
		this, &UGodfreyPerformerAnimationBridgeComponent::DeferredMetaHumanClothingRefresh);

	World->GetTimerManager().SetTimerForNextTick(Delegate);

	FTimerHandle DelayedRefreshHandle;
	World->GetTimerManager().SetTimer(DelayedRefreshHandle, Delegate, 0.05f, false);

	FTimerHandle LateRefreshHandle;
	World->GetTimerManager().SetTimer(LateRefreshHandle, Delegate, 0.15f, false);

	FTimerHandle PieSettleHandle;
	World->GetTimerManager().SetTimer(PieSettleHandle, Delegate, 0.5f, false);

	FTimerHandle PieLateHandle;
	World->GetTimerManager().SetTimer(PieLateHandle, Delegate, 1.0f, false);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: scheduled %d MetaHuman garment refresh passes."),
		MetaHumanMaxClothingRefreshPasses);
}

void UGodfreyPerformerAnimationBridgeComponent::DeferredMetaHumanClothingRefresh()
{
	if (!UsesMetaHumanNativeClothingPipeline())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	if (!AreMetaHumanGarmentMeshesRegistered())
	{
		if (MetaHumanGarmentRegisterWaitFrames < MetaHumanMaxGarmentRegisterWaitFrames)
		{
			++MetaHumanGarmentRegisterWaitFrames;
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
					this, &UGodfreyPerformerAnimationBridgeComponent::DeferredMetaHumanClothingRefresh));
			}
		}
		return;
	}

	MetaHumanGarmentRegisterWaitFrames = 0;

	if (MetaHumanClothingRefreshPassCount >= MetaHumanMaxClothingRefreshPasses)
	{
		return;
	}

	++MetaHumanClothingRefreshPassCount;

	// MetaHumanComponentUE::BeginPlay runs after this component and resets OnlyTickPoseWhenRendered.
	if (ShouldManageMetaHumanGarmentsAtRuntime() && HasMetaHumanGarmentPostProcessMesh())
	{
		EnsureMetaHumanCopyPoseBodySource();
	}

	if (ShouldManageMetaHumanGarmentsAtRuntime())
	{
		EnsureMetaHumanBodyTicksForClothingPostProcess();
	}

	if (!IsValid(TargetSkeletalMesh))
	{
		return;
	}

	RefreshMetaHumanGarmentPoses(TargetSkeletalMesh);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: MetaHuman garment refresh pass %d complete."),
		MetaHumanClothingRefreshPassCount);

	if (MetaHumanClothingRefreshPassCount == 1)
	{
		LogSkeletalMeshPropagationReport();
	}

#if WITH_EDITOR
	ReapplyEditorGarmentPreviewSettings();
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::ApplyMetaHumanClothingTickPrerequisites(
	USkeletalMeshComponent* Body)
{
	if (!IsValid(Body) || bMetaHumanClothingTickPrerequisitesApplied)
	{
		return;
	}

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || Follower == Body)
		{
			continue;
		}

		Follower->AddTickPrerequisiteComponent(Body);
	}

	bMetaHumanClothingTickPrerequisitesApplied = true;

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: garment mesh tick prerequisites -> Body '%s'."),
		*Body->GetName());
}

void UGodfreyPerformerAnimationBridgeComponent::TryStabilizeClothingForEditorViewport()
{
	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		return;
	}

#if WITH_EDITOR
	const bool bCopyPoseGarment = HasMetaHumanGarmentPostProcessMesh();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: editor stabilize on '%s' (copyPoseGarment=%d metaHuman=%d)."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
		bCopyPoseGarment ? 1 : 0,
		UsesMetaHumanNativeClothingPipeline() ? 1 : 0);

	if (ShouldManageMetaHumanGarmentsAtRuntime() && bCopyPoseGarment)
	{
		if (bCopyPoseGarment)
		{
			if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
			{
				ResolveTargetBodyMesh();
			}

			if (!AreMetaHumanGarmentMeshesRegistered())
			{
				if (MetaHumanGarmentRegisterWaitFrames < MetaHumanMaxGarmentRegisterWaitFrames)
				{
					++MetaHumanGarmentRegisterWaitFrames;
					if (UWorld* MutableWorld = GetWorld())
					{
						MutableWorld->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
							this, &UGodfreyPerformerAnimationBridgeComponent::TryStabilizeClothingForEditorViewport));
					}
				}
				return;
			}

			MetaHumanGarmentRegisterWaitFrames = 0;
			EnsureMetaHumanCopyPoseBodySource();
			RestoreEditorMetaHumanViewportDefaults();
			RefreshMetaHumanGarmentPoses(TargetSkeletalMesh);
			MaybeLogMetaHumanShirtDiagnostics(TEXT("EditorStabilize"), true);
			EnsureEditorShirtDiagnosticTicker();
		}

		return;
	}

	if (!ShouldManageClothingLeaderPose())
	{
		return;
	}

	if (UWorld* MutableWorld = GetWorld())
	{
		MutableWorld->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::DeferredClothingStabilize));
	}
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMetaHumanCopyPoseBodySource()
{
	if (!HasMetaHumanGarmentPostProcessMesh())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (!IsDriveableSkeletalMesh(Body))
	{
		return;
	}

	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->bEnableUpdateRateOptimizations = false;
	Body->bEnableAnimation = true;
	Body->SetDisablePostProcessBlueprint(false);
	ApplyMetaHumanCopyPoseBodyVisibility(Body, IsEditorViewportWorld());

#if WITH_EDITOR
	Body->SetUpdateAnimationInEditor(true);
#endif

	Body->TickAnimation(0.f, false);
	ForceSkeletalMeshPoseRefresh(Body);

	EnsureMetaHumanFaceVisible();
	ApplyMetaHumanClothingTickPrerequisites(Body);

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Follower->bEnableUpdateRateOptimizations = false;
		Follower->SetHiddenInGame(false, true);

#if WITH_EDITOR
		Follower->SetUpdateAnimationInEditor(true);
#endif

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			ClearPostProcessGarmentLeaderPose(Follower);
			EnsureMetaHumanCopyPoseGarmentAttachedToBody(Body, Follower);
			WireClothingPostProcessCopyPoseSource(Body, Follower, false);
		}
		else if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
			Follower->UpdateFollowerComponent();
		}
	}

	Body->RefreshFollowerComponents();

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: MetaHuman CopyPose body source ready (Body+all garments AlwaysTick; leader pose preserved on Legs/Feet)."));
}

void UGodfreyPerformerAnimationBridgeComponent::MaintainMetaHumanCopyPoseBodySource()
{
	if (!HasMetaHumanGarmentPostProcessMesh() || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (Body->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
	{
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Body->bEnableUpdateRateOptimizations = false;
	}

	if (!Body->IsVisible())
	{
		ApplyMetaHumanCopyPoseBodyVisibility(Body, IsEditorViewportWorld());
	}

#if WITH_EDITOR
	if (IsEditorViewportWorld())
	{
		Body->SetUpdateAnimationInEditor(true);
		Body->SetHiddenInGame(false, true);
		RefreshMetaHumanBodyPoseChain(Body);
	}
	else
	{
		Body->SetUpdateAnimationInEditor(true);
		RefreshMetaHumanBodyPoseChain(Body);
	}
#else
	RefreshMetaHumanBodyPoseChain(Body);
#endif

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		if (Follower->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Follower->bEnableUpdateRateOptimizations = false;
		}

		if (Follower->bHiddenInGame)
		{
			Follower->SetHiddenInGame(false, true);
		}

#if WITH_EDITOR
		if (IsEditorViewportWorld())
		{
			Follower->SetUpdateAnimationInEditor(true);
		}
#endif

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			ClearPostProcessGarmentLeaderPose(Follower);
			EnsureMetaHumanCopyPoseGarmentAttachedToBody(Body, Follower);
			WireClothingPostProcessCopyPoseSource(Body, Follower, false);
#if WITH_EDITOR
			if (IsEditorViewportWorld() && Follower->GetFName() == FName(TEXT("Torso")))
			{
				Follower->SetDisablePostProcessBlueprint(false);
				if (UAnimInstance* const PostProcessInstance = Follower->GetPostProcessInstance())
				{
					ApplyEditorTorsoCopyPoseOnlyOverrides(PostProcessInstance);
				}
			}
#endif
		}
		else if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
			Follower->UpdateFollowerComponent();
		}
		else
		{
			Follower->UpdateFollowerComponent();
		}
	}

}

#if WITH_EDITOR
void UGodfreyPerformerAnimationBridgeComponent::PinEditorViewportMetaHumanLOD()
{
	if (!IsBridgeActiveForMetaHumanIntervention()
		|| !IsEditorViewportWorld()
		|| !UsesMetaHumanNativeClothingPipeline())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	if (AActor* const Owner = GetOwner())
	{
		if (ULODSyncComponent* const LODSync = Owner->FindComponentByClass<ULODSyncComponent>())
		{
			LODSync->ForcedLOD = 0;
			LODSync->SetComponentTickEnabled(false);
		}
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (IsDriveableSkeletalMesh(Body))
	{
		Body->SetForcedLOD(0);
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Body->SetUpdateAnimationInEditor(true);
	}

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		if (USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName))
		{
			Follower->SetForcedLOD(0);
			Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Follower->SetUpdateAnimationInEditor(true);
			if (Follower->LeaderPoseComponent.Get() == Body)
			{
				Follower->UpdateFollowerComponent();
			}
		}
	}

	if (IsDriveableSkeletalMesh(Body))
	{
		Body->RefreshFollowerComponents();
	}

	if (!bLoggedEditorViewportLODPin)
	{
		bLoggedEditorViewportLODPin = true;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: editor viewport LOD pinned (LODSync forced=0, garment meshes LOD 0; garment bridge off)."));
	}
}

void UGodfreyPerformerAnimationBridgeComponent::RestoreEditorTorsoCopyPoseGarment(
	USkeletalMeshComponent* Body, USkeletalMeshComponent* Torso)
{
	if (!IsEditorViewportWorld() || !IsValid(Body) || !IsValid(Torso))
	{
		return;
	}

	Torso->SetDisablePostProcessBlueprint(false);
	Torso->SetForcedLOD(0);
	ClearPostProcessGarmentLeaderPose(Torso);
	EnsureMetaHumanCopyPoseGarmentAttachedToBody(Body, Torso);
	WireClothingPostProcessCopyPoseSource(Body, Torso, false);
	if (UAnimInstance* const PostProcessInstance = Torso->GetPostProcessInstance())
	{
		ApplyEditorTorsoCopyPoseOnlyOverrides(PostProcessInstance);
	}
	Torso->UpdateFollowerComponent();
	ForceSkeletalMeshPoseRefresh(Torso);
}

void UGodfreyPerformerAnimationBridgeComponent::RestoreEditorMetaHumanViewportDefaults()
{
	if (!IsEditorViewportWorld())
	{
		return;
	}

	if (AActor* const Owner = GetOwner())
	{
		if (ULODSyncComponent* const LODSync = Owner->FindComponentByClass<ULODSyncComponent>())
		{
			LODSync->ForcedLOD = -1;
			if (!LODSync->IsComponentTickEnabled())
			{
				LODSync->SetComponentTickEnabled(true);
				UE_LOG(LogGodfreyPerformance, Log,
					TEXT("GodfreyPerformerBridge: editor LODSync restored (auto LOD, tick re-enabled)."));
			}
		}
	}

	if (!IsValid(TargetSkeletalMesh))
	{
		return;
	}

	USkeletalMeshComponent* const Body = TargetSkeletalMesh;
	Body->SetForcedLOD(0);

	USkeletalMeshComponent* const Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	if (IsValid(Torso))
	{
		RestoreEditorTorsoCopyPoseGarment(Body, Torso);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::ReapplyEditorGarmentPreviewSettings()
{
	RestoreEditorMetaHumanViewportDefaults();
}

void UGodfreyPerformerAnimationBridgeComponent::ApplyEditorTorsoCopyPoseOnlyOverrides(
	UAnimInstance* PostProcessInstance) const
{
	if (!PostProcessInstance)
	{
		return;
	}

	static constexpr int32 NeverActiveLODThreshold = 999;
	MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
		PostProcessInstance, TEXT("Enable Control Rig"), false);
	MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
		PostProcessInstance, TEXT("Enable Rigid Body Simulation"), false);
	MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
		PostProcessInstance, TEXT("Control Rig LOD Threshold"), NeverActiveLODThreshold);
	MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
		PostProcessInstance, TEXT("Rigid Body LOD Threshold"), NeverActiveLODThreshold);

	static bool bLoggedEditorTorsoSimOff = false;
	if (!bLoggedEditorTorsoSimOff)
	{
		bLoggedEditorTorsoSimOff = true;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: editor Torso CopyPose-only (CR/RB off, LOD thresholds pinned)."));
	}
}

void UGodfreyPerformerAnimationBridgeComponent::StabilizeEditorTorsoOnViewportChange()
{
	if (!ShouldManageMetaHumanGarmentsAtRuntime()
		|| !IsEditorViewportWorld()
		|| !HasMetaHumanGarmentPostProcessMesh())
	{
		return;
	}

	USkeletalMeshComponent* const Body = TargetSkeletalMesh;
	USkeletalMeshComponent* const Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	if (!IsDriveableSkeletalMesh(Body) || !IsDriveableSkeletalMesh(Torso))
	{
		return;
	}

	RestoreEditorTorsoCopyPoseGarment(Body, Torso);
	RefreshMetaHumanBodyPoseChain(Body);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: editor zoom stabilize on Torso (CopyPose, CR/RB off)."));
}
#endif

void UGodfreyPerformerAnimationBridgeComponent::MaybeLogMetaHumanShirtDiagnostics(
	const TCHAR* TriggerReason, const bool bForce)
{
	if (!bLogMetaHumanShirtDiagnostics)
	{
		return;
	}

	USkeletalMeshComponent* const Body = TargetSkeletalMesh;
	USkeletalMeshComponent* const Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	if (!IsValid(Body) || !IsValid(Torso))
	{
		return;
	}

	const int32 BodyTickOpt = static_cast<int32>(Body->VisibilityBasedAnimTickOption);
	const int32 TorsoTickOpt = static_cast<int32>(Torso->VisibilityBasedAnimTickOption);
	const int32 BodyVisible = Body->IsVisible() ? 1 : 0;
	const int32 TorsoVisible = Torso->IsVisible() ? 1 : 0;
	const int32 BodyHiddenInGame = Body->bHiddenInGame ? 1 : 0;

	bool bStateChanged = BodyTickOpt != LastLoggedBodyTickOpt
		|| TorsoTickOpt != LastLoggedTorsoTickOpt
		|| BodyVisible != LastLoggedBodyVisible
		|| TorsoVisible != LastLoggedTorsoVisible
		|| BodyHiddenInGame != LastLoggedBodyHiddenInGame;

	const int32 BodyLOD = Body->GetPredictedLODLevel();
	const int32 TorsoLOD = Torso->GetPredictedLODLevel();
	bStateChanged = bStateChanged
		|| BodyLOD != LastLoggedBodyLOD
		|| TorsoLOD != LastLoggedTorsoLOD;

	bool bZoomChanged = false;
#if WITH_EDITOR
	if (IsEditorViewportWorld())
	{
		const FEditorViewportWatch Watch = GetEditorViewportWatch(Torso);
		if (Watch.ViewDistance > 0.f)
		{
			if (LastEditorViewDistanceToTorso > 0.f)
			{
				const float AbsDelta = FMath::Abs(Watch.ViewDistance - LastEditorViewDistanceToTorso);
				const float RelativeChange = AbsDelta / FMath::Max(LastEditorViewDistanceToTorso, 1.f);
				bZoomChanged = RelativeChange >= 0.03f || AbsDelta >= 10.f;
			}
			if (LastEditorViewFOV > 0.f && Watch.ViewFOV > 0.f)
			{
				bZoomChanged = bZoomChanged || FMath::Abs(Watch.ViewFOV - LastEditorViewFOV) >= 0.25f;
			}
		}
	}
#endif

	FVector BodyPelvis = FVector::ZeroVector;
	FVector TorsoPelvis = FVector::ZeroVector;
	const bool bHasBodyPelvis = GetBoneWorldLocationSafe(Body, FName(TEXT("pelvis")), BodyPelvis);
	const bool bHasTorsoPelvis = GetBoneWorldLocationSafe(Torso, FName(TEXT("pelvis")), TorsoPelvis);
	const float PelvisDeltaCm = (bHasBodyPelvis && bHasTorsoPelvis) ? FVector::Dist(BodyPelvis, TorsoPelvis) : -1.f;

	const bool bExplosionSuspect = PelvisDeltaCm >= ShirtExplosionPelvisDeltaThresholdCm;
	const bool bPelvisDeltaJump = LastLoggedPelvisDeltaCm >= 0.f && PelvisDeltaCm >= 0.f
		&& FMath::Abs(PelvisDeltaCm - LastLoggedPelvisDeltaCm) >= 25.f;

	const UWorld* const World = GetWorld();
	const double NowSeconds = World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
	const bool bIntervalElapsed = (NowSeconds - LastShirtDiagnosticLogTimeSeconds) >= static_cast<double>(ShirtDiagnosticMinLogInterval);
	const bool bMeaningfulChange = bStateChanged || bZoomChanged || bExplosionSuspect || bPelvisDeltaJump;

#if WITH_EDITOR
	if (bZoomChanged && IsEditorViewportWorld())
	{
		StabilizeEditorTorsoOnViewportChange();
	}
#endif

	if (!bForce && !bMeaningfulChange && !bIntervalElapsed)
	{
#if WITH_EDITOR
		if (IsEditorViewportWorld())
		{
			const FEditorViewportWatch Watch = GetEditorViewportWatch(Torso);
			if (Watch.ViewDistance > 0.f)
			{
				LastEditorViewDistanceToTorso = Watch.ViewDistance;
			}
			if (Watch.ViewFOV > 0.f)
			{
				LastEditorViewFOV = Watch.ViewFOV;
			}
		}
#endif
		return;
	}

	if (!bForce && !bMeaningfulChange && bIntervalElapsed && TriggerReason
		&& (FCString::Strcmp(TriggerReason, TEXT("MaintainCopyPose")) == 0
			|| FCString::Strcmp(TriggerReason, TEXT("EditorTicker")) == 0))
	{
#if WITH_EDITOR
		if (IsEditorViewportWorld())
		{
			const FEditorViewportWatch Watch = GetEditorViewportWatch(Torso);
			if (Watch.ViewDistance > 0.f)
			{
				LastEditorViewDistanceToTorso = Watch.ViewDistance;
			}
			if (Watch.ViewFOV > 0.f)
			{
				LastEditorViewFOV = Watch.ViewFOV;
			}
		}
#endif
		return;
	}

	const TCHAR* EffectiveReason = TriggerReason;
	if (bExplosionSuspect)
	{
		EffectiveReason = TEXT("EXPLOSION_SUSPECT");
	}
	else if (bZoomChanged)
	{
		EffectiveReason = TEXT("EDITOR_ZOOM");
	}
	else if (bStateChanged)
	{
		EffectiveReason = TEXT("STATE_CHANGE");
	}

#if WITH_EDITOR
	int32 LogBodyLOD = BodyLOD;
	int32 LogTorsoLOD = TorsoLOD;
	const int32 bLodMismatch = (LogTorsoLOD != LogBodyLOD) ? 1 : 0;
	if (IsEditorViewportWorld() && bLodMismatch)
	{
		RestoreEditorTorsoCopyPoseGarment(Body, Torso);
		LogBodyLOD = Body->GetPredictedLODLevel();
		LogTorsoLOD = Torso->GetPredictedLODLevel();
	}
	const ILODSyncInterface* const TorsoLODInterface = Cast<ILODSyncInterface>(Torso);
	const int32 TorsoStreamLOD = TorsoLODInterface ? TorsoLODInterface->GetForceStreamedLOD() : INDEX_NONE;
	const int32 TorsoBestLOD = TorsoLODInterface ? TorsoLODInterface->GetBestAvailableLOD() : INDEX_NONE;
	const int32 bLeaderBody = (Torso->LeaderPoseComponent.Get() == Body) ? 1 : 0;
	const int32 bPPOff = Torso->GetDisablePostProcessBlueprint() ? 1 : 0;
#else
	const int32 LogBodyLOD = BodyLOD;
	const int32 LogTorsoLOD = TorsoLOD;
	const int32 bLodMismatch = 0;
	const int32 TorsoStreamLOD = INDEX_NONE;
	const int32 TorsoBestLOD = INDEX_NONE;
	const int32 bLeaderBody = 0;
	const int32 bPPOff = 0;
#endif

	LastLoggedBodyTickOpt = BodyTickOpt;
	LastLoggedTorsoTickOpt = TorsoTickOpt;
	LastLoggedBodyVisible = BodyVisible;
	LastLoggedTorsoVisible = TorsoVisible;
	LastLoggedBodyHiddenInGame = BodyHiddenInGame;
	LastLoggedBodyLOD = BodyLOD;
	LastLoggedTorsoLOD = TorsoLOD;
	LastLoggedPelvisDeltaCm = PelvisDeltaCm;
	LastShirtDiagnosticLogTimeSeconds = NowSeconds;

#if WITH_EDITOR
	if (IsEditorViewportWorld())
	{
		const FEditorViewportWatch Watch = GetEditorViewportWatch(Torso);
		if (Watch.ViewDistance > 0.f)
		{
			LastEditorViewDistanceToTorso = Watch.ViewDistance;
		}
		if (Watch.ViewFOV > 0.f)
		{
			LastEditorViewFOV = Watch.ViewFOV;
		}
	}
#endif

	if (bExplosionSuspect || bPelvisDeltaJump)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("ShirtDiag [%s] trigger=%s pelvis=%.1fcm maxBone=%.1fcm boundsR=%.0f bodyLOD=%d torsoLOD=%d forcedLOD=%d/%d CR=%d RB=%d zoom=%d state=%d viewDist=%.0f strm=%d best=%d lp=%d pp=%d lodMis=%d"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
			EffectiveReason,
			PelvisDeltaCm,
			ComputeMaxWatchBoneDeltaCm(Body, Torso),
			Torso->Bounds.SphereRadius,
			LogBodyLOD,
			LogTorsoLOD,
			Body->GetForcedLOD(),
			Torso->GetForcedLOD(),
			ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Control Rig")),
			ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Rigid Body Simulation")),
			bZoomChanged ? 1 : 0,
			bStateChanged ? 1 : 0,
			LastEditorViewDistanceToTorso,
			TorsoStreamLOD,
			TorsoBestLOD,
			bLeaderBody,
			bPPOff,
			bLodMismatch);
	}
	else if (bZoomChanged || bStateChanged || bForce || bLodMismatch)
	{
		if (bLodMismatch)
		{
			UE_LOG(LogGodfreyPerformance, Warning,
				TEXT("ShirtDiag [%s] trigger=%s pelvis=%.1fcm maxBone=%.1fcm boundsR=%.0f bodyLOD=%d torsoLOD=%d forcedLOD=%d/%d CR=%d RB=%d zoom=%d state=%d viewDist=%.0f strm=%d best=%d lp=%d pp=%d lodMis=%d"),
				GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
				EffectiveReason,
				PelvisDeltaCm,
				ComputeMaxWatchBoneDeltaCm(Body, Torso),
				Torso->Bounds.SphereRadius,
				LogBodyLOD,
				LogTorsoLOD,
				Body->GetForcedLOD(),
				Torso->GetForcedLOD(),
				ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Control Rig")),
				ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Rigid Body Simulation")),
				bZoomChanged ? 1 : 0,
				bStateChanged ? 1 : 0,
				LastEditorViewDistanceToTorso,
				TorsoStreamLOD,
				TorsoBestLOD,
				bLeaderBody,
				bPPOff,
				bLodMismatch);
		}
		else
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("ShirtDiag [%s] trigger=%s pelvis=%.1fcm maxBone=%.1fcm boundsR=%.0f bodyLOD=%d torsoLOD=%d forcedLOD=%d/%d CR=%d RB=%d zoom=%d state=%d viewDist=%.0f strm=%d best=%d lp=%d pp=%d lodMis=%d"),
				GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
				EffectiveReason,
				PelvisDeltaCm,
				ComputeMaxWatchBoneDeltaCm(Body, Torso),
				Torso->Bounds.SphereRadius,
				LogBodyLOD,
				LogTorsoLOD,
				Body->GetForcedLOD(),
				Torso->GetForcedLOD(),
				ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Control Rig")),
				ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Rigid Body Simulation")),
				bZoomChanged ? 1 : 0,
				bStateChanged ? 1 : 0,
				LastEditorViewDistanceToTorso,
				TorsoStreamLOD,
				TorsoBestLOD,
				bLeaderBody,
				bPPOff,
				bLodMismatch);
		}
	}

	if (bExplosionSuspect || bPelvisDeltaJump || bZoomChanged || bStateChanged || bForce)
	{
		LogMetaHumanShirtDiagnosticSnapshot(EffectiveReason);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::LogMetaHumanShirtDiagnosticSnapshot(const TCHAR* TriggerReason) const
{
	USkeletalMeshComponent* const Body = TargetSkeletalMesh;
	USkeletalMeshComponent* const Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	if (!IsValid(Body) || !IsValid(Torso))
	{
		return;
	}

	const USceneComponent* const TorsoAttachParent = Torso->GetAttachParent();
	const USkinnedMeshComponent* const TorsoLeader = Torso->LeaderPoseComponent.Get();
	UAnimInstance* const BodyPP = Body->GetPostProcessInstance();
	UAnimInstance* const TorsoPP = Torso->GetPostProcessInstance();

	bool bEnableControlRig = false;
	bool bEnableRigidBody = false;
	if (TorsoPP)
	{
		ReadPostProcessBoolProperty(TorsoPP, TEXT("Enable Control Rig"), bEnableControlRig);
		ReadPostProcessBoolProperty(TorsoPP, TEXT("Enable Rigid Body Simulation"), bEnableRigidBody);
	}

	FVector BodyPelvis = FVector::ZeroVector;
	FVector TorsoPelvis = FVector::ZeroVector;
	FVector BodySpine05 = FVector::ZeroVector;
	FVector TorsoSpine05 = FVector::ZeroVector;
	const bool bBodyPelvis = GetBoneWorldLocationSafe(Body, FName(TEXT("pelvis")), BodyPelvis);
	const bool bTorsoPelvis = GetBoneWorldLocationSafe(Torso, FName(TEXT("pelvis")), TorsoPelvis);
	const bool bBodySpine = GetBoneWorldLocationSafe(Body, FName(TEXT("spine_05")), BodySpine05);
	const bool bTorsoSpine = GetBoneWorldLocationSafe(Torso, FName(TEXT("spine_05")), TorsoSpine05);

	const float PelvisDeltaCm = (bBodyPelvis && bTorsoPelvis) ? FVector::Dist(BodyPelvis, TorsoPelvis) : -1.f;
	const float SpineDeltaCm = (bBodySpine && bTorsoSpine) ? FVector::Dist(BodySpine05, TorsoSpine05) : -1.f;

	float MaxWatchBoneDeltaCm = 0.f;
	{
		static const FName WatchBones[] = {
			FName(TEXT("pelvis")),
			FName(TEXT("spine_05")),
			FName(TEXT("clavicle_l")),
			FName(TEXT("clavicle_r")),
		};
		for (const FName BoneName : WatchBones)
		{
			FVector BodyPos = FVector::ZeroVector;
			FVector TorsoPos = FVector::ZeroVector;
			if (GetBoneWorldLocationSafe(Body, BoneName, BodyPos) && GetBoneWorldLocationSafe(Torso, BoneName, TorsoPos))
			{
				MaxWatchBoneDeltaCm = FMath::Max(MaxWatchBoneDeltaCm, FVector::Dist(BodyPos, TorsoPos));
			}
		}
	}

	const float TorsoBoundsRadius = Torso->Bounds.SphereRadius;

#if WITH_EDITOR
	const FEditorViewportWatch ViewWatch = IsEditorViewportWorld() ? GetEditorViewportWatch(Torso) : FEditorViewportWatch{};
	const float ViewDistance = ViewWatch.ViewDistance;
	const float ViewFOV = ViewWatch.ViewFOV;
	const bool bBodyEditorAnim = Body->GetUpdateAnimationInEditor();
	const bool bTorsoEditorAnim = Torso->GetUpdateAnimationInEditor();
#else
	const float ViewDistance = -1.f;
	const float ViewFOV = -1.f;
	const bool bBodyEditorAnim = false;
	const bool bTorsoEditorAnim = false;
#endif

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("ShirtDiag snapshot reason=%s world=%s editorViewport=%d manageGarmentsRuntime=%d copyPoseGarment=%d"),
		TriggerReason ? TriggerReason : TEXT("(none)"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("(none)"),
		IsEditorViewportWorld() ? 1 : 0,
		bManageMetaHumanGarmentsAtRuntime ? 1 : 0,
		UsesMetaHumanNativeClothingPipeline() ? 1 : 0);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("  Body '%s': visible=%d hiddenInGame=%d tickOpt=%s lod=%d registered=%d animInst=%s postProcess=%s editorAnim=%d"),
		*Body->GetName(),
		Body->IsVisible() ? 1 : 0,
		Body->bHiddenInGame ? 1 : 0,
		AnimTickOptionToString(Body->VisibilityBasedAnimTickOption),
		Body->GetPredictedLODLevel(),
		Body->IsRegistered() ? 1 : 0,
		Body->GetAnimInstance() ? *Body->GetAnimInstance()->GetClass()->GetName() : TEXT("(none)"),
		BodyPP ? *BodyPP->GetClass()->GetName() : TEXT("(none)"),
		bBodyEditorAnim ? 1 : 0);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("  Torso '%s': visible=%d hiddenInGame=%d tickOpt=%s lod=%d registered=%d attach='%s' leader='%s' postProcess=%s editorAnim=%d"),
		*Torso->GetName(),
		Torso->IsVisible() ? 1 : 0,
		Torso->bHiddenInGame ? 1 : 0,
		AnimTickOptionToString(Torso->VisibilityBasedAnimTickOption),
		Torso->GetPredictedLODLevel(),
		Torso->IsRegistered() ? 1 : 0,
		TorsoAttachParent ? *TorsoAttachParent->GetName() : TEXT("(none)"),
		TorsoLeader ? *TorsoLeader->GetName() : TEXT("(none)"),
		TorsoPP ? *TorsoPP->GetClass()->GetName() : TEXT("(none)"),
		bTorsoEditorAnim ? 1 : 0);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("  Torso PP vars: EnableControlRig=%d EnableRigidBody=%d disablePPBlueprint=%d"),
		bEnableControlRig ? 1 : 0,
		bEnableRigidBody ? 1 : 0,
		Torso->GetDisablePostProcessBlueprint() ? 1 : 0);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("  Bones world: bodyPelvis=%s torsoPelvis=%s pelvisDelta=%.1fcm spineDelta=%.1fcm maxWatchBoneDelta=%.1fcm torsoBoundsR=%.0f viewDist=%.0f viewFOV=%.1f"),
		bBodyPelvis ? *BodyPelvis.ToCompactString() : TEXT("(missing)"),
		bTorsoPelvis ? *TorsoPelvis.ToCompactString() : TEXT("(missing)"),
		PelvisDeltaCm,
		SpineDeltaCm,
		MaxWatchBoneDeltaCm,
		TorsoBoundsRadius,
		ViewDistance,
		ViewFOV);

	if (PelvisDeltaCm >= ShirtExplosionPelvisDeltaThresholdCm)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("ShirtDiag EXPLOSION_SUSPECT: Torso pelvis %.1fcm from Body (threshold %.0f) — CopyPose likely stale or sim woke with bad input."),
			PelvisDeltaCm,
			ShirtExplosionPelvisDeltaThresholdCm);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMetaHumanBodyTicksForClothingPostProcess()
{
	if (!ShouldManageMetaHumanGarmentsAtRuntime())
	{
		return;
	}

	if (!UsesMetaHumanNativeClothingPipeline())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (!IsValid(Body) || !HasRenderableSkeletalMeshAsset(Body))
	{
		return;
	}

	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->bEnableUpdateRateOptimizations = false;
	Body->SetHiddenInGame(true, true);
	Body->SetVisibility(true, true);

#if WITH_EDITOR
	Body->SetUpdateAnimationInEditor(true);
#endif

	Body->TickAnimation(0.f, false);
	ForceSkeletalMeshPoseRefresh(Body);

	EnsureMetaHumanFaceVisible();
	ApplyMetaHumanClothingTickPrerequisites(Body);

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Follower->bEnableUpdateRateOptimizations = false;
		Follower->SetHiddenInGame(false, true);

#if WITH_EDITOR
		Follower->SetUpdateAnimationInEditor(true);
#endif

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			// MetaHuman owns leader pose + CopyPoseFromMesh graph pins — do not InitAnim or clear leader here.
		}
		else if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
			Follower->UpdateFollowerComponent();
		}
	}

	Body->RefreshFollowerComponents();

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: MetaHuman hidden Body set to AlwaysTickPoseAndRefreshBones (feeds garment post-process on zoom/LOD)."));
}

void UGodfreyPerformerAnimationBridgeComponent::MaintainMetaHumanBodyTickForClothing()
{
	if (!ShouldManageMetaHumanGarmentsAtRuntime() || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (Body->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
	{
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Body->bEnableUpdateRateOptimizations = false;
	}

#if WITH_EDITOR
	Body->SetUpdateAnimationInEditor(true);
#endif

	Body->RefreshBoneTransforms();

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		if (Follower->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Follower->bEnableUpdateRateOptimizations = false;
		}

#if WITH_EDITOR
		Follower->SetUpdateAnimationInEditor(true);
#endif

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			// Preserve MetaHuman post-process CopyPoseFromMesh initialization — tick only.
		}
		else if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
			Follower->UpdateFollowerComponent();
		}
	}

	Body->RefreshFollowerComponents();
}

void UGodfreyPerformerAnimationBridgeComponent::DeferredClothingStabilize()
{
	if (!ShouldManageClothingLeaderPose())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	if (bDebugHideClothingMeshesAtBeginPlay || bDebugForceBodyMeshVisibleAtBeginPlay)
	{
		ApplyBodyMotionDebugVisibility(bDebugHideClothingMeshesAtBeginPlay, bDebugForceBodyMeshVisibleAtBeginPlay);
	}

	const int32 Wired = WireClothingMeshesToBodyLeaderPose();
	InitAssignedBodyAnimClassOnly();
	StabilizeClothingLeaderPoseMeshes();
	RefreshClothingPoseAfterStabilize();
	MaintainClothingLeaderPose();
	LogSkeletalMeshPropagationReport();

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: deferred clothing stabilize complete (%d follower(s); overrides MetaHuman OnlyTickPoseWhenRendered)."),
		Wired);
}

void UGodfreyPerformerAnimationBridgeComponent::RefreshClothingPoseAfterStabilize()
{
	if (!IsValid(TargetSkeletalMesh))
	{
		return;
	}

	TargetSkeletalMesh->TickAnimation(0.f, false);
	TargetSkeletalMesh->RefreshBoneTransforms();
	TargetSkeletalMesh->RefreshFollowerComponents();

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		if (USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName))
		{
			Follower->TickAnimation(0.f, false);
			Follower->RefreshBoneTransforms();
		}
	}
}

void UGodfreyPerformerAnimationBridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	LoopedBodySlotMontages.Empty();
	ActiveSpeakingIdlePlayMontage = nullptr;
	SpeakingIdleMontageCycleSeconds = 0.f;
	SpeakingIdleMontageWallCycleSeconds = 0.f;
	SpeakingIdleCycleStartWorldTime = -1.0;

	if (AActor* const Owner = GetOwner())
	{
		CachedExhibitYawDegrees = Owner->GetActorRotation().Yaw;
		bHasCachedExhibitYaw = true;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	bMetaHumanClothingRefreshPassesScheduled = false;
	bEditorCopyPoseStabilizeScheduled = false;
	MetaHumanGarmentRegisterWaitFrames = 0;
	MetaHumanClothingRefreshPassCount = 0;
	bLoggedCopyPoseWireForTorso = false;
	LastLoggedBodyTickOpt = -1;
	LastLoggedTorsoTickOpt = -1;
	LastLoggedBodyVisible = -1;
	LastLoggedTorsoVisible = -1;
	LastLoggedBodyHiddenInGame = -1;
	LastEditorViewDistanceToTorso = -1.f;
	LastLoggedPelvisDeltaCm = -1.f;

	if (UsesMetaHumanNativeClothingPipeline())
	{
		if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
		{
			ResolveTargetBodyMesh();
		}

		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: MetaHuman on '%s' — montage bridge (copyPoseGarment=%d bManageMetaHumanGarmentsAtRuntime=%d)."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
			HasMetaHumanGarmentPostProcessMesh() ? 1 : 0,
			bManageMetaHumanGarmentsAtRuntime ? 1 : 0);

		if (ShouldManageMetaHumanGarmentsAtRuntime())
		{
			// MetaHumanComponentUE::BeginPlay resets garment tick — run after it on subsequent frames.
			ScheduleMetaHumanClothingRefreshPasses();
		}

		LogSkeletalMeshPropagationReport();
	}
	else
	{
		LogSkeletalMeshPropagationReport();
	}

	MaybeLogMetaHumanShirtDiagnostics(TEXT("BeginPlay"), true);

	if (ShouldManageClothingLeaderPose())
	{
		if (UWorld* World = GetWorld())
		{
			// MetaHumanComponentUE::BeginPlay sets clothing meshes to OnlyTickPoseWhenRendered — defer until after it runs.
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::DeferredClothingStabilize));
		}
	}

	if (bAutoAssignPlaceholderMontages)
	{
		TryAssignPlaceholderMontages();
	}

	TryBindPerformerState();
	LogMontageSetupStatus();
	UpdatePerformerTickEnabled();
}

bool UGodfreyPerformerAnimationBridgeComponent::ShouldAutoResolveBodyMesh() const
{
	if (!IsValid(TargetSkeletalMesh))
	{
		return true;
	}

	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		return true;
	}

	return !TargetSkeletalMesh->GetName().Equals(BodyMeshNameHint, ESearchCase::IgnoreCase);
}

bool UGodfreyPerformerAnimationBridgeComponent::ResolveTargetBodyMesh()
{
	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	const FString BodyHint = BodyMeshNameHint;
	const FString FaceExclude = FaceMeshNameExclude;

	TArray<USkeletalMeshComponent*> Meshes;
	Owner->GetComponents<USkeletalMeshComponent>(Meshes);

	USkeletalMeshComponent* Best = nullptr;
	int32 BestScore = -1;

	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || IsExcludedFaceMesh(Mesh, FaceExclude))
		{
			continue;
		}

		const int32 Score = ScoreBodyMeshCandidate(Mesh, BodyHint);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Mesh;
		}
	}

	if (!Best)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: ResolveTargetBodyMesh found no usable body mesh on '%s'."),
			*Owner->GetName());
		return false;
	}

	const bool bChanged = TargetSkeletalMesh != Best;
	TargetSkeletalMesh = Best;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: %s TargetSkeletalMesh -> '%s' (score=%d, skel=%s)."),
		bChanged ? TEXT("resolved") : TEXT("confirmed"),
		*Best->GetName(),
		BestScore,
		*Best->GetSkeletalMeshAsset()->GetName());
	return true;
}

bool UGodfreyPerformerAnimationBridgeComponent::HasClothingFollowerMeshesOnBody() const
{
	if (!bAutoWireClothingLeaderPoseToBody || !IsValid(TargetSkeletalMesh))
	{
		return false;
	}

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || Follower == TargetSkeletalMesh)
		{
			continue;
		}

		if (!HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		return true;
	}

	return false;
}

void UGodfreyPerformerAnimationBridgeComponent::InitAssignedBodyAnimClassOnly()
{
	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		return;
	}

	if (HasClothingFollowerMeshesOnBody() && !UsesMetaHumanNativeClothingPipeline())
	{
		// Non-MetaHuman performers only: keep Body in bind pose for leader-follower clothing.
		// MetaHuman actors must keep their assigned Body AnimBP (Live Link / retarget).
		TargetSkeletalMesh->SetAnimInstanceClass(nullptr);
		TargetSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		TargetSkeletalMesh->InitAnim(true);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: BeginPlay keeps '%s' in bind/reference pose (no AnimInstance) — clothing leader pose + post-process."),
			*TargetSkeletalMesh->GetName());
		return;
	}

	const TSubclassOf<UAnimInstance> ExistingAnimClass = TargetSkeletalMesh->GetAnimClass();
	if (TargetSkeletalMesh->GetAnimInstance())
	{
		return;
	}

	if (!ExistingAnimClass || ExistingAnimClass == UAnimSingleNodeInstance::StaticClass())
	{
		return;
	}

	TargetSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	TargetSkeletalMesh->InitAnim(true);

	if (TargetSkeletalMesh->GetAnimInstance())
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: initialized assigned AnimClass '%s' on '%s'."),
			*ExistingAnimClass->GetName(),
			*TargetSkeletalMesh->GetName());
	}
}

void UGodfreyPerformerAnimationBridgeComponent::StabilizeClothingLeaderPoseMeshes()
{
	if (UsesMetaHumanNativeClothingPipeline() || !HasClothingFollowerMeshesOnBody() || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	auto ConfigureMeshAnimTickStability = [](USkeletalMeshComponent* Mesh)
	{
		if (!IsValid(Mesh))
		{
			return;
		}

		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
		Mesh->SetDisablePostProcessBlueprint(false);

#if WITH_EDITOR
		Mesh->SetUpdateAnimationInEditor(true);
#endif
	};

	ConfigureMeshAnimTickStability(TargetSkeletalMesh);
	// Hidden in game for the player, but keep the component visible so the leader keeps ticking.
	TargetSkeletalMesh->SetHiddenInGame(true, true);
	TargetSkeletalMesh->SetVisibility(true, true);

	UClass* const ClothingPostProcessClass = LoadClothingPostProcessAnimClass();

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		const EVisibilityBasedAnimTickOption PreviousTick = Follower->VisibilityBasedAnimTickOption;
		ConfigureMeshAnimTickStability(Follower);

		if (PreviousTick != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: '%s' anim tick %d -> AlwaysTickPoseAndRefreshBones (MetaHuman override)."),
				*Follower->GetName(),
				static_cast<int32>(PreviousTick));
		}

		if (ClothingPostProcessClass)
		{
			Follower->SetOverridePostProcessAnimBP(ClothingPostProcessClass, true);
			Follower->RefreshBoneTransforms();
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: clothing post-process AnimBP on '%s'."),
				*Follower->GetName());
		}
	}

	TargetSkeletalMesh->RefreshBoneTransforms();
	TargetSkeletalMesh->RefreshFollowerComponents();
	TargetSkeletalMesh->MarkRenderTransformDirty();
	TargetSkeletalMesh->MarkRenderStateDirty();

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: clothing leader pose stabilized (always tick bones, URO off, Body='%s')."),
		*TargetSkeletalMesh->GetName());
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMontageAnimInstanceReady()
{
	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		return;
	}

	if (TargetSkeletalMesh->GetAnimInstance())
	{
		return;
	}

	const TSubclassOf<UAnimInstance> ExistingAnimClass = TargetSkeletalMesh->GetAnimClass();
	if (ExistingAnimClass && ExistingAnimClass != UAnimSingleNodeInstance::StaticClass())
	{
		TargetSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		TargetSkeletalMesh->InitAnim(true);

		if (TargetSkeletalMesh->GetAnimInstance())
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: initialized assigned AnimClass '%s' on '%s'."),
				*ExistingAnimClass->GetName(),
				*TargetSkeletalMesh->GetName());
		}
		return;
	}

	const TSubclassOf<UAnimInstance> BootstrapClass =
		(!UsesMetaHumanNativeClothingPipeline() && HasClothingFollowerMeshesOnBody())
		? UGodfreyBodyAnimInstance::StaticClass()
		: UAnimSingleNodeInstance::StaticClass();

	TargetSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	TargetSkeletalMesh->SetAnimInstanceClass(BootstrapClass);
	TargetSkeletalMesh->InitAnim(true);

	if (BootstrapClass == UGodfreyBodyAnimInstance::StaticClass())
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: bootstrapped GodfreyBodyAnimInstance on '%s' for montage playback (ref pose when idle; safe for clothing leader pose)."),
			*TargetSkeletalMesh->GetName());
	}
	else
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: bootstrapped AnimSingleNodeInstance on '%s' (assign UGodfreyBodyAnimInstance on Body for upper-body montages)."),
			*TargetSkeletalMesh->GetName());
	}
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::MakeOrGetPlaceholderMontage(UAnimSequence* Sequence,
	const TCHAR* Label, const int32 LoopCount)
{
	if (!Sequence)
	{
		return nullptr;
	}

	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
		Sequence,
		PlaceholderMontageSlotName,
		0.2f,
		0.2f,
		1.f,
		FMath::Max(1, LoopCount));
	if (!Montage)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: failed to create placeholder montage from '%s' for %s."),
			*Sequence->GetName(), Label);
		return nullptr;
	}

	GeneratedPlaceholderMontages.Add(Montage);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: placeholder montage for %s from sequence '%s' (slot=%s, len=%.2fs, loopCount=%d)."),
		Label, *Sequence->GetName(), *PlaceholderMontageSlotName.ToString(), Montage->GetPlayLength(), LoopCount);
	return Montage;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolveLoopedBodySlotMontage(UAnimMontage* Montage,
	const TCHAR* ContextLabel)
{
	if (!Montage)
	{
		return nullptr;
	}

	if (TObjectPtr<UAnimMontage>* Cached = LoopedBodySlotMontages.Find(Montage))
	{
		if (*Cached)
		{
			return Cached->Get();
		}
	}

	UAnimMontage* const BaseMontage = ResolveMontageForBodySlot(Montage, ContextLabel);
	if (!BaseMontage)
	{
		return nullptr;
	}

	const FName BodySlot = UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName;
	if (MontageHasPlayableSlotTrack(BaseMontage, BodySlot))
	{
		LoopedBodySlotMontages.Add(Montage, BaseMontage);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: using source montage '%s' for speaking loop (DefaultSlot OK, len=%.2fs)."),
			ContextLabel,
			*BaseMontage->GetName(),
			BaseMontage->GetPlayLength());
		return BaseMontage;
	}

	UAnimSequence* SourceSequence = ExtractPrimarySequenceFromMontage(BaseMontage);
	if (!SourceSequence)
	{
		SourceSequence = ExtractPrimarySequenceFromMontage(Montage);
	}
	if (!SourceSequence)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: cannot build looped montage for '%s' — no AnimSequence; using one-shot."),
			ContextLabel, *Montage->GetName());
		return BaseMontage;
	}

	UAnimMontage* const LoopedMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
		SourceSequence,
		BodySlot,
		0.2f,
		0.2f,
		1.f,
		GodfreySpeakingIdleSegmentLoopCount);
	if (!LoopedMontage)
	{
		return BaseMontage;
	}

	GeneratedPlaceholderMontages.Add(LoopedMontage);
	LoopedBodySlotMontages.Add(Montage, LoopedMontage);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [%s]: dynamic speaking montage '%s' from sequence '%s' (len=%.2fs)."),
		ContextLabel,
		*LoopedMontage->GetName(),
		*SourceSequence->GetName(),
		LoopedMontage->GetPlayLength());
	return LoopedMontage;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolveMontageForBodySlot(UAnimMontage* Montage,
	const TCHAR* ContextLabel)
{
	if (!Montage)
	{
		return nullptr;
	}

	const FName BodySlot = UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName;
	const FString SlotSummary = DescribeMontageSlotTracks(Montage);

	if (MontageHasPlayableSlotTrack(Montage, BodySlot))
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: montage '%s' slot tracks [%s] — DefaultSlot OK."),
			ContextLabel, *Montage->GetName(), *SlotSummary);
		return Montage;
	}

	if (!bAutoRemapMontagesToBodySlot)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: montage '%s' slot tracks [%s] — missing DefaultSlot; auto-remap disabled."),
			ContextLabel, *Montage->GetName(), *SlotSummary);
		return Montage;
	}

	if (TObjectPtr<UAnimMontage>* Cached = BodySlotRemappedMontages.Find(Montage))
	{
		if (*Cached)
		{
			return Cached->Get();
		}
	}

	UAnimSequence* SourceSequence = ExtractPrimarySequenceFromMontage(Montage);
	if (!SourceSequence)
	{
		SourceSequence = LoadObject<UAnimSequence>(nullptr,
			TEXT("/Game/Godfrey/Animation/Retargeted/As_Godfrey_Talking_Anim.As_Godfrey_Talking_Anim"));
	}

	if (!SourceSequence)
	{
		UE_LOG(LogGodfreyPerformance, Error,
			TEXT("GodfreyPerformerBridge [%s]: montage '%s' has slot tracks [%s] but no DefaultSlot and no AnimSequence to rebuild. Open montage → add DefaultSlot track with the retargeted sequence."),
			ContextLabel, *Montage->GetName(), *SlotSummary);
		return Montage;
	}

	UAnimMontage* const Remapped = MakeOrGetPlaceholderMontage(SourceSequence, ContextLabel);
	if (Remapped)
	{
		BodySlotRemappedMontages.Add(Montage, Remapped);
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: rebuilt '%s' -> dynamic montage '%s' on DefaultSlot from sequence '%s' (original slots: [%s])."),
			ContextLabel,
			*Montage->GetName(),
			*Remapped->GetName(),
			*SourceSequence->GetName(),
			*SlotSummary);
	}
	return Remapped ? Remapped : Montage;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolvePlaceholderMontageAsset()
{
	if (PlaceholderMontageOverride)
	{
		return PlaceholderMontageOverride;
	}

	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		return nullptr;
	}

	USkeleton* const BodySkeleton = TargetSkeletalMesh->GetSkeletalMeshAsset()->GetSkeleton();
	if (!BodySkeleton)
	{
		return nullptr;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.SearchAllAssets(true);

	auto SkeletonMatches = [BodySkeleton](const USkeleton* Candidate) -> bool
	{
		if (!Candidate || !BodySkeleton)
		{
			return false;
		}
#if WITH_EDITOR
		return Candidate == BodySkeleton || BodySkeleton->IsCompatibleForEditor(Candidate);
#else
		return Candidate == BodySkeleton;
#endif
	};

	auto TryAssetsUnderPath = [&](const FName& Path, UAnimMontage*& OutBestMontage, UAnimSequence*& OutBestSequence,
		int32& OutBestMontageScore, int32& OutBestSequenceScore) -> void
	{
		TArray<FAssetData> PathAssets;
		AssetRegistry.GetAssetsByPath(Path, PathAssets, true);
		for (const FAssetData& Data : PathAssets)
		{
			if (Data.AssetClassPath == UAnimMontage::StaticClass()->GetClassPathName())
			{
				if (UAnimMontage* Montage = Cast<UAnimMontage>(Data.GetAsset()))
				{
					if (!SkeletonMatches(Montage->GetSkeleton()))
					{
						continue;
					}
					const int32 Score = bPreferObviousPlaceholderAnimations
						? ScoreAnimAssetNameForObviousTest(Montage->GetName())
						: 0;
					if (!OutBestMontage || Score > OutBestMontageScore)
					{
						OutBestMontage = Montage;
						OutBestMontageScore = Score;
					}
				}
			}
		}
		for (const FAssetData& Data : PathAssets)
		{
			if (Data.AssetClassPath == UAnimSequence::StaticClass()->GetClassPathName())
			{
				if (UAnimSequence* Sequence = Cast<UAnimSequence>(Data.GetAsset()))
				{
					if (!SkeletonMatches(Sequence->GetSkeleton()))
					{
						continue;
					}
					const int32 Score = bPreferObviousPlaceholderAnimations
						? ScoreAnimAssetNameForObviousTest(Sequence->GetName())
						: 0;
					if (!OutBestSequence || Score > OutBestSequenceScore)
					{
						OutBestSequence = Sequence;
						OutBestSequenceScore = Score;
					}
				}
			}
		}
	};

	UAnimMontage* BestMontage = nullptr;
	UAnimSequence* BestSequence = nullptr;
	int32 BestMontageScore = MIN_int32;
	int32 BestSequenceScore = MIN_int32;

	const TArray<FName> SearchPaths = {
		PlaceholderMontageSearchPath,
		FName(TEXT("/Game/Characters")),
		FName(TEXT("/Game/Animation")),
	};

	for (const FName& Path : SearchPaths)
	{
		TryAssetsUnderPath(Path, BestMontage, BestSequence, BestMontageScore, BestSequenceScore);
	}

	if (BestMontage)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: discovered placeholder AnimMontage '%s' (obviousScore=%d)."),
			*BestMontage->GetName(), BestMontageScore);
		return BestMontage;
	}

	if (BestSequence)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: discovered placeholder AnimSequence '%s' (obviousScore=%d)."),
			*BestSequence->GetName(), BestSequenceScore);
		return MakeOrGetPlaceholderMontage(BestSequence, TEXT("discovered sequence"));
	}

	UE_LOG(LogGodfreyPerformance, Warning,
		TEXT("GodfreyPerformerBridge: no compatible placeholder montage/sequence for skeleton '%s'."),
		*BodySkeleton->GetName());
	return nullptr;
}

int32 UGodfreyPerformerAnimationBridgeComponent::ScoreAnimAssetNameForObviousTest(const FString& AssetName) const
{
	const FString Lower = AssetName.ToLower();
	int32 Score = 0;

	auto Boost = [&](const TCHAR* Token, const int32 Points)
	{
		if (Lower.Contains(Token))
		{
			Score += Points;
		}
	};

	Boost(TEXT("wave"), 120);
	Boost(TEXT("greet"), 100);
	Boost(TEXT("gesture"), 90);
	Boost(TEXT("talk"), 80);
	Boost(TEXT("point"), 80);
	Boost(TEXT("punch"), 75);
	Boost(TEXT("hello"), 70);
	Boost(TEXT("walk"), 40);
	Boost(TEXT("idle"), 10);

	Boost(TEXT("calf"), -120);
	Boost(TEXT("ankle"), -100);
	Boost(TEXT("toe"), -90);
	Boost(TEXT("foot"), -80);
	Boost(TEXT("heel"), -70);

	return Score;
}

USkeletalMeshComponent* UGodfreyPerformerAnimationBridgeComponent::FindFollowerMeshByComponentName(const FName MeshName) const
{
	AActor* const Owner = GetOwner();
	if (!Owner || MeshName.IsNone())
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> Meshes;
	Owner->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (IsValid(Mesh) && Mesh->GetName().Equals(MeshName.ToString(), ESearchCase::IgnoreCase))
		{
			return Mesh;
		}
	}
	return nullptr;
}

void UGodfreyPerformerAnimationBridgeComponent::LogSkeletalMeshPropagationReport() const
{
	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge mesh propagation report for '%s':"), *Owner->GetName());

	TArray<USkeletalMeshComponent*> Meshes;
	Owner->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh))
		{
			continue;
		}

		const USkeletalMesh* SkelAsset = Mesh->GetSkeletalMeshAsset();
		const USkinnedMeshComponent* Leader = Mesh->LeaderPoseComponent.Get();
		const FString LeaderName = Leader ? Leader->GetName() : TEXT("(none)");
		const FString SkelName = SkelAsset ? SkelAsset->GetName() : TEXT("(none)");
		const UClass* AnimClass = Mesh->GetAnimClass();
		const FString AnimClassName = AnimClass ? AnimClass->GetName() : TEXT("(none)");
		const UAnimInstance* AnimInst = Mesh->GetAnimInstance();
		const FString AnimInstName = AnimInst ? AnimInst->GetClass()->GetName() : TEXT("(none)");
		const UAnimInstance* PostProcessInst = Mesh->GetPostProcessInstance();
		const FString PostProcessInstName =
			PostProcessInst ? PostProcessInst->GetClass()->GetName() : TEXT("(none)");
		const USceneComponent* AttachParent = Mesh->GetAttachParent();
		const FString AttachParentName = AttachParent ? AttachParent->GetName() : TEXT("(none)");

		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("  mesh='%s' visible=%d hiddenInGame=%d tickOpt=%d skel='%s' animClass='%s' animInst='%s' postProcess='%s' leader='%s' attach='%s'%s"),
			*Mesh->GetName(),
			Mesh->IsVisible(),
			Mesh->bHiddenInGame,
			static_cast<int32>(Mesh->VisibilityBasedAnimTickOption),
			*SkelName,
			*AnimClassName,
			*AnimInstName,
			*PostProcessInstName,
			*LeaderName,
			*AttachParentName,
			(Mesh == TargetSkeletalMesh.Get()) ? TEXT(" [montage target]") : TEXT(""));
	}
}

int32 UGodfreyPerformerAnimationBridgeComponent::WireClothingMeshesToBodyLeaderPose()
{
	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge: cannot wire leader pose — Body target mesh missing."));
		return 0;
	}

	int32 WiredCount = 0;
	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || Follower == TargetSkeletalMesh)
		{
			continue;
		}

		if (!HasRenderableSkeletalMeshAsset(Follower))
		{
			UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformerBridge: skip leader pose for '%s' (no skeletal mesh asset)."),
				*Follower->GetName());
			continue;
		}

		const USkinnedMeshComponent* ExistingLeader = Follower->LeaderPoseComponent.Get();
		if (ExistingLeader == TargetSkeletalMesh)
		{
			UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: '%s' already follows Body."), *Follower->GetName());
			++WiredCount;
			continue;
		}

		Follower->SetLeaderPoseComponent(TargetSkeletalMesh, true, true);
		++WiredCount;
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: wired leader pose '%s' -> Body."), *Follower->GetName());
	}

	return WiredCount;
}

void UGodfreyPerformerAnimationBridgeComponent::ApplyBodyMotionDebugVisibility(const bool bHideClothingMeshes,
	const bool bForceBodyMeshVisible)
{
	if (bHideClothingMeshes)
	{
		for (const FName FollowerName : ClothingFollowerMeshNames)
		{
			if (USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName))
			{
				Follower->SetHiddenInGame(true, true);
				UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge debug: hid clothing mesh '%s'."), *Follower->GetName());
			}
		}
	}

	if (bForceBodyMeshVisible && IsValid(TargetSkeletalMesh))
	{
		TargetSkeletalMesh->SetHiddenInGame(false, true);
		TargetSkeletalMesh->SetVisibility(true, true);
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge debug: forced Body mesh visible."));
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::TryAssignPlaceholderMontages()
{
	UAnimMontage* const Placeholder = ResolvePlaceholderMontageAsset();
	if (!Placeholder)
	{
		return false;
	}

	auto AssignIfEmpty = [Placeholder](TObjectPtr<UAnimMontage>& Slot, const TCHAR* Label) -> bool
	{
		if (Slot)
		{
			return false;
		}
		Slot = Placeholder;
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: assigned placeholder to %s."), Label);
		return true;
	};

	bool bAny = false;
	bAny |= AssignIfEmpty(SpeakingStartMontage, TEXT("SpeakingStartMontage"));
	bAny |= AssignIfEmpty(SpeakingIdleMontage, TEXT("SpeakingIdleMontage"));
	bAny |= AssignIfEmpty(ReturnToIdleMontage, TEXT("ReturnToIdleMontage"));
	bAny |= AssignIfEmpty(ThinkingMontage, TEXT("ThinkingMontage"));
	bAny |= AssignIfEmpty(ListeningEnterMontage, TEXT("ListeningEnterMontage"));
	return bAny;
}

void UGodfreyPerformerAnimationBridgeComponent::LogMontageSetupStatus() const
{
	auto LogSlot = [](const TCHAR* Label, const UAnimMontage* Montage)
	{
		if (Montage)
		{
			UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge setup: %s = '%s'"), Label, *Montage->GetName());
		}
		else
		{
			UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge setup: %s is NOT assigned (assign a body montage in Details)."), Label);
		}
	};

	if (IsValid(TargetSkeletalMesh))
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge setup: TargetSkeletalMesh = '%s'"),
			*TargetSkeletalMesh->GetName());
	}
	else
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge setup: TargetSkeletalMesh is NOT set."));
	}

	LogSlot(TEXT("SpeakingStartMontage"), SpeakingStartMontage);
	LogSlot(TEXT("SpeakingIdleMontage"), SpeakingIdleMontage);
	LogSlot(TEXT("ReturnToIdleMontage"), ReturnToIdleMontage);
	LogSlot(TEXT("ThinkingMontage"), ThinkingMontage);
	LogSlot(TEXT("ListeningEnterMontage"), ListeningEnterMontage);
}

#if WITH_EDITOR
void UGodfreyPerformerAnimationBridgeComponent::EnsureEditorShirtDiagnosticTicker()
{
	if (!bLogMetaHumanShirtDiagnostics)
	{
		return;
	}

	if (EditorShirtDiagnosticTickerHandle.IsValid())
	{
		return;
	}

	EditorShirtDiagnosticTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::EditorShirtDiagnosticTickerPoll),
		0.05f);
}

void UGodfreyPerformerAnimationBridgeComponent::RemoveEditorShirtDiagnosticTicker()
{
	if (EditorShirtDiagnosticTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorShirtDiagnosticTickerHandle);
		EditorShirtDiagnosticTickerHandle.Reset();
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::EditorShirtDiagnosticTickerPoll(float /*DeltaTime*/)
{
	if (!IsValid(this) || !bLogMetaHumanShirtDiagnostics)
	{
		RemoveEditorShirtDiagnosticTicker();
		return false;
	}

	const UWorld* const World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		return true;
	}

	if (HasMetaHumanGarmentPostProcessMesh())
	{
		if (ShouldManageMetaHumanGarmentsAtRuntime())
		{
			MaintainMetaHumanCopyPoseBodySource();
		}
		MaybeLogMetaHumanShirtDiagnostics(TEXT("EditorTicker"));
	}
	else
	{
		MaybeLogMetaHumanShirtDiagnostics(TEXT("EditorTicker"));
	}

	return true;
}
#endif

void UGodfreyPerformerAnimationBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if WITH_EDITOR
	RemoveEditorShirtDiagnosticTicker();
#endif
	UnbindPerformerState();
	StopIdleBreathingMontageIfActive();
	bMetaHumanClothingRefreshPassesScheduled = false;
	bEditorCopyPoseStabilizeScheduled = false;
	MetaHumanGarmentRegisterWaitFrames = 0;
	Super::EndPlay(EndPlayReason);
}

void UGodfreyPerformerAnimationBridgeComponent::RefreshBehaviourTick()
{
	UpdatePerformerTickEnabled();
}

void UGodfreyPerformerAnimationBridgeComponent::SetCurrentAttentionTarget(AActor* NewTarget)
{
	AActor* const Old = CurrentAttentionTarget.Get();
	if (Old != NewTarget)
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: attention target %s -> %s"),
			Old ? *Old->GetName() : TEXT("(none)"), NewTarget ? *NewTarget->GetName() : TEXT("(none)"));
	}
	CurrentAttentionTarget = NewTarget;
}

void UGodfreyPerformerAnimationBridgeComponent::UpdatePerformerTickEnabled()
{
	const bool bEditorCopyPosePreview =
		ShouldManageMetaHumanGarmentsAtRuntime() && IsEditorViewportWorld() && HasMetaHumanGarmentPostProcessMesh();
	const bool bWantTick =
		bEnableIdleMicroMotion || bEnableAttentionTargetFollow || (bLoopSpeakingIdleMontage && bIsSpeaking)
		|| ShouldManageMetaHumanGarmentsAtRuntime() || bEditorCopyPosePreview;
	SetComponentTickEnabled(bWantTick);
	if (bWantTick)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: behaviour tick enabled (IdleMicro=%d Attention=%d editorCopyPose=%d)."),
			bEnableIdleMicroMotion, bEnableAttentionTargetFollow, bEditorCopyPosePreview ? 1 : 0);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bEnableIdleMicroMotion)
	{
		IdleMicroTimeSeconds += DeltaTime;
		const float BreathSpeed = 1.15f * FMath::Max(0.15f, IdleBreathingIntensity);
		const float SwaySpeed = 0.52f * FMath::Max(0.15f, IdleBreathingIntensity);
		IdleBreathingWave = FMath::Sin(IdleMicroTimeSeconds * BreathSpeed);
		IdlePostureSwayWave = FMath::Sin(IdleMicroTimeSeconds * SwaySpeed + 1.1f) * 0.65f;
	}

	if (bEnableAttentionTargetFollow)
	{
		UpdateAttentionRotation(DeltaTime);
	}

	if (bIsSpeaking && bLoopSpeakingIdleMontage)
	{
		MaintainSpeakingIdleMontage();
	}

	if (ShouldManageMetaHumanGarmentsAtRuntime() && IsEditorViewportWorld() && HasMetaHumanGarmentPostProcessMesh())
	{
		MaintainMetaHumanCopyPoseBodySource();
		return;
	}

	if (ShouldManageClothingLeaderPose())
	{
		MaintainClothingLeaderPose();
	}
	else if (ShouldManageMetaHumanGarmentsAtRuntime())
	{
		MaintainMetaHumanBodyTickForClothing();
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::IsBodyMontagePlaying() const
{
	if (!IsValid(TargetSkeletalMesh))
	{
		return false;
	}

	if (UAnimInstance* AnimInst = TargetSkeletalMesh->GetAnimInstance())
	{
		return AnimInst->IsAnyMontagePlaying();
	}

	return false;
}

void UGodfreyPerformerAnimationBridgeComponent::MaintainClothingLeaderPose()
{
	if (UsesMetaHumanNativeClothingPipeline() || !HasClothingFollowerMeshesOnBody() || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->bEnableUpdateRateOptimizations = false;
	Body->SetHiddenInGame(true, true);
	Body->SetVisibility(true, true);

#if WITH_EDITOR
	Body->SetUpdateAnimationInEditor(true);
#endif

	if (!Body->GetAnimInstance() && !IsBodyMontagePlaying())
	{
		const bool bWasForceRefPose = Body->bForceRefpose;
		Body->bForceRefpose = true;
		Body->RefreshBoneTransforms();
		Body->bForceRefpose = bWasForceRefPose;
	}
	else
	{
		Body->RefreshBoneTransforms();
	}

	UClass* const ClothingPostProcessClass = LoadClothingPostProcessAnimClass();
	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
		}

		if (Follower->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}

		Follower->bEnableUpdateRateOptimizations = false;
		Follower->SetDisablePostProcessBlueprint(false);

#if WITH_EDITOR
		Follower->SetUpdateAnimationInEditor(true);
#endif

		if (ClothingPostProcessClass
			&& Follower->GetPostProcessInstance()
			&& Follower->GetPostProcessInstance()->GetClass() != ClothingPostProcessClass)
		{
			Follower->SetOverridePostProcessAnimBP(ClothingPostProcessClass, true);
		}
		else if (ClothingPostProcessClass && !Follower->GetPostProcessInstance())
		{
			Follower->SetOverridePostProcessAnimBP(ClothingPostProcessClass, true);
		}

		if (Follower->GetPostProcessInstance())
		{
			Follower->TickAnimation(0.f, false);
			Follower->RefreshBoneTransforms();
		}
		else
		{
			Follower->UpdateFollowerComponent();
		}
	}

	Body->RefreshFollowerComponents();
}

void UGodfreyPerformerAnimationBridgeComponent::UpdateAttentionRotation(const float DeltaTime)
{
	AActor* const Owner = GetOwner();
	if (!Owner || !bHasCachedExhibitYaw)
	{
		return;
	}

	const float Speed = FMath::Max(0.1f, AttentionInterpSpeed);
	FRotator Current = Owner->GetActorRotation();

	float GoalYawDegrees = CachedExhibitYawDegrees;
	if (IsValid(CurrentAttentionTarget))
	{
		FVector Delta = CurrentAttentionTarget->GetActorLocation() - Owner->GetActorLocation();
		Delta.Z = 0.f;
		if (!Delta.IsNearlyZero(1.f))
		{
			const float ToYaw = Delta.Rotation().Yaw;
			float Offset = FRotator::NormalizeAxis(ToYaw - CachedExhibitYawDegrees);
			Offset = FMath::Clamp(Offset, -AttentionOffsetStrength, AttentionOffsetStrength);
			GoalYawDegrees = CachedExhibitYawDegrees + Offset;
		}
	}

	const FRotator TargetRot(Current.Pitch, GoalYawDegrees, Current.Roll);
	const FRotator NewRot = FMath::RInterpTo(Current, TargetRot, DeltaTime, Speed);
	if (!NewRot.Equals(Current, 0.05f))
	{
		Owner->SetActorRotation(NewRot);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::TryBindPerformerState()
{
	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: cannot bind — no owner. Add this component to BP_Gavin / character actor."));
		return;
	}

	PerformerState = Owner->FindComponentByClass<UGodfreyPerformanceStateComponent>();
	if (!PerformerState)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: UGodfreyPerformanceStateComponent not found on actor '%s'. Bridge will not receive events."),
			*Owner->GetName());
		return;
	}

	PerformerState->OnListeningStarted.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleListeningStarted);
	PerformerState->OnThinkingStarted.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleThinkingStarted);
	PerformerState->OnSpeakingStarted.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingStarted);
	PerformerState->OnSpeakingEnded.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingEnded);
	PerformerState->OnReturnedToIdle.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleReturnedToIdle);
	PerformerState->OnEmphasisTriggered.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleEmphasisTriggered);
	PerformerState->OnAmusedTriggered.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleAmusedTriggered);
	PerformerState->OnSeriousTriggered.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSeriousTriggered);
	PerformerState->OnPerformanceCueReceived.AddDynamic(this,
		&UGodfreyPerformerAnimationBridgeComponent::HandlePerformanceCueReceived);

	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: bound to UGodfreyPerformanceStateComponent on '%s' (CurrentPerformanceState=%d)."),
		*Owner->GetName(), static_cast<int32>(CurrentPerformanceState));
}

void UGodfreyPerformerAnimationBridgeComponent::UnbindPerformerState()
{
	if (!PerformerState)
	{
		return;
	}

	PerformerState->OnListeningStarted.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleListeningStarted);
	PerformerState->OnThinkingStarted.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleThinkingStarted);
	PerformerState->OnSpeakingStarted.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingStarted);
	PerformerState->OnSpeakingEnded.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingEnded);
	PerformerState->OnReturnedToIdle.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleReturnedToIdle);
	PerformerState->OnEmphasisTriggered.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleEmphasisTriggered);
	PerformerState->OnAmusedTriggered.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleAmusedTriggered);
	PerformerState->OnSeriousTriggered.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSeriousTriggered);
	PerformerState->OnPerformanceCueReceived.RemoveDynamic(this,
		&UGodfreyPerformerAnimationBridgeComponent::HandlePerformanceCueReceived);

	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: unbound from performer state."));
	PerformerState = nullptr;
}

void UGodfreyPerformerAnimationBridgeComponent::RefreshMirroredPerformanceState()
{
	if (PerformerState)
	{
		CurrentPerformanceState = PerformerState->GetPerformanceState();
	}
}

UAnimInstance* UGodfreyPerformerAnimationBridgeComponent::ResolveAnimInstance(const TCHAR* ContextLabel) const
{
	if (!IsValid(TargetSkeletalMesh))
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [%s]: TargetSkeletalMesh is not set."), ContextLabel);
		return nullptr;
	}
	UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [%s]: no AnimInstance on mesh '%s' (is the mesh visible / begun play?)."),
			ContextLabel, *TargetSkeletalMesh->GetName());
		return nullptr;
	}
	return AnimInst;
}

bool UGodfreyPerformerAnimationBridgeComponent::PlayMontageIfPossible(UAnimMontage* Montage, const TCHAR* ContextLabel,
	const float PlayRate, const bool bRestartIfAlreadyPlaying, const bool bLoopMontage)
{
	if (!Montage)
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [%s]: no montage assigned; skipping play."), ContextLabel);
		return false;
	}
	EnsureMontageAnimInstanceReady();
	UAnimInstance* const AnimInst = ResolveAnimInstance(ContextLabel);
	if (!AnimInst)
	{
		return false;
	}

	UAnimMontage* const PlayMontage = bLoopMontage
		? ResolveLoopedBodySlotMontage(Montage, ContextLabel)
		: ResolveMontageForBodySlot(Montage, ContextLabel);
	if (!PlayMontage)
	{
		return false;
	}

	if (!bRestartIfAlreadyPlaying && bDeduplicateActiveMontagePlays && AnimInst->Montage_IsActive(PlayMontage))
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformerBridge [%s]: montage '%s' already active; skip restart."),
			ContextLabel, *PlayMontage->GetName());
		return false;
	}

	const float PlayLength = AnimInst->Montage_Play(PlayMontage, FMath::Max(0.05f, PlayRate));

	if (PlayLength > KINDA_SMALL_NUMBER && bLoopMontage)
	{
		ApplySpeakingMontageSectionLoop(AnimInst, PlayMontage);
		// Montage_GetPosition uses montage timeline (GetPlayLength), not Montage_Play's wall-clock return (length/playRate).
		SpeakingIdleMontageCycleSeconds = PlayMontage->GetPlayLength();
		SpeakingIdleMontageWallCycleSeconds = PlayLength;
		if (UWorld* World = GetWorld())
		{
			SpeakingIdleCycleStartWorldTime = World->GetTimeSeconds();
		}
		ActiveSpeakingIdlePlayMontage = PlayMontage;
		BindSpeakingIdleMontageEndDelegate(AnimInst, PlayMontage);
		if (PlayMontage->CompositeSections.Num() > 0)
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge [%s]: montage '%s' section '%s' set to loop (timeline=%.2fs wall=%.2fs)."),
				ContextLabel,
				*PlayMontage->GetName(),
				*PlayMontage->CompositeSections[0].SectionName.ToString(),
				SpeakingIdleMontageCycleSeconds,
				PlayLength);
		}
	}

	FName MontageSlotName = NAME_None;
	if (PlayMontage->SlotAnimTracks.Num() > 0)
	{
		MontageSlotName = PlayMontage->SlotAnimTracks[0].SlotName;
	}

	const float DefaultSlotWeight =
		AnimInst->GetSlotMontageGlobalWeight(UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName);
	const bool bSlotMatches =
		MontageSlotName.IsNone() || MontageSlotName == UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName;

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [%s]: playing montage '%s' (source='%s' rate=%.2f len=%.2fs) anim='%s' montageSlot='%s' expectedSlot='%s' slotWeight=%.2f match=%d."),
		ContextLabel,
		*PlayMontage->GetName(),
		*Montage->GetName(),
		PlayRate,
		PlayLength,
		*AnimInst->GetClass()->GetName(),
		MontageSlotName.IsNone() ? TEXT("(none)") : *MontageSlotName.ToString(),
		*UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName.ToString(),
		DefaultSlotWeight,
		bSlotMatches ? 1 : 0);

	if (PlayLength <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: Montage_Play returned 0 — skeleton/slot mismatch or empty montage (slots: [%s])."),
			ContextLabel, *DescribeMontageSlotTracks(PlayMontage));
	}
	else if (!bSlotMatches)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: montage slot '%s' != body AnimBP slot '%s'."),
			ContextLabel,
			MontageSlotName.IsNone() ? TEXT("(none)") : *MontageSlotName.ToString(),
			*UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName.ToString());
	}
	else if (DefaultSlotWeight <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: montage playing but DefaultSlot weight is ~0 — slot node may not be receiving this montage."),
			ContextLabel);
	}

	return PlayLength > KINDA_SMALL_NUMBER;
}

void UGodfreyPerformerAnimationBridgeComponent::StopIdleBreathingMontageIfActive()
{
	if (!IsValid(TargetSkeletalMesh) || !IdleBreathingMontage)
	{
		return;
	}
	UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}
	if (AnimInst->Montage_IsActive(IdleBreathingMontage))
	{
		AnimInst->Montage_Stop(SpeakingIdleMontageBlendOut, IdleBreathingMontage);
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge [IdleBreath]: stopped '%s'."),
			*IdleBreathingMontage->GetName());
	}
}

void UGodfreyPerformerAnimationBridgeComponent::TryStartIdleBreathingMontage()
{
	if (!IdleBreathingMontage)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformerBridge [IdleBreath]: no IdleBreathingMontage assigned."));
		return;
	}
	UAnimInstance* const AnimInst = ResolveAnimInstance(TEXT("IdleBreath"));
	if (!AnimInst)
	{
		return;
	}
	if (bDeduplicateActiveMontagePlays && AnimInst->Montage_IsActive(IdleBreathingMontage))
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformerBridge [IdleBreath]: '%s' already active."),
			*IdleBreathingMontage->GetName());
		return;
	}
	AnimInst->Montage_Play(IdleBreathingMontage, FMath::Max(0.05f, IdleBreathingIntensity));
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge [IdleBreath]: started '%s'."), *IdleBreathingMontage->GetName());
}

void UGodfreyPerformerAnimationBridgeComponent::PlaySpeakingIdleInternal(const bool bRestartIfAlreadyPlaying)
{
	const bool bLoop = bLoopSpeakingIdleMontage && SpeakingIdleMontage != nullptr;
	PlayMontageIfPossible(SpeakingIdleMontage, TEXT("SpeakingIdle"), SpeakingMotionIntensity, bRestartIfAlreadyPlaying, bLoop);
}

void UGodfreyPerformerAnimationBridgeComponent::BindSpeakingIdleMontageEndDelegate(UAnimInstance* AnimInst,
	UAnimMontage* PlayMontage)
{
	if (!AnimInst || !PlayMontage)
	{
		return;
	}

	SpeakingIdleMontageEndedDelegate.BindUObject(this, &UGodfreyPerformerAnimationBridgeComponent::OnSpeakingIdleMontageEnded);
	AnimInst->Montage_SetEndDelegate(SpeakingIdleMontageEndedDelegate, PlayMontage);
}

void UGodfreyPerformerAnimationBridgeComponent::OnSpeakingIdleMontageEnded(UAnimMontage* EndedMontage, const bool bInterrupted)
{
	if (bInterrupted || !bIsSpeaking || !bLoopSpeakingIdleMontage || !SpeakingIdleMontage)
	{
		return;
	}

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [SpeakingIdle]: montage '%s' ended while still speaking — restarting."),
		EndedMontage ? *EndedMontage->GetName() : TEXT("(null)"));
	PlaySpeakingIdleInternal(true);
}

void UGodfreyPerformerAnimationBridgeComponent::MaintainSpeakingIdleMontage()
{
	if (!SpeakingIdleMontage || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	UAnimMontage* PlayMontage = ActiveSpeakingIdlePlayMontage.Get();
	if (!PlayMontage)
	{
		PlayMontage = ResolveLoopedBodySlotMontage(SpeakingIdleMontage, TEXT("SpeakingMaintain"));
	}

	if (!PlayMontage)
	{
		return;
	}

	if (!AnimInst->Montage_IsActive(PlayMontage))
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [SpeakingMaintain]: speaking idle montage inactive while bIsSpeaking — restarting."));
		PlaySpeakingIdleInternal(true);
		return;
	}

	// Wall-clock restart: dynamic montages and low slot weights can leave Montage_IsActive true while
	// Montage_GetPosition stops advancing — restart on elapsed play time instead.
	if (SpeakingIdleMontageWallCycleSeconds > KINDA_SMALL_NUMBER && SpeakingIdleCycleStartWorldTime > 0.0)
	{
		const UWorld* World = GetWorld();
		if (World)
		{
			const float Elapsed = static_cast<float>(World->GetTimeSeconds() - SpeakingIdleCycleStartWorldTime);
			const float RestartAt = FMath::Max(0.35f, SpeakingIdleMontageWallCycleSeconds - 0.05f);
			if (Elapsed >= RestartAt)
			{
				UE_LOG(LogGodfreyPerformance, Log,
					TEXT("GodfreyPerformerBridge [SpeakingMaintain]: wall-clock restart after %.2fs (cycle=%.2fs) montage='%s'."),
					Elapsed, SpeakingIdleMontageWallCycleSeconds, *PlayMontage->GetName());
				SpeakingIdleCycleStartWorldTime = World->GetTimeSeconds();
				PlaySpeakingIdleInternal(true);
				return;
			}
		}
	}

	const float TimelineLength = PlayMontage->GetPlayLength();
	if (TimelineLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float Position = AnimInst->Montage_GetPosition(PlayMontage);
	const float CycleEnd = TimelineLength - 0.03f;
	if (Position < CycleEnd)
	{
		return;
	}

	AnimInst->Montage_SetPosition(PlayMontage, 0.f);
	ApplySpeakingMontageSectionLoop(AnimInst, PlayMontage);

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - LastSpeakingIdleCycleRewindLogTime > 2.0)
	{
		LastSpeakingIdleCycleRewindLogTime = Now;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [SpeakingMaintain]: rewound '%s' at pos=%.2fs (timeline=%.2fs)."),
			*PlayMontage->GetName(), Position, TimelineLength);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::PlayListeningBehaviour()
{
	PlayMontageIfPossible(ListeningEnterMontage, TEXT("Listening"), 1.f, !bDeduplicateActiveMontagePlays);
}

void UGodfreyPerformerAnimationBridgeComponent::PlayThinkingBehaviour()
{
	PlayMontageIfPossible(ThinkingMontage, TEXT("Thinking"), 1.f, !bDeduplicateActiveMontagePlays);
}

void UGodfreyPerformerAnimationBridgeComponent::PlaySpeakingStartBehaviour()
{
	PlayMontageIfPossible(SpeakingStartMontage, TEXT("SpeakingStart"), SpeakingMotionIntensity, true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlaySpeakingIdleBehaviour()
{
	PlaySpeakingIdleInternal(true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlayEmphasisBehaviour()
{
	PlayMontageIfPossible(EmphasisMontage, TEXT("Emphasis"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlayAmusedBehaviour()
{
	PlayMontageIfPossible(AmusedMontage, TEXT("Amused"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlaySeriousBehaviour()
{
	PlayMontageIfPossible(SeriousMontage, TEXT("Serious"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlayReturnToIdleBehaviour()
{
	PlayMontageIfPossible(ReturnToIdleMontage, TEXT("ReturnToIdle"), 1.f, !bDeduplicateActiveMontagePlays);
}

void UGodfreyPerformerAnimationBridgeComponent::StopSpeakingBehaviour()
{
	if (!IsValid(TargetSkeletalMesh))
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [StopSpeaking]: TargetSkeletalMesh is not set."));
		return;
	}
	UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [StopSpeaking]: no AnimInstance on mesh '%s'."),
			*TargetSkeletalMesh->GetName());
		return;
	}

	auto StopMontageIfActive = [&](UAnimMontage* Montage, const TCHAR* Label)
	{
		if (!Montage)
		{
			return;
		}
		UAnimMontage* const Resolved = ResolveMontageForBodySlot(Montage, TEXT("StopSpeaking"));
		if (Resolved && AnimInst->Montage_IsActive(Resolved))
		{
			AnimInst->Montage_Stop(SpeakingIdleMontageBlendOut, Resolved);
			UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge [StopSpeaking]: stopped %s montage '%s'."),
				Label, *Resolved->GetName());
		}
	};

	StopMontageIfActive(SpeakingIdleMontage, TEXT("idle"));
	StopMontageIfActive(SpeakingStartMontage, TEXT("start"));
	if (SpeakingIdleMontage)
	{
		if (TObjectPtr<UAnimMontage>* Looped = LoopedBodySlotMontages.Find(SpeakingIdleMontage))
		{
			StopMontageIfActive(Looped->Get(), TEXT("idle-looped"));
		}
	}

	ActiveSpeakingIdlePlayMontage = nullptr;
	SpeakingIdleMontageCycleSeconds = 0.f;
	SpeakingIdleMontageWallCycleSeconds = 0.f;
	SpeakingIdleCycleStartWorldTime = -1.0;
}

void UGodfreyPerformerAnimationBridgeComponent::HandleListeningStarted()
{
	StopIdleBreathingMontageIfActive();
	bIsListening = true;
	bIsThinking = false;
	bIsSpeaking = false;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Listening (flags updated)."));
	OnBridgeListening.Broadcast();
	PlayMontageIfPossible(ListeningEnterMontage, TEXT("Listening"), 1.f, !bDeduplicateActiveMontagePlays);
}

void UGodfreyPerformerAnimationBridgeComponent::HandleThinkingStarted()
{
	StopIdleBreathingMontageIfActive();
	bIsListening = false;
	bIsThinking = true;
	bIsSpeaking = false;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Thinking (flags updated)."));
	OnBridgeThinking.Broadcast();
	PlayMontageIfPossible(ThinkingMontage, TEXT("Thinking"), 1.f, !bDeduplicateActiveMontagePlays);
}

void UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingStarted()
{
	StopIdleBreathingMontageIfActive();
	bIsListening = false;
	bIsThinking = false;
	bIsSpeaking = true;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour SpeakingStarted (flags updated)."));
	OnBridgeSpeakingStarted.Broadcast();

	const bool bSameStartAndIdle =
		SpeakingStartMontage != nullptr && SpeakingStartMontage == SpeakingIdleMontage;
	const bool bSkipStart =
		bPreferSpeakingIdleLoopOnly || bSameStartAndIdle || SpeakingStartMontage == nullptr;

	if (!bSkipStart)
	{
		PlayMontageIfPossible(SpeakingStartMontage, TEXT("SpeakingStart"), SpeakingMotionIntensity, true, false);
	}

	PlaySpeakingIdleInternal(!bSkipStart);
	UpdatePerformerTickEnabled();
}


void UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingEnded()
{
	bIsSpeaking = false;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour SpeakingEnded."));
	OnBridgeSpeakingEnded.Broadcast();
	StopSpeakingBehaviour();
	UpdatePerformerTickEnabled();
}

void UGodfreyPerformerAnimationBridgeComponent::HandleReturnedToIdle()
{
	bIsListening = false;
	bIsThinking = false;
	bIsSpeaking = false;
	GestureIntensity = GestureIntensityDefault;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: behaviour ReturnedToIdle -> idle micro / breathing layer may start (attention flags cleared; mood flags unchanged)."));
	OnBridgeReturnedToIdle.Broadcast();
	PlayMontageIfPossible(ReturnToIdleMontage, TEXT("ReturnToIdle"), 1.f, !bDeduplicateActiveMontagePlays);
	TryStartIdleBreathingMontage();
}

void UGodfreyPerformerAnimationBridgeComponent::HandleEmphasisTriggered()
{
	UWorld* const World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bCooldownActive =
		World && (Now - LastEmphasisMontageWorldTimeSeconds) < static_cast<double>(GestureCooldownSeconds);
	if (bCooldownActive)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: emphasis cooldown skip (elapsed=%.3fs, need>=%.3fs)."),
			Now - LastEmphasisMontageWorldTimeSeconds, GestureCooldownSeconds);
		if (bFireBridgeEmphasisOnCooldownSkip)
		{
			OnBridgeEmphasis.Broadcast();
		}
		return;
	}

	if (World)
	{
		LastEmphasisMontageWorldTimeSeconds = Now;
	}

	GestureIntensity = FMath::Clamp(GestureIntensity + 0.35f, 1.f, 2.5f);
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Emphasis (GestureIntensity=%.2f)."), GestureIntensity);
	OnBridgeEmphasis.Broadcast();
	PlayMontageIfPossible(EmphasisMontage, TEXT("Emphasis"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::HandleAmusedTriggered()
{
	bIsSerious = false;
	bIsAmused = true;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Amused (mood flags updated)."));
	OnBridgeAmused.Broadcast();
	PlayMontageIfPossible(AmusedMontage, TEXT("Amused"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::HandleSeriousTriggered()
{
	bIsAmused = false;
	bIsSerious = true;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Serious (mood flags updated)."));
	OnBridgeSerious.Broadcast();
	PlayMontageIfPossible(SeriousMontage, TEXT("Serious"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::HandlePerformanceCueReceived(const FString& CueType, const FString& CueValue,
	const FString& RawCue)
{
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: cue forwarded type=\"%s\" value=\"%s\"."), *CueType, *CueValue);
	OnBridgeCueReceived.Broadcast(CueType, CueValue, RawCue);
}
