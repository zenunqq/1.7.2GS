#pragma once
#include "pch.h"
#include "HookingLibrary.h"

struct FAbilityReplicatedData
{

	bool bTriggered;

	FVector_NetQuantize100 VectorPayload;

	TMulticastInlineDelegate<void()> Delegate;
};

struct FAbilityReplicatedDataCache
{

	FGameplayAbilityTargetDataHandle TargetData;

	FGameplayTag ApplicationTag;

	bool bTargetConfirmed;

	bool bTargetCancelled;

	TDelegate<void(FGameplayAbilityTargetDataHandle&, FGameplayTag)> TargetSetDelegate;

	TMulticastInlineDelegate<void()> TargetCancelledDelegate;

	FAbilityReplicatedData	GenericEvents[12];

	FPredictionKey PredictionKey;
};

struct FGameplayAbilityReplicatedDataContainer
{
public:
	typedef TPair<FGameplayAbilitySpecHandleAndPredictionKey, TSharedRef<FAbilityReplicatedDataCache>> FKeyDataPair;

	TArray<FKeyDataPair> InUseData;
	TArray<TSharedRef<FAbilityReplicatedDataCache>> FreeData;
};

class Abilities {
private:
	static void InternalServerTryActivateAbility(UFortAbilitySystemComponentAthena*, FGameplayAbilitySpecHandle, bool, FPredictionKey&, FGameplayEventData*);
public:
	static void GiveAbility(UAbilitySystemComponent*, UObject*);
	static void GiveAbilitySet(UAbilitySystemComponent*, UFortAbilitySet*);

	InitHooks;
};
