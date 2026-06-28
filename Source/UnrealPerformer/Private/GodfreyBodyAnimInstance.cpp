#include "GodfreyBodyAnimInstance.h"

#include "GodfreyPerformanceLog.h"

const FName UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName(TEXT("DefaultSlot"));

namespace
{
/** spine_01 depth 6 = upper torso + clavicles/arms; excludes head/neck/legs from montage layer. */
constexpr int32 GodfreyUpperBodyBlendDepth = 6;
/** Subtle exhibition pass — full 1.0 reads as exaggerated Mixamo gesture on MetaHuman. */
constexpr float GodfreyUpperBodyMontageBlendWeight = 0.85f;

void ConfigureUpperBodyLayer(FAnimNode_LayeredBoneBlend& LayeredBlendNode)
{
	LayeredBlendNode.BlendMode = ELayeredBoneBlendMode::BranchFilter;
	LayeredBlendNode.BlendPoses.SetNum(1);
	LayeredBlendNode.BlendWeights.SetNum(1);
	LayeredBlendNode.BlendWeights[0] = 0.f;
	LayeredBlendNode.LayerSetup.SetNum(1);

	FInputBlendPose LayerSetup;
	FBranchFilter BranchFilter;
	BranchFilter.BoneName = FName(TEXT("spine_01"));
	BranchFilter.BlendDepth = GodfreyUpperBodyBlendDepth;
	LayerSetup.BranchFilters.Add(BranchFilter);
	LayeredBlendNode.LayerSetup[0] = LayerSetup;
}
} // namespace

FGodfreyBodyAnimInstanceProxy::FGodfreyBodyAnimInstanceProxy(UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
}

void FGodfreyBodyAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
	ConstructNodes();

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyBodyAnimInstance: initialized (slot=%s, upper-body layered blend from spine_01 depth=%d weight=%.2f)."),
		*UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName.ToString(),
		GodfreyUpperBodyBlendDepth,
		GodfreyUpperBodyMontageBlendWeight);
}

void FGodfreyBodyAnimInstanceProxy::Update(float DeltaSeconds)
{
	if (UAnimInstance* const AnimInst = Cast<UAnimInstance>(GetAnimInstanceObject()))
	{
		const float SlotWeight = AnimInst->GetSlotMontageGlobalWeight(UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName);
		LayeredBlendNode.BlendWeights[0] = SlotWeight > KINDA_SMALL_NUMBER ? GodfreyUpperBodyMontageBlendWeight : 0.f;
	}

	FAnimInstanceProxy::Update(DeltaSeconds);
}

FAnimNode_Base* FGodfreyBodyAnimInstanceProxy::GetCustomRootNode()
{
	return &RootNode;
}

void FGodfreyBodyAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&RefPoseNode);
	OutNodes.Add(&SlotNode);
	OutNodes.Add(&LayeredBlendNode);
	OutNodes.Add(&RootNode);
}

void FGodfreyBodyAnimInstanceProxy::ConstructNodes()
{
	SlotNode.SlotName = UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName;
	SlotNode.Source.SetLinkNode(&RefPoseNode);

	ConfigureUpperBodyLayer(LayeredBlendNode);
	LayeredBlendNode.BasePose.SetLinkNode(&RefPoseNode);
	LayeredBlendNode.BlendPoses[0].SetLinkNode(&SlotNode);

	RootNode.Result.SetLinkNode(&LayeredBlendNode);
}

UGodfreyBodyAnimInstance::UGodfreyBodyAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FAnimInstanceProxy* UGodfreyBodyAnimInstance::CreateAnimInstanceProxy()
{
	return new FGodfreyBodyAnimInstanceProxy(this);
}

void UGodfreyBodyAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void UGodfreyBodyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (bLoggedActiveSlotWeight)
	{
		return;
	}

	const float SlotWeight = GetSlotMontageGlobalWeight(DefaultBodyMontageSlotName);
	if (SlotWeight <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	bLoggedActiveSlotWeight = true;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyBodyAnimInstance: DefaultSlot montage weight=%.2f (upper-body layer active)."),
		SlotWeight);
}
