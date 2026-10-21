#include "pch.h"
#include "HookingLibrary.h"
#include "FortInventory.h"
#include "FortPlayerControllerAthena.h"

FFortRangedWeaponStats* Inventory::GetStats(UFortWeaponItemDefinition* Def)
{
	if (!Def || !Def->WeaponStatHandle.DataTable)
		return nullptr;

	auto Val = Def->WeaponStatHandle.DataTable->RowMap.Search([Def](FName& Key, uint8_t* Value)
		{ return Def->WeaponStatHandle.RowName == Key && Value; });

	return Val ? *(FFortRangedWeaponStats**)Val : nullptr;
}

int Inventory::GetLevel(const FDataTableCategoryHandle& CategoryHandle)
{
	auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;

	if (!CategoryHandle.DataTable)
		return 0;

	if (!CategoryHandle.ColumnName.ComparisonIndex)
		return 0;

	if (!CategoryHandle.RowContents.ComparisonIndex)
		return 0;

	int Level = 0;
	FFortLootLevelData* LootLevelData = nullptr;
	for (auto& LootLevelDataPair : (TMap<FName, FFortLootLevelData*>)CategoryHandle.DataTable->RowMap)
	{
		if (LootLevelDataPair.Value()->Category != CategoryHandle.RowContents || LootLevelDataPair.Value()->LootLevel > GameState->WorldLevel || LootLevelDataPair.Value()->LootLevel <= Level)
			continue;

		Level = LootLevelDataPair.Value()->LootLevel;
		LootLevelData = LootLevelDataPair.Value();
	}

	if (LootLevelData)
	{
		auto subbed = LootLevelData->MaxItemLevel - LootLevelData->MinItemLevel;

		if (subbed <= -1)
			subbed = 0;
		else
		{
			auto calc = (int)(((float)rand() / 32767) * (float)(subbed + 1));
			if (calc <= subbed)
				subbed = calc;
		}

		return subbed + LootLevelData->MinItemLevel;
	}

	return 0;
}

FFortItemEntry* Inventory::MakeItemEntry(UFortItemDefinition* ItemDefinition, int32 Count, int32 Level) {
	FFortItemEntry* ItemEntry = new FFortItemEntry();

	ItemEntry->MostRecentArrayReplicationKey = -1;
	ItemEntry->ReplicationID = -1;
	ItemEntry->ReplicationKey = -1;

	ItemEntry->ItemDefinition = ItemDefinition;
	ItemEntry->Count = Count;
	ItemEntry->Durability = 1.f;
	ItemEntry->GameplayAbilitySpecHandle = FGameplayAbilitySpecHandle(-1);
	ItemEntry->ParentInventory.ObjectIndex = -1;
	ItemEntry->Level = Level;
	if (auto Weapon = ItemDefinition->Cast<UFortWeaponItemDefinition>())
	{
		if (auto Stats = GetStats(Weapon))
			ItemEntry->LoadedAmmo = Stats->ClipSize;
	}

	return ItemEntry;
}

void Inventory::TriggerInventoryUpdate(AFortPlayerController* PlayerController, FFortItemEntry* Entry)
{
	if (!PlayerController)
		return;
	AFortInventory* Inventory = PlayerController->WorldInventory;
	Inventory->bRequiresLocalUpdate = true;
	Inventory->HandleInventoryLocalUpdate();

	return Entry ? Inventory->Inventory.MarkItemDirty(*Entry) : Inventory->Inventory.MarkArrayDirty();
}

UFortWorldItem* Inventory::GiveItem(AFortPlayerController* PlayerController, UFortItemDefinition* Def, int Count, int LoadedAmmo, int Level, bool ShowPickupNoti, bool updateInventory, int PhantomReserveAmmo)
{
	if (!PlayerController || !PlayerController->WorldInventory || !Def || !Count)
		return nullptr;

	UFortWorldItem* Item = (UFortWorldItem*)Def->CreateTemporaryItemInstanceBP(Count, Level);
	if (!Item)
		return nullptr;

	Item->SetOwningControllerForTemporaryItem(PlayerController);
	Item->ItemEntry.LoadedAmmo = LoadedAmmo;

	if (Def->IsA<UFortAmmoItemDefinition>() || Def->IsA<UFortResourceItemDefinition>())
	{
		FFortItemEntryStateValue Value{};
		Value.IntValue = ShowPickupNoti;
		Value.StateType = EFortItemEntryState::ShouldShowItemToast;
		Item->ItemEntry.StateValues.Add(Value);
	}

	PlayerController->WorldInventory->Inventory.ReplicatedEntries.Add(Item->ItemEntry);
	PlayerController->WorldInventory->Inventory.ItemInstances.Add(Item);

	if (PlayerController->QuickBars)
		PlayerController->QuickBars->ServerAddItemInternal(Item->ItemEntry.ItemGuid, GetQuickbar(Def), -1);

	if (updateInventory)
		TriggerInventoryUpdate(PlayerController, &Item->ItemEntry);
	return Item;
}

UFortWorldItem* Inventory::GiveItem(AFortPlayerController* PC, FFortItemEntry entry, int Count, bool ShowPickupNoti, bool updateInventory)
{
	if (Count == -1)
		Count = entry.Count;
	return GiveItem(PC, entry.ItemDefinition, Count, entry.LoadedAmmo, entry.Level, ShowPickupNoti, updateInventory, -1);
}


EFortQuickBars Inventory::GetQuickbar(UFortItemDefinition* ItemDefinition)
{
	if (!ItemDefinition) return EFortQuickBars::Max_None;
	return ItemDefinition->IsA<UFortWeaponMeleeItemDefinition>() || ItemDefinition->IsA<UFortResourceItemDefinition>() || ItemDefinition->IsA<UFortAmmoItemDefinition>() || ItemDefinition->IsA<UFortTrapItemDefinition>() || ItemDefinition->IsA<UFortBuildingItemDefinition>() || ItemDefinition->IsA<UFortEditToolItemDefinition>() ? EFortQuickBars::Secondary : EFortQuickBars::Primary;
}

AFortPickupAthena* Inventory::SpawnPickup(FVector Loc, FFortItemEntry& Entry, AFortPlayerPawn* Pawn, int OverrideCount, bool Toss, bool RandomRotation, bool bCombine)
{
	AFortPickupAthena* NewPickup = Utils::SpawnActor<AFortPickupAthena>(Loc, {});
	if (!NewPickup)
		return nullptr;
	NewPickup->PrimaryPickupItemEntry.ItemDefinition = Entry.ItemDefinition;
	NewPickup->PrimaryPickupItemEntry.LoadedAmmo = Entry.LoadedAmmo;
	NewPickup->PrimaryPickupItemEntry.Count = OverrideCount != -1 ? OverrideCount : Entry.Count;
	NewPickup->OnRep_PrimaryPickupItemEntry();
	NewPickup->PawnWhoDroppedPickup = Pawn;


	NewPickup->TossPickup(Loc, Pawn, -1);
	if (NewPickup->bTossedFromContainer) NewPickup->OnRep_TossedFromContainer();

	return NewPickup;
}

AFortPickupAthena* Inventory::SpawnPickup(FVector Loc, UFortItemDefinition* ItemDefinition, int Count, int LoadedAmmo, AFortPlayerPawn* Pawn, bool Toss, bool bRandomRotation)
{
	if (!ItemDefinition)
		return nullptr;

	auto Entry = MakeItemEntry(ItemDefinition, Count, 0);
	if (LoadedAmmo > 0)
		Entry->LoadedAmmo = LoadedAmmo;

	auto Pickup = SpawnPickup(Loc, *Entry, Pawn, -1, Toss, bRandomRotation, true);
	delete Entry;
	return Pickup;
}

void Inventory::ReplaceEntry(AFortPlayerController* PlayerController, FFortItemEntry& Entry)
{
	if (!PlayerController)
		return;
	auto ent = PlayerController->WorldInventory->Inventory.ItemInstances.Search([&](UFortWorldItem* item)
		{ return item->ItemEntry.ItemGuid == Entry.ItemGuid; });
	if (ent)
		(*ent)->ItemEntry = Entry;

	TriggerInventoryUpdate(PlayerController, &Entry);
}

void Inventory::Remove(AFortPlayerController* PlayerController, FGuid Guid)
{
	if (!PlayerController || !PlayerController->WorldInventory)
		return;

	auto& Replicated = PlayerController->WorldInventory->Inventory.ReplicatedEntries;
	auto& Instances = PlayerController->WorldInventory->Inventory.ItemInstances;

	auto EntryIndex = Replicated.SearchIndex([&](FFortItemEntry& Entry) { return Entry.ItemGuid == Guid; });
	if (EntryIndex == -1)
	{
		auto InstanceIndex = Instances.SearchIndex([&](UFortWorldItem* Item) { return Item->ItemEntry.ItemGuid == Guid; });
		if (InstanceIndex != -1)
			Instances.Remove(InstanceIndex);
		return TriggerInventoryUpdate(PlayerController, nullptr);
	}

	auto Definition = Replicated[EntryIndex].ItemDefinition;
	Replicated.Remove(EntryIndex);

	auto InstanceIndex = Instances.SearchIndex([&](UFortWorldItem* Item) { return Item->ItemEntry.ItemGuid == Guid; });
	if (InstanceIndex != -1)
		Instances.Remove(InstanceIndex);

	if (PlayerController->QuickBars)
	{
		auto Type = GetQuickbar(Definition);
		auto& QuickBar = Type == EFortQuickBars::Primary ? PlayerController->QuickBars->PrimaryQuickBar : PlayerController->QuickBars->SecondaryQuickBar;

		int SlotIndex = -1;
		int Index = 0;
		for (auto& Slot : QuickBar.Slots)
		{
			for (auto& Item : Slot.Items)
			{
				if (Item == Guid)
				{
					SlotIndex = Index;
					break;
				}
			}
			if (SlotIndex != -1)
				break;
			Index++;
		}

		if (SlotIndex != -1)
		{
			PlayerController->QuickBars->EmptySlot(Type, SlotIndex);
			PlayerController->QuickBars->ServerRemoveItemInternal(Guid, false, true);
		}
	}

	TriggerInventoryUpdate(PlayerController, nullptr);
}
