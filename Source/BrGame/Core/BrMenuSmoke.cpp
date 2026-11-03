#include "BrGameMode.h"
#include "BrPlayerController.h"
#include "BrMenuWidget.h"
#include "BrCharacter.h"
#include "BrLifeComponent.h"
#include "BrHealthComponent.h"
#include "BrInventoryComponent.h"
#include "BrCorpse.h"
#include "BrGameState.h"
#include "BrPlayerState.h"
#include "BrGameInstance.h"
#include "BrLog.h"
#include "BrDeveloperSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void ABrPlayerController::ClientMenuSmoke_Implementation(int32 Stage)
{
#if !UE_BUILD_SHIPPING
	if (!IsLocalController() || !FParse::Param(FCommandLine::Get(), TEXT("BrMenuSmoke"))) return;
	if (Stage == 0)
	{
		ToggleMenu();
		if (MenuWidget) MenuWidget->Act(TEXT("Unstuck"));
		UE_LOG(LogBr, Display, TEXT("MENU CLIENT unstuck requested"));
	}
	else if (Stage == 1)
	{
		if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>()) GI->bMenuSmokeRejoin = true;
		ToggleMenu();
		UE_LOG(LogBr, Display, TEXT("MENU CLIENT confirmed leave"));
		if (MenuWidget) { MenuWidget->Act(TEXT("Leave")); MenuWidget->Act(TEXT("Confirm")); }
	}
#endif
}

#if !UE_BUILD_SHIPPING
void ABrGameMode::ServiceMenuSmoke()
{
	ABrPlayerController* Host = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!HasAuthority() || !Host || !Host->IsLocalController() || !FParse::Param(FCommandLine::Get(), TEXT("BrMenuSmoke"))) return;
	ABrGameState* GS = GetGameState<ABrGameState>();
	const float Now = GetWorld()->GetTimeSeconds();
	if (IsLobbyMode())
	{
		if (GS->PlayerArray.Num() >= 3) StartRound(Host);
		return;
	}
	if (Now < MenuSmokeNext || MenuSmokeStage >= 8) return;
	auto Fail = [&](const TCHAR* Reason)
	{
		UE_LOG(LogBr, Error, TEXT("MENU SMOKE FAIL: %s Stage=%d"), Reason, MenuSmokeStage);
		MenuSmokeStage = 8;
	};
	ABrCharacter* HostPawn = Cast<ABrCharacter>(Host->GetPawn());
	if (MenuSmokeStage == 0)
	{
		if (Now - MatchBeganTime < 5 || !HostPawn) return;
		for (APlayerState* Entry : GS->PlayerArray)
		{
			ABrPlayerController* PC = Cast<ABrPlayerController>(Entry->GetOwner());
			if (PC && PC != Host && Entry->GetPlayerName() == TEXT("MenuClient1") && PC->HasClientLoadedCurrentWorld())
				MenuSmokeClient = PC;
		}
		ABrPlayerController* Client = MenuSmokeClient.Get();
		ABrCharacter* Target = Client ? Cast<ABrCharacter>(Client->GetPawn()) : nullptr;
		if (!Target) return;
		Target->TeleportTo(FVector(-1000, 0, 100), FRotator::ZeroRotator, false, true);
		Target->GetCharacterMovement()->StopMovementImmediately();
		MenuSmokeOrigin = Target->GetActorLocation();
		MenuSmokeIdentity = RoundKey(Client);
		Client->ClientMenuSmoke(0);
		MenuSmokeNext = Now + UBrDeveloperSettings::Get()->UnstuckHoldTime + 2;
		MenuSmokeStage = 1;
	}
	else if (MenuSmokeStage == 1)
	{
		ABrPlayerController* Client = MenuSmokeClient.Get();
		ABrCharacter* Target = Client ? Cast<ABrCharacter>(Client->GetPawn()) : nullptr;
		if (!Target || Client->UnstuckEnd > 0 || Client->UnstuckCooldownEnd <= Now ||
			FVector::DistSquared(Target->GetActorLocation(), MenuSmokeOrigin) < 100)
			{ Fail(TEXT("Unstuck did not move pawn/consume cooldown")); return; }
		UE_LOG(LogBr, Display, TEXT("MENU UNSTUCK SUCCESS PASS"));
		Host->ServerUnstuck();
		if (Host->UnstuckEnd <= 0) { Fail(TEXT("Host unstuck did not start")); return; }
		MenuSmokeNext = Now + 0.5f;
		MenuSmokeStage = 2;
	}
	else if (MenuSmokeStage == 2)
	{
		ABrPlayerController* Client = MenuSmokeClient.Get();
		HostPawn->GetLife()->InvulnerabilityEndTime = 0;
		FDamageEvent Event;
		const float Applied = HostPawn->TakeDamage(1, Event, Client, Client ? Client->GetPawn() : nullptr);
		if (Applied <= 0 || Host->UnstuckEnd > 0 || Host->UnstuckCooldownEnd > Now)
			{ Fail(TEXT("Damage failed to cancel unstuck without cooldown")); return; }
		UE_LOG(LogBr, Display, TEXT("MENU UNSTUCK DAMAGE CANCEL PASS"));
		// Keep client2 connected as a spectator to verify host-leave delivery.
		for (APlayerState* Entry : GS->PlayerArray)
			if (Entry->GetPlayerName() == TEXT("MenuClient2"))
				if (ABrCharacter* Target = Cast<ABrCharacter>(Entry->GetPawn())) Target->GetLife()->Eliminate();
		MenuSmokeNext = Now + 1;
		MenuSmokeStage = 3;
	}
	else if (MenuSmokeStage == 3)
	{
		if (!MenuSmokeClient.IsValid()) { Fail(TEXT("Client vanished before leave")); return; }
		MenuSmokeClient->ClientMenuSmoke(1);
		MenuSmokeNext = Now + 1;
		MenuSmokeStage = 4;
	}
	else if (MenuSmokeStage == 4)
	{
		const FBrRoundRecord* Record = RoundRecords.Find(MenuSmokeIdentity);
		if (!Record || !Record->bForfeited || !Record->Life.bEliminated || !GS->bResults) return;
		int32 Corpses = 0;
		for (TActorIterator<ABrCorpse> It(GetWorld()); It; ++It)
			if (It->GetGear().Weapon) ++Corpses;
		if (Corpses < 2) { Fail(TEXT("Elimination/forfeit corpses missing gear")); return; }
		UE_LOG(LogBr, Display, TEXT("MENU FORFEIT PASS: immediate ledger elimination, corpse gear, winner recheck"));
		MenuSmokeStage = 5;
	}
	else if (MenuSmokeStage == 5)
	{
		FBrRoundRecord* Record = RoundRecords.Find(MenuSmokeIdentity);
		if (!Record || !Record->ActiveController.IsValid() || Record->ActiveController.Get() == MenuSmokeClient.Get()) return;
		ABrPlayerController* Rejoin = Cast<ABrPlayerController>(Record->ActiveController.Get());
		if (!Rejoin) return;
		if (PreparePlayer(Rejoin) || !Rejoin->GetPlayerState<ABrPlayerState>()->bEliminated || Rejoin->GetPawn())
			{ Fail(TEXT("Rejoin restored forfeited contestant")); return; }
		UE_LOG(LogBr, Display, TEXT("MENU REJOIN FORFEITED PASS"));
		MenuSmokeNext = Now + 2;
		MenuSmokeStage = 6;
	}
	else if (MenuSmokeStage == 6)
	{
		Host->ToggleMenu();
		UE_LOG(LogBr, Display, TEXT("MENU HOST LEAVE via overlay confirmation"));
		if (UBrMenuWidget* Menu = Host->GetMenuWidget()) { Menu->Act(TEXT("Leave")); Menu->Act(TEXT("Confirm")); }
		MenuSmokeStage = 8;
	}
}
#endif
