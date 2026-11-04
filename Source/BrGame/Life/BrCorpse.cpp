#include "BrCorpse.h"
#include "BrDeveloperSettings.h"
#include "BrLog.h"
#include "BrSupinePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ABrCorpse::ABrCorpse()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletonPlaceholder"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	Mesh->SetRelativeRotation(BrSupinePose::Rotation());
	Mesh->SetComponentTickEnabled(false);
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> White(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (White.Succeeded()) Mesh->SetMaterial(0, White.Object);
}

void ABrCorpse::BeginPlay()
{
	Super::BeginPlay();
	USkeletalMesh* CharacterMesh = UBrDeveloperSettings::Get()->DefaultCharacterMesh.LoadSynchronous();
	if (!CharacterMesh)
	{
		UE_LOG(LogBr, Error, TEXT("Corpse mannequin mesh could not load."));
		return;
	}
	Mesh->SetSkeletalMesh(CharacterMesh);
	Mesh->SetRelativeLocation(FVector(0, 0, BrSupinePose::GroundLift(CharacterMesh)));
	UMaterialInterface* White = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!White)
	{
		UE_LOG(LogBr, Error, TEXT("Corpse bone-white placeholder material is missing."));
	}
	else
	{
		for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index) Mesh->SetMaterial(Index, White);
	}
	Mesh->SetComponentTickEnabled(false);
	if (HasAuthority())
	{
		// Newly relevant connections still receive the initial actor bunch; nothing changes afterward.
		ForceNetUpdate();
		SetNetDormancy(DORM_DormantAll);
		UE_LOG(LogBr, Display, TEXT("Corpse created: %s"), *GetName());
	}
}
