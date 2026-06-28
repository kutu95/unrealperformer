#pragma once

#include "UnrealPerformerApi.h"
#include "Animation/AnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Root.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimNodes/AnimNode_RefPose.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "GodfreyBodyAnimInstance.generated.h"

/**
 * Native MetaHuman body anim instance for Godfrey exhibition pass.
 * Ref pose + DefaultSlot montage layered on upper body from spine_01 (ACE owns Face mesh).
 */
USTRUCT()
struct FGodfreyBodyAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

public:
	FGodfreyBodyAnimInstanceProxy() = default;
	explicit FGodfreyBodyAnimInstanceProxy(UAnimInstance* InAnimInstance);

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void Update(float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

private:
	void ConstructNodes();

	FAnimNode_RefPose RefPoseNode;
	FAnimNode_Slot SlotNode;
	FAnimNode_LayeredBoneBlend LayeredBlendNode;
	FAnimNode_Root RootNode;
};

UCLASS(BlueprintType, Blueprintable)
class UNREAL_PERFORMER_API UGodfreyBodyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UGodfreyBodyAnimInstance(const FObjectInitializer& ObjectInitializer);

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** Montage slot used by UGodfreyPerformerAnimationBridgeComponent (PlaceholderMontageSlotName). */
	static const FName DefaultBodyMontageSlotName;

private:
	bool bLoggedActiveSlotWeight = false;
};
