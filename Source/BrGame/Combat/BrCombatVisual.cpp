#include "BrCombatVisual.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ABrCombatVisual::ABrCombatVisual()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sphere"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	Mesh->SetStaticMesh(Sphere.Object);
	if (Material.Succeeded())
	{
		Mesh->SetMaterial(0, Material.Object);
	}
	SetActorScale3D(FVector(0.07f));
}

void ABrCombatVisual::InitTracer(const FVector& Direction, float Speed, float Range)
{
	Velocity = Direction * Speed;
	SetLifeSpan(FMath::Max(0.01f, Range / FMath::Max(1.0f, Speed)));
}

void ABrCombatVisual::InitExplosion(float Radius)
{
	ExplosionRadius = Radius;
	SetLifeSpan(0.3f);
}

void ABrCombatVisual::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (ExplosionRadius > 0)
	{
		SetActorScale3D(FVector(ExplosionRadius / 50.0f * FMath::Clamp(Age / 0.3f, 0.01f, 1.0f)));
	}
	else
	{
		SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);
	}
}
