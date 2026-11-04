#include "BrCharacterAnimInstance.h"
#include "BrCharacter.h"
#include "BrDeveloperSettings.h"
#include "BrLifeComponent.h"
#include "BrLog.h"
#include "BrPostureVisual.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

namespace
{
	enum EBone { Pelvis, Spine, Neck, Head, ThighL, CalfL, FootL, ThighR, CalfR, FootR,
		ArmL, ElbowL, HandL, ArmR, ElbowR, HandR, BoneCount };
	const FName BoneNames[BoneCount] = { TEXT("pelvis"), TEXT("spine_01"), TEXT("neck_01"), TEXT("head"),
		TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"),
		TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r") };

	void SetCS(FCSPose<FCompactPose>& Pose, FCompactPoseBoneIndex Bone, const FTransform& Transform)
	{
		const FBoneTransform Change(Bone, Transform);
		Pose.SafeSetCSBoneTransforms(MakeArrayView(&Change, 1));
	}

	// Analytic two-link solve. Uses segment directions, not mannequin-specific bone rotation axes.
	// Rotations only: bone lengths/scale remain unchanged, targets clamp to reachable distance.
	void SolveLimb(FCSPose<FCompactPose>& Pose, FCompactPoseBoneIndex Upper, FCompactPoseBoneIndex Lower,
		FCompactPoseBoneIndex End, const FVector& Goal, const FVector& Bend)
	{
		if (Upper == INDEX_NONE || Lower == INDEX_NONE || End == INDEX_NONE) return;
		FTransform A = Pose.GetComponentSpaceTransform(Upper);
		FTransform B = Pose.GetComponentSpaceTransform(Lower);
		const FTransform C = Pose.GetComponentSpaceTransform(End);
		const FVector Origin = A.GetLocation();
		const double L1 = FVector::Distance(Origin, B.GetLocation());
		const double L2 = FVector::Distance(B.GetLocation(), C.GetLocation());
		if (FMath::Min(L1, L2) < 1) return;
		const FVector Direction = (Goal - Origin).GetSafeNormal(UE_SMALL_NUMBER, FVector(0, 0, -1));
		const double D = FMath::Clamp(FVector::Distance(Origin, Goal), FMath::Abs(L1 - L2) + 0.1, L1 + L2 - 0.1);
		const double Along = (L1 * L1 - L2 * L2 + D * D) / (2 * D);
		const double Out = FMath::Sqrt(FMath::Max(0., L1 * L1 - Along * Along));
		const FVector Pole = (Bend - Direction * FVector::DotProduct(Bend, Direction)).GetSafeNormal();
		const FVector Knee = Origin + Direction * Along + Pole * Out;
		A.SetRotation((FQuat::FindBetweenNormals((B.GetLocation() - Origin).GetSafeNormal(),
			(Knee - Origin).GetSafeNormal()) * A.GetRotation()).GetNormalized());
		SetCS(Pose, Upper, A);
		B = Pose.GetComponentSpaceTransform(Lower);
		const FVector Tip = Pose.GetComponentSpaceTransform(End).GetLocation();
		B.SetRotation((FQuat::FindBetweenNormals((Tip - B.GetLocation()).GetSafeNormal(),
			(Origin + Direction * D - B.GetLocation()).GetSafeNormal()) * B.GetRotation()).GetNormalized());
		SetCS(Pose, Lower, B);
	}
}

struct FBrCharacterAnimProxy final : FAnimInstanceProxy
{
	explicit FBrCharacterAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
	FBrPostureBlend Blend;
	FBoneReference Bones[BoneCount];
	bool bHasCoreBones = false;
	float Speed = 0;
	float Direction = 0;
	float CrouchDrop = 45;
	float CrouchForward = 0;
	float HipLean = 0;
	float TorsoLean = 0;
	float KneeOutward = 0;
	float FootSpread = 0;
	float CrawlPhase = 0;
	float MoveAlpha = 0;
	double Time = 0;
	double LastUpdateTime = 0;
	bool bFirstUpdate = true;
	struct FSample
	{
		UAnimationAsset* Asset = nullptr; // UBrCharacterAnimInstance::Assets keeps these alive.
		TArray<FBlendSampleData> Cache;
		int32 Triangle = INDEX_NONE;
	};
	FSample Samples[5];

	void SetAssets(UBrCharacterAnimInstance* Anim)
	{
		for (int32 I = 0; I < 5; ++I)
		{
			Samples[I].Asset = Anim->Assets.IsValidIndex(I) ? Anim->Assets[I].Get() : nullptr;
			if (const auto* BS = Cast<UBlendSpace>(Samples[I].Asset))
				Samples[I].Cache.Reserve(BS->GetBlendSamples().Num());
		}
	}

	virtual void CacheBones() override
	{
		FAnimInstanceProxy::CacheBones();
		bHasCoreBones = true;
		for (int32 I = 0; I < BoneCount; ++I)
		{
			Bones[I].BoneName = BoneNames[I];
			Bones[I].Initialize(GetRequiredBones());
			// A lower LOD may strip limbs; skip those chains, but never rotate a missing pelvis.
			if (I == Pelvis) bHasCoreBones = Bones[I].IsValidToEvaluate(GetRequiredBones());
		}
	}

	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
		const auto* Character = Cast<ABrCharacter>(Instance->TryGetPawnOwner());
		const auto* S = UBrDeveloperSettings::Get();
		const bool Alive = Character && Character->GetLife()->IsAlive();
		const EBrPosture Posture = Character ? Character->GetPosture() : EBrPosture::Standing;
		const double Now = Instance->GetWorld() ? Instance->GetWorld()->GetTimeSeconds() : LastUpdateTime;
		const float VisualDelta = FMath::Max(DeltaSeconds, static_cast<float>(Now - LastUpdateTime));
		auto* Anim = CastChecked<UBrCharacterAnimInstance>(Instance);
		const bool Snap = bFirstUpdate || Anim->bSnapPosture;
		if (Snap)
		{
			// A newly relevant/late-joining proxy starts at its persistent state,
			// not a one-frame standing pose. Subsequent changes still blend normally.
			Blend.Weights = Blend.Start = Blend.Target = FBrPostureBlend::Select(Posture, Alive);
			bFirstUpdate = false;
			Anim->bSnapPosture = false;
		}
		LastUpdateTime = Now;
		// Catch up a hidden/URO mesh in O(1), so re-reveal doesn't restart a stale pose transition.
		Blend.Advance(Posture, Alive, VisualDelta,
			FMath::Clamp(S->CrouchPoseBlendTime, 0.f, 2.f), FMath::Clamp(S->PronePoseBlendTime, 0.f, 2.f));
		Speed = Character ? Character->GetVelocity().Size2D() : 0;
		const FVector LocalVelocity = Character ? Character->GetActorQuat().UnrotateVector(Character->GetVelocity()) : FVector::ZeroVector;
		Direction = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));
		MoveAlpha = FMath::FInterpConstantTo(MoveAlpha, FMath::Clamp(Speed / 35.f, 0.f, 1.f), DeltaSeconds, 5.f);
		if (Snap) MoveAlpha = FMath::Clamp(Speed / 35.f, 0.f, 1.f);
		CrouchDrop = FMath::Clamp(S->CrouchPelvisDrop * (96.f - S->CrouchCapsuleHalfHeight) / 36.f, 0.f, 75.f);
		CrouchForward = FMath::Clamp(S->CrouchPelvisForward, 0.f, 35.f);
		HipLean = FMath::Clamp(S->CrouchHipLean, 0.f, 25.f);
		TorsoLean = FMath::Clamp(S->CrouchTorsoLean, 0.f, 55.f);
		KneeOutward = FMath::Clamp(S->CrouchKneeOutward, 0.f, 2.f);
		FootSpread = FMath::Clamp(S->CrouchFootSpread, 0.f, 20.f);
		CrawlPhase = FMath::Fmod(CrawlPhase + DeltaSeconds * FMath::Clamp(Speed / 120.f, 0.f, 2.f) * 2.f * PI, 2.f * PI);
		Time += DeltaSeconds;
	}

	void Sample(int32 Slot, FPoseContext& Out, float SampleSpeed)
	{
		Out.ResetToRefPose();
		FSample& State = Samples[Slot];
		FAnimationPoseData Data(Out);
		if (auto* BS = Cast<UBlendSpace>(State.Asset))
		{
			// Match the existing BS_Idle_Walk_Run: direction X, speed Y.
			BS->GetSamplesFromBlendInput(FVector(Direction, SampleSpeed, 0), State.Cache, State.Triangle, true);
			const float Length = BS->GetAnimationLengthFromSampleData(State.Cache);
			const float Phase = Length > UE_SMALL_NUMBER ? FMath::Fmod(Time / Length, 1.) : 0;
			BS->ResetBlendSamples(State.Cache, Phase, true, false);
			BS->GetAnimationPose(State.Cache, FAnimExtractContext(0., false), Data);
		}
		else if (auto* Sequence = Cast<UAnimSequence>(State.Asset))
		{
			const double Length = Sequence->GetPlayLength();
			Sequence->GetAnimationPose(Data, FAnimExtractContext(Length > UE_SMALL_NUMBER ? FMath::Fmod(Time, Length) : 0., false));
		}
	}

	FCompactPoseBoneIndex Index(EBone Bone) const { return Bones[Bone].GetCompactPoseIndex(GetRequiredBones()); }

	void Procedural(FPoseContext& Out, bool Prone)
	{
		if (!bHasCoreBones) return;
		FCSPose<FCompactPose> CS;
		CS.InitPose(Out.Pose);
		const auto Get = [&](EBone B) { return CS.GetComponentSpaceTransform(Index(B)); };
		const auto Valid = [&](EBone B) { return Index(B) != INDEX_NONE; };
		FTransform Feet[2];
		if (Valid(FootL)) Feet[0] = Get(FootL);
		if (Valid(FootR)) Feet[1] = Get(FootR);
		FTransform PelvisPose = Get(Pelvis);
		if (Prone)
		{
			// Mesh forward is +Y (component yaw -90). -90 around +X puts the face DOWN,
			// head towards +Y. Rotate the skeleton, never the CMC-owned mesh component.
			PelvisPose.SetRotation((FQuat(FVector::ForwardVector, -PI / 2) * PelvisPose.GetRotation()).GetNormalized());
			PelvisPose.SetLocation(FVector(0, 0, 24));
		}
		else
		{
			PelvisPose.AddToTranslation(FVector(0, CrouchForward, -CrouchDrop));
			PelvisPose.SetRotation((FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-HipLean)) *
				PelvisPose.GetRotation()).GetNormalized());
		}
		SetCS(CS, Index(Pelvis), PelvisPose);
		if (Valid(Spine) && !Prone)
		{
			FTransform Torso = Get(Spine);
			Torso.SetRotation((FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-TorsoLean)) * Torso.GetRotation()).GetNormalized());
			SetCS(CS, Index(Spine), Torso);
		}
		if (Prone && Valid(Neck))
		{
			FTransform Look = Get(Neck);
			Look.SetRotation((FQuat(FVector::ForwardVector, FMath::DegreesToRadians(22.f)) * Look.GetRotation()).GetNormalized());
			SetCS(CS, Index(Neck), Look);
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const bool Left = Side == 0;
			// Mesh +X is character-left after the mesh's -90 yaw offset.
			const double Sign = Left ? 1 : -1;
			const float Wave = FMath::Sin(CrawlPhase + (Left ? 0 : PI)) * MoveAlpha;
			const EBone Foot = Left ? FootL : FootR;
			if (!Prone)
			{
				SolveLimb(CS, Index(Left ? ThighL : ThighR), Index(Left ? CalfL : CalfR), Index(Foot),
					Feet[Side].GetLocation() + FVector(Sign * FootSpread, 0, 0), FVector(Sign * KneeOutward, 1, 0));
				if (Valid(Foot))
				{
					FTransform Flat = Get(Foot);
					Flat.SetRotation(Feet[Side].GetRotation());
					SetCS(CS, Index(Foot), Flat);
				}
			}
			else
			{
				SolveLimb(CS, Index(Left ? ThighL : ThighR), Index(Left ? CalfL : CalfR), Index(Foot),
					FVector(Sign * (19 + 5 * Wave), -68 + 12 * Wave, 10 + 3 * FMath::Max(0.f, Wave)),
					FVector(Sign * 0.4, -0.3, 1));
				SolveLimb(CS, Index(Left ? ArmL : ArmR), Index(Left ? ElbowL : ElbowR), Index(Left ? HandL : HandR),
					FVector(Sign * 18, 88 + 10 * Wave, 20), FVector(Sign, 0, -1));
			}
		}
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(CS, Out.Pose);
		Out.Pose.NormalizeRotations();
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		Sample(0, Output, Speed);
		const float LowWeight = Blend.Weights.Y + Blend.Weights.Z;
		if (LowWeight < UE_SMALL_NUMBER) return true;
		FPoseContext Low(this);
		FPoseContext Crawl(this);
		const auto LowPose = [&](FPoseContext& Pose, bool Prone)
		{
			// Base cycle retained in crouch. Prone starts from idle, with speed-driven limbs only.
			Sample(0, Pose, Prone ? 0 : Speed);
			Procedural(Pose, Prone);
			const int32 Idle = Prone ? 3 : 1;
			FPoseContext Override(this);
			if (Samples[Idle].Asset)
			{
				Sample(Idle, Override, Speed);
				FAnimationPoseData Data(Pose), Other(Override);
				FAnimationRuntime::BlendTwoPosesTogetherInPlace(Data, Other, Samples[Idle + 1].Asset ? 0.f : MoveAlpha);
			}
			if (Samples[Idle + 1].Asset)
			{
				Sample(Idle + 1, Override, Speed);
				FAnimationPoseData Data(Pose), Other(Override);
				FAnimationRuntime::BlendTwoPosesTogetherInPlace(Data, Other, 1.f - MoveAlpha);
			}
		};
		if (Blend.Weights.Y > UE_SMALL_NUMBER) LowPose(Low, false);
		if (Blend.Weights.Z > UE_SMALL_NUMBER) LowPose(Crawl, true);
		if (Blend.Weights.Y <= UE_SMALL_NUMBER) Low = Crawl;
		else if (Blend.Weights.Z > UE_SMALL_NUMBER)
		{
			FAnimationPoseData Data(Low), Other(Crawl);
			FAnimationRuntime::BlendTwoPosesTogetherInPlace(Data, Other, Blend.Weights.Y / LowWeight);
		}
		FAnimationPoseData Data(Output), Other(Low);
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(Data, Other, 1.f - LowWeight);
		return true;
	}
};

void UBrCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	const auto* S = UBrDeveloperSettings::Get();
	const USkeletalMesh* Mesh = GetSkelMeshComponent() ? GetSkelMeshComponent()->GetSkeletalMeshAsset() : nullptr;
	Assets.Reset(5);
	UBlendSpace* Locomotion = S->PostureLocomotion.LoadSynchronous();
	Assets.Add(Locomotion && Mesh && Locomotion->GetSkeleton() == Mesh->GetSkeleton() && !Locomotion->IsValidAdditive() ? Locomotion : nullptr);
	for (const TSoftObjectPtr<UAnimationAsset>& Slot : {S->CrouchIdle, S->CrouchMove, S->ProneIdle, S->ProneMove})
	{
		UAnimationAsset* Asset = Slot.LoadSynchronous();
		const bool Supported = Asset && (Asset->IsA<UAnimSequence>() || Asset->IsA<UBlendSpace>()) &&
			!Asset->IsValidAdditive() && Mesh && Asset->GetSkeleton() == Mesh->GetSkeleton();
		if (!Slot.IsNull() && !Supported)
			UE_LOG(LogBr, Warning, TEXT("Posture asset %s missing, additive, unsupported or wrong skeleton; using procedural pose."), *Slot.ToString());
		Assets.Add(Supported ? Asset : nullptr);
	}
	if (!Assets[0]) UE_LOG(LogBr, Error, TEXT("Posture locomotion blendspace missing or incompatible with mesh: %s"), *S->PostureLocomotion.ToString());
	for (FName Name : BoneNames)
		if (!Mesh || GetSkelMeshComponent()->GetBoneIndex(Name) == INDEX_NONE)
			UE_LOG(LogBr, Warning, TEXT("Posture mesh %s missing bone %s; skipping unavailable chains."), *GetNameSafe(GetSkelMeshComponent()->GetSkeletalMeshAsset()), *Name.ToString());
	GetProxyOnGameThread<FBrCharacterAnimProxy>().SetAssets(this);
}

void UBrCharacterAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();
	if (auto* Character = Cast<ABrCharacter>(TryGetPawnOwner())) Character->UpdatePostureWeaponPose();
}

void UBrCharacterAnimInstance::SnapPostureOnNextUpdate()
{
	check(IsInGameThread());
	bSnapPosture = true;
}

FAnimInstanceProxy* UBrCharacterAnimInstance::CreateAnimInstanceProxy() { return new FBrCharacterAnimProxy(this); }
void UBrCharacterAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
