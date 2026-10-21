#include "pch.h"
#include "FortPlayerControllerAthena.h"
#include "AbilitySystemComponent.h"

void Player::ServerReadyToStartMatch(UObject* Context, FFrame& Stack)
{
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerController*)Context;

	PlayerController->QuickBars = Utils::SpawnActor<AFortQuickBars>(FVector{});
	PlayerController->QuickBars->SetOwner(PlayerController);

	callOG(PlayerController, Stack.CurrentNativeFunction, ServerReadyToStartMatch);
}

void Player::ServerAcknowledgePossession(UObject* Context, FFrame& Stack)
{
	APawn* Pawn;
	Stack.StepCompiledIn(&Pawn);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerController*)Context;
	PlayerController->AcknowledgedPawn = Pawn;
}

void Player::GetPlayerViewPoint(APlayerController* PlayerController, FVector& Loc, FRotator& Rot)
{
	static auto SFName = FName(L"Spectating");
	if (PlayerController->StateName == SFName)
	{
		Loc = PlayerController->LastSpectatorSyncLocation;
		Rot = PlayerController->LastSpectatorSyncRotation;
	}
	else if (PlayerController->GetViewTarget())
	{
		Loc = PlayerController->GetViewTarget()->K2_GetActorLocation();
		Rot = PlayerController->GetControlRotation();
	}
	else return GetPlayerViewPointOG(PlayerController, Loc, Rot);
}

void Player::ServerExecuteInventoryItem(UObject* Context, FFrame& Stack)
{
	FGuid ItemGuid;
	Stack.StepCompiledIn(&ItemGuid);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerController*)Context;
	if (!PlayerController) return;
	auto entry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry) {
		return entry.ItemGuid == ItemGuid;
		});

	if (!entry || !PlayerController->MyFortPawn) return;
	UFortWeaponItemDefinition* ItemDefinition = entry->ItemDefinition->IsA<UFortGadgetItemDefinition>() ? ((UFortGadgetItemDefinition*)entry->ItemDefinition)->GetDecoItemDefinition() : (UFortWeaponItemDefinition*)entry->ItemDefinition;
	auto Weapon = PlayerController->MyFortPawn->EquipWeaponDefinition(ItemDefinition, ItemGuid);
	if (auto BuildingTool = Weapon->Cast<AFortWeap_BuildingTool>())
	{
		static auto RoofPiece = Utils::FindObject<UFortBuildingItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS");
		static auto FloorPiece = Utils::FindObject<UFortBuildingItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor");
		static auto WallPiece = Utils::FindObject<UFortBuildingItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall");
		static auto StairPiece = Utils::FindObject<UFortBuildingItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W");

		if (ItemDefinition == RoofPiece)
		{
			static auto RoofMetadata = Utils::FindObject<UBuildingEditModeMetadata>(L"/Game/Building/EditModePatterns/Roof/EMP_Roof_RoofC.EMP_Roof_RoofC");
			BuildingTool->DefaultMetadata = RoofMetadata;
		}
		else if (ItemDefinition == StairPiece)
		{
			static auto StairMetadata = Utils::FindObject<UBuildingEditModeMetadata>(L"/Game/Building/EditModePatterns/Stair/EMP_Stair_StairW.EMP_Stair_StairW");
			BuildingTool->DefaultMetadata = StairMetadata;
		}
		else if (ItemDefinition == WallPiece)
		{
			static auto WallMetadata = Utils::FindObject<UBuildingEditModeMetadata>(L"/Game/Building/EditModePatterns/Wall/EMP_Wall_Solid.EMP_Wall_Solid");
			BuildingTool->DefaultMetadata = WallMetadata;
		}
		else if (ItemDefinition == FloorPiece)
		{
			static auto FloorMetadata = Utils::FindObject<UBuildingEditModeMetadata>(L"/Game/Building/EditModePatterns/Floor/EMP_Floor_Floor.EMP_Floor_Floor");
			BuildingTool->DefaultMetadata = FloorMetadata;
		}

		BuildingTool->OnRep_DefaultMetadata();
	}
}

void Player::ServerReturnToMainMenu(UObject* Context, FFrame& Stack)
{
	Stack.IncrementCode();
	return ((AFortPlayerController*)Context)->ClientReturnToMainMenu(L"");
}

void Player::ClientOnPawnDied(AFortPlayerControllerAthena* PlayerController, FFortPlayerDeathReport& DeathReport)
{
	if (!PlayerController)
		return ClientOnPawnDiedOG(PlayerController, DeathReport);
	auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;
	auto PlayerState = (AFortPlayerStateAthena*)PlayerController->PlayerState;

	auto KillerPlayerState = (AFortPlayerStateAthena*)DeathReport.KillerPlayerState;
	auto KillerPawn = (AFortPlayerPawnAthena*)DeathReport.KillerPawn;

	if (KillerPlayerState && KillerPawn && KillerPawn->Controller && KillerPawn->Controller->IsA<AFortPlayerControllerAthena>() && KillerPawn->Controller != PlayerController)
	{
		KillerPlayerState->Kills++;
		KillerPlayerState->OnRep_Kills();

		KillerPlayerState->ClientReportKill(L"uhh");

		auto KillerPC = (AFortPlayerControllerAthena*)KillerPlayerState->Owner;
	}

	PlayerState->DeathInfo.bDBNO = PlayerController->MyFortPawn ? PlayerController->MyFortPawn->IsDBNO() : false;

	PlayerState->OnRep_DeathInfo();

	if (PlayerController->MyFortPawn ? !PlayerController->MyFortPawn->IsDBNO() : true)
	{

		PlayerState->Place = GameState->PlayersLeft;
		PlayerState->OnRep_Place();

		AFortWeapon* DamageCauser = nullptr;
		if (auto Weapon = DeathReport.DamageCauser ? DeathReport.DamageCauser->Cast<AFortWeapon>() : nullptr)
			DamageCauser = Weapon;

		if (PlayerController->MyFortPawn && ((KillerPlayerState && KillerPlayerState->Place == 1) || PlayerState->Place == 1))
		{
			if (PlayerState->Place == 1)
			{
				KillerPlayerState = PlayerState;
				KillerPawn = (AFortPlayerPawnAthena*)PlayerController->MyFortPawn;
			}
			auto KillerPlayerController = (AFortPlayerControllerAthena*)KillerPlayerState->Owner;
			auto KillerWeapon = DamageCauser ? DamageCauser->WeaponData : nullptr;

			KillerPlayerController->PlayWinEffects();
			KillerPlayerController->ClientNotifyWon();
			KillerPlayerController->ClientNotifyTeamWon();

			GameState->WinningTeam = int32(KillerPlayerState->TeamIndex);
			GameState->OnRep_WinningTeam();
		}
	}

	return ClientOnPawnDiedOG(PlayerController, DeathReport);
}

void Player::Hook()
{
	Utils::ExecHook(L"/Script/FortniteGame.FortPlayerController.ServerReadyToStartMatch", ServerReadyToStartMatch, ServerReadyToStartMatchOG);
	Utils::ExecHook(L"/Script/Engine.PlayerController.ServerAcknowledgePossession", ServerAcknowledgePossession);
	Utils::Hook(ImageBase + 0x2320d00, GetPlayerViewPoint, GetPlayerViewPointOG);

	Utils::ExecHook(L"/Script/FortniteGame.FortPlayerController.ServerExecuteInventoryItem", ServerExecuteInventoryItem);
	Utils::ExecHook(L"/Script/FortniteGame.FortPlayerController.ServerReturnToMainMenu", ServerReturnToMainMenu);

	Utils::Hook(ImageBase + 0x9ca190, ClientOnPawnDied, ClientOnPawnDiedOG);

}
