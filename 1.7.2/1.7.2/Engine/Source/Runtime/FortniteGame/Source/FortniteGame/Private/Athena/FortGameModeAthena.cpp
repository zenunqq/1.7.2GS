#include "pch.h"
#include <vector>
#include "FortGameModeAthena.h"
#include "GameEngine.h"
#include "AbilitySystemComponent.h"
#include "FortPlayerControllerAthena.h"
#include "FortInventory.h"

namespace {
	constexpr int32 DefaultPlaylistId = 1;
	constexpr uint64_t NullFuncOffset = 0x137DBC0;
	constexpr uint64_t ApplyCharacterPartsOffset = 0x217db10;
	const wchar_t* AbilitySetPath = L"/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_DefaultPlayer.GAS_DefaultPlayer";
	const wchar_t* PawnClassPath = L"/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C";
	const wchar_t* PickaxePath = L"/Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01";

	void SetPlaylist(AFortGameModeAthena* GameMode) {
		auto GameState = (AFortGameStateAthena*)GameMode->GameState;
		GameState->CurrentPlaylistId = GameMode->CurrentPlaylistId = DefaultPlaylistId;
		GameMode->bAlwaysDBNO = false;
	}

	bool IsStartingItem(AFortGameModeAthena* GameMode, UFortItemDefinition* Definition) {
		static auto Pickaxe = Utils::FindObject<UFortItemDefinition>(PickaxePath);
		if (Definition == Pickaxe)
			return true;
		for (auto& StartingItem : GameMode->StartingItems)
			if (StartingItem.Item == Definition)
				return true;
		return false;
	}

	void RemoveNonStartingItems(AFortGameModeAthena* GameMode, AFortPlayerControllerAthena* PlayerController) {
		std::vector<FGuid> ToRemove;
		for (auto& Entry : PlayerController->WorldInventory->Inventory.ReplicatedEntries)
			if (!IsStartingItem(GameMode, Entry.ItemDefinition))
				ToRemove.push_back(Entry.ItemGuid);

		for (auto& Guid : ToRemove)
			Inventory::Remove(PlayerController, Guid);
	}

	void GiveStartingItems(AFortGameModeAthena* GameMode, AFortPlayerControllerAthena* PlayerController) {
		Inventory::GiveItem(PlayerController, Utils::FindObject<UFortItemDefinition>(PickaxePath));
		for (auto& StartingItem : GameMode->StartingItems)
			if (StartingItem.Item && StartingItem.Count)
				Inventory::GiveItem(PlayerController, StartingItem.Item, StartingItem.Count);
	}

	void ApplyRandomHero(AFortPlayerStateAthena* PlayerState) {
		static std::vector<UFortHeroType*> HeroTypes;
		if (HeroTypes.empty()) {
			for (int i = 0; i < UObject::GObjects->Num(); i++) {
				auto Object = UObject::GObjects->GetByIndex(i);
				if (!Object)
					continue;
				auto HeroType = Object->Cast<UFortHeroType>();
				if (!HeroType)
					continue;
				if (UKismetSystemLibrary::GetPathName(HeroType).ToString().starts_with("/Game/Athena/Heroes/"))
					HeroTypes.push_back(HeroType);
			}
		}

		if (HeroTypes.empty())
			return;

		static std::mt19937 Generator{ std::random_device{}() };
		std::uniform_int_distribution<size_t> Distribution(0, HeroTypes.size() - 1);

		for (auto& Specialization : HeroTypes[Distribution(Generator)]->Specializations) {
			if (!Specialization)
				continue;
			for (auto& Part : Specialization->CharacterParts)
				if (Part)
					PlayerState->CharacterParts[(int)Part->CharacterPartType] = Part;
		}
	}
}

void GameMode::ReadyToStartMatch(UObject* Context, FFrame& Stack, bool* Ret) {
	Stack.IncrementCode();
	auto GameMode = Context->Cast<AFortGameModeAthena>();
	if (!GameMode) {
		*Ret = callOGWithRet(((AGameMode*)Context), Stack.CurrentNativeFunction, ReadyToStartMatch);
		return;
	}
	auto GameState = ((AFortGameStateAthena*)GameMode->GameState);

	static bool bInitialized = false;
	if (!bInitialized) {
		bInitialized = true;
		GameMode->WarmupRequiredPlayerCount = 1;
		SetPlaylist(GameMode);
	}

	if (!GameMode->bWorldIsReady) {
		auto Starts = Utils::GetAll<AFortPlayerStartWarmup>();
		auto StartsNum = Starts.Num();
		Starts.Free();
		if (StartsNum == 0 || !GameState->MapInfo) {
			*Ret = false;
			return;
		}

		AbilitySets.Add(Utils::FindObject<UFortAbilitySet>(AbilitySetPath));

		GameMode->DefaultPawnClass = Utils::FindObject<UClass>(PawnClassPath);
		Utils::Patch<uint8>(ImageBase + NullFuncOffset, 0xc3);
		SetConsoleTitleA("Tung 1.7.2: Ready");
		GameMode->bWorldIsReady = true;
	}

	*Ret = callOGWithRet(((AGameMode*)Context), Stack.CurrentNativeFunction, ReadyToStartMatch);
}

APawn* GameMode::SpawnDefaultPawnFor(UObject* Context, FFrame& Stack, APawn** Ret) {
	AController* NewPlayer;
	AActor* StartSpot;
	Stack.StepCompiledIn(&NewPlayer);
	Stack.StepCompiledIn(&StartSpot);
	Stack.IncrementCode();

	auto GameMode = (AFortGameModeAthena*)Context;
	auto Transform = StartSpot->GetTransform();
	auto Pawn = GameMode->SpawnDefaultPawnAtTransform(NewPlayer, Transform);

	auto PlayerController = NewPlayer->Cast<AFortPlayerControllerAthena>();
	if (!PlayerController || !PlayerController->WorldInventory)
		return *Ret = Pawn;

	if (PlayerController->WorldInventory->Inventory.ReplicatedEntries.Num() != 0) {
		RemoveNonStartingItems(GameMode, PlayerController);
		return *Ret = Pawn;
	}

	GiveStartingItems(GameMode, PlayerController);

	auto PlayerState = (AFortPlayerStateAthena*)PlayerController->PlayerState;
	for (auto& AbilitySet : AbilitySets)
		Abilities::GiveAbilitySet(PlayerState->AbilitySystemComponent, AbilitySet);

	ApplyRandomHero(PlayerState);

	((void (*)(APlayerState*, APawn*))(ImageBase + ApplyCharacterPartsOffset))(PlayerController->PlayerState, Pawn);

	return *Ret = Pawn;
}

EFortTeam GameMode::PickTeam(AFortGameModeAthena* GameMode, uint8_t PreferredTeam, AFortPlayerControllerAthena* Controller) {
	uint8_t Team = CurrentTeam;

	if (++PlayersOnCurTeam >= 1) {
		CurrentTeam++;
		PlayersOnCurTeam = 0;
	}

	return EFortTeam(Team);
}

void GameMode::HandleStartingNewPlayer(UObject* Context, FFrame& Stack) {
	AFortPlayerControllerAthena* NewPlayer;
	Stack.StepCompiledIn(&NewPlayer);
	Stack.IncrementCode();
	auto GameMode = (AFortGameModeAthena*)Context;

	return callOG(GameMode, Stack.CurrentNativeFunction, HandleStartingNewPlayer, NewPlayer);
}

void GameMode::Hook()
{
	Utils::ExecHook(L"/Script/Engine.GameMode.ReadyToStartMatch", ReadyToStartMatch, ReadyToStartMatchOG);
	Utils::ExecHook(L"/Script/Engine.GameModeBase.SpawnDefaultPawnFor", SpawnDefaultPawnFor);
	Utils::ExecHook(L"/Script/Engine.GameModeBase.HandleStartingNewPlayer", HandleStartingNewPlayer, HandleStartingNewPlayerOG);
}
