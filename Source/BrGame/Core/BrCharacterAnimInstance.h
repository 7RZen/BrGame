#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "BrCharacterAnimInstance.generated.h"

class UAnimationAsset;

/** Native unarmed locomotion with component-space crouch/crawl posing. */
UCLASS(Transient, NotBlueprintable)
class BRGAME_API UBrCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativePostEvaluateAnimation() override;
	/** Game thread, after completing any previous evaluation; consumed by the immediate reveal update. */
	void SnapPostureOnNextUpdate();
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
private:
	friend struct FBrCharacterAnimProxy;
	bool bSnapPosture = false;
	// Loaded once on the game thread; held for the lifetime of all worker evaluations.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimationAsset>> Assets;
};
