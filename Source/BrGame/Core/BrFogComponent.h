#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Canvas.h"
#include "BrFogComponent.generated.h"

class ABrCharacter;
class ABrPlayerState;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class APostProcessVolume;
class UPrimitiveComponent;

/** Owning local view only; no replicated exploration or actor hidden flags. */
UCLASS()
class BRGAME_API UBrFogComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBrFogComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void StartView();
	bool CanSee(const AActor* Actor) const;
	// Borrow the active view's masks; never allocate/copy exploration for the map.
	UTextureRenderTarget2D* GetVisibleMask() const { return MatchesActiveView() ? VisibleMask.Get() : nullptr; }
	UTextureRenderTarget2D* GetExploredMask() const { return MatchesActiveView() ? ExploredMask.Get() : nullptr; }
	const FBox& GetMapBounds() const { return Bounds; }
	/** Exact last local hide decision, without additional per-frame LOS traces. */
	bool IsPresentedVisible(const AActor* Actor) const;
	static bool LocallyVisible(const AActor* Actor);
	/** Development smoke only: synchronous GPU readback, never used in ordinary play. */
	void ValidateSmokeMasks(int32 Stage);
private:
	void Update();
	bool InitializeRendering();
	void DrawMask(UTextureRenderTarget2D* Target, const FLinearColor& Color);
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> VisibleMask;
	/** Exactly one target; switching groups discards inactive exploration. */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ExploredMask;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> Volume;
	int64 LastGroup = MIN_int64;
	int64 PresentedGroup = MIN_int64;
	TSet<TWeakObjectPtr<AActor>> PresentedVisible;
	bool MatchesActiveView() const;
	TArray<TWeakObjectPtr<ABrCharacter>> Sources;
	TArray<FCanvasUVTri> Triangles;
	TArray<FVector2D> Polygon;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, bool> OriginalHidden;
	struct FObstaclePresentation
	{
		bool bCustomDepth = false;
		int32 Stencil = 0;
	};
	// Weak, local-only map geometry; last-seen history belongs only to the active group.
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FObstaclePresentation> OriginalObstacles;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, double> ObstacleLastSeen;
	void SeeObstacle(UPrimitiveComponent* Primitive, double Now);
	void UpdateObstacles(bool bShow, double Now);
	void RestorePresentation();
	FBox Bounds = FBox(ForceInit);
	FTimerHandle Timer;
	bool bActive = false;
	bool bRenderFailed = false;
};
