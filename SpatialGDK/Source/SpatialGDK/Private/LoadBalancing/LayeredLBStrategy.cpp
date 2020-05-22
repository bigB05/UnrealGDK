// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "LoadBalancing/LayeredLBStrategy.h"

#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialWorldSettings.h"
#include "LoadBalancing/GridBasedLBStrategy.h"
#include "Utils/LayerInfo.h"
#include "Utils/SpatialActorUtils.h"

#include "Templates/Tuple.h"

DEFINE_LOG_CATEGORY(LogLayeredLBStrategy);

ULayeredLBStrategy::ULayeredLBStrategy()
	: Super()
{
}

ULayeredLBStrategy::~ULayeredLBStrategy()
{
	for (const auto& Elem : LayerNameToLBStrategy)
	{
		Elem.Value->RemoveFromRoot();
	}
}

void ULayeredLBStrategy::Init()
{
	Super::Init();

	VirtualWorkerId CurrentVirtualWorkerId = SpatialConstants::INVALID_VIRTUAL_WORKER_ID + 1;

	const USpatialGDKSettings* Settings = GetDefault<USpatialGDKSettings>();
	check(Settings->bEnableUnrealLoadBalancer);

	const ASpatialWorldSettings* WorldSettings = GetWorld() ? Cast<ASpatialWorldSettings>(GetWorld()->GetWorldSettings()) : nullptr;

	if (WorldSettings == nullptr)
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("If EnableUnrealLoadBalancer is set, WorldSettings should inherit from SpatialWorldSettings to get the load balancing strategy."));
		UAbstractLBStrategy* DefaultLBStrategy = NewObject<UGridBasedLBStrategy>(this);
		AddStrategyForLayer(SpatialConstants::DefaultLayer, DefaultLBStrategy);
		return;
	}

	// This will be uncommented after the next PR.
	// For each Layer, add a LB Strategy for that layer.
// 			TMap<FName, FLBLayerInfo> WorkerLBLayers = WorldSettings->WorkerLBLayers;
// 
// 			for (const TPair<FName, FLayerInfo>& Layer : Settings->WorkerLayers)
// 			{
// 				FName LayerName = Layer.Key;
// 
// 				// Look through the WorldSettings to find the LBStrategy type for this layer.
// 				if (!WorkerLBLayers.Contains(LayerName))
// 				{
// 					UE_LOG(LogLayeredLBStrategy, Error, TEXT("Layer %s does not have a defined LBStrategy in the WorldSettings. It will not be simulated."), *(LayerName.ToString()));
// 					continue;
// 				}
// 
// 				UAbstractLBStrategy* LBStrategy = NewObject<UAbstractLBStrategy>(this, WorkerLBLayers[LayerName].LoadBalanceStrategy);
// 				AddStrategyForLayer(LayerName, LBStrategy);
// 
// 				for (const TSoftClassPtr<AActor>& ClassPtr : Layer.Value.ActorClasses)
// 				{
// 					ClassPathToLayer.Add(ClassPtr, LayerName);
// 				}
// 			}

	// Finally, add the default layer.
	if (WorldSettings->LoadBalanceStrategy == nullptr)
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("If EnableUnrealLoadBalancer is set, there must be a LoadBalancing strategy set. Using a 1x1 grid."));
		UAbstractLBStrategy* DefaultLBStrategy = NewObject<UGridBasedLBStrategy>(this);
		AddStrategyForLayer(SpatialConstants::DefaultLayer, DefaultLBStrategy);
	}
	else
	{
		UAbstractLBStrategy* DefaultLBStrategy = NewObject<UAbstractLBStrategy>(this, WorldSettings->LoadBalanceStrategy);
		AddStrategyForLayer(SpatialConstants::DefaultLayer, DefaultLBStrategy);
	}
}

void ULayeredLBStrategy::SetLocalVirtualWorkerId(VirtualWorkerId InLocalVirtualWorkerId)
{
	LocalVirtualWorkerId = InLocalVirtualWorkerId;
	for (const auto& Elem : LayerNameToLBStrategy)
	{
		Elem.Value->SetLocalVirtualWorkerId(InLocalVirtualWorkerId);
	}
}

TSet<VirtualWorkerId> ULayeredLBStrategy::GetVirtualWorkerIds() const
{
	return TSet<VirtualWorkerId>(VirtualWorkerIds);
}

bool ULayeredLBStrategy::ShouldHaveAuthority(const AActor& Actor) const
{
	if (!IsReady())
	{
		UE_LOG(LogLayeredLBStrategy, Warning, TEXT("LayeredLBStrategy not ready to relinquish authority for Actor %s."), *AActor::GetDebugName(&Actor));
		return false;
	}

	const FName& LayerName = GetLayerNameForActor(Actor);
	if (!LayerNameToLBStrategy.Contains(LayerName))
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("LayeredLBStrategy doesn't have a LBStrategy for Actor %s which is in Layer %s."), *AActor::GetDebugName(&Actor), *LayerName.ToString());
		return false;
	}

	// If this worker is not responsible for the Actor's layer, just return false.
	if (VirtualWorkerIdToLayerName.Contains(LocalVirtualWorkerId) && VirtualWorkerIdToLayerName[LocalVirtualWorkerId] != LayerName)
	{
		return false;
	}

	return LayerNameToLBStrategy[LayerName]->ShouldHaveAuthority(Actor);
}

VirtualWorkerId ULayeredLBStrategy::WhoShouldHaveAuthority(const AActor& Actor) const
{
	if (!IsReady())
	{
		UE_LOG(LogLayeredLBStrategy, Warning, TEXT("LayeredLBStrategy not ready to decide on authority for Actor %s."), *AActor::GetDebugName(&Actor));
		return SpatialConstants::INVALID_VIRTUAL_WORKER_ID;
	}

	const FName& LayerName = GetLayerNameForActor(Actor);
	if (!LayerNameToLBStrategy.Contains(LayerName))
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("LayeredLBStrategy doesn't have a LBStrategy for Actor %s which is in Layer %s."), *AActor::GetDebugName(&Actor), *LayerName.ToString());
		return SpatialConstants::INVALID_VIRTUAL_WORKER_ID;
	}

	const VirtualWorkerId ReturnedWorkerId = LayerNameToLBStrategy[LayerName]->WhoShouldHaveAuthority(Actor);

	UE_LOG(LogLayeredLBStrategy, Log, TEXT("LayeredLBStrategy returning virtual worker id %d for Actor %s."), ReturnedWorkerId, *AActor::GetDebugName(&Actor));
	return ReturnedWorkerId;
}

SpatialGDK::QueryConstraint ULayeredLBStrategy::GetWorkerInterestQueryConstraint() const
{
	check(IsReady());
	if (!VirtualWorkerIdToLayerName.Contains(LocalVirtualWorkerId))
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("LayeredLBStrategy doesn't have a LBStrategy for worker %d."), LocalVirtualWorkerId);
		SpatialGDK::QueryConstraint Constraint;
		Constraint.ComponentConstraint = 0;
		return Constraint;
	}
	else
	{
		const FName& LayerName = VirtualWorkerIdToLayerName[LocalVirtualWorkerId];
		check(LayerNameToLBStrategy.Contains(LayerName));
		return LayerNameToLBStrategy[LayerName]->GetWorkerInterestQueryConstraint();
	}
}

FVector ULayeredLBStrategy::GetWorkerEntityPosition() const
{
	check(IsReady());
	if (!VirtualWorkerIdToLayerName.Contains(LocalVirtualWorkerId))
	{
		UE_LOG(LogLayeredLBStrategy, Error, TEXT("LayeredLBStrategy doesn't have a LBStrategy for worker %d."), LocalVirtualWorkerId);
		return FVector{ 0.f, 0.f, 0.f };
	}
	else
	{
		const FName& LayerName = VirtualWorkerIdToLayerName[LocalVirtualWorkerId];
		check(LayerNameToLBStrategy.Contains(LayerName));
		return LayerNameToLBStrategy[LayerName]->GetWorkerEntityPosition();
	}
}

uint32 ULayeredLBStrategy::GetMinimumRequiredWorkers() const
{
	// The MinimumRequiredWorkers for this strategy is a sum of the required workers for each of the wrapped strategies.
	uint32 MinimumRequiredWorkers = 0;
	for (const auto& Elem : LayerNameToLBStrategy)
	{
		MinimumRequiredWorkers += Elem.Value->GetMinimumRequiredWorkers();
	}

	UE_LOG(LogLayeredLBStrategy, Verbose, TEXT("LayeredLBStrategy needs %d workers to support all layer strategies."), MinimumRequiredWorkers);
	return MinimumRequiredWorkers;
}

void ULayeredLBStrategy::SetVirtualWorkerIds(const VirtualWorkerId& FirstVirtualWorkerId, const VirtualWorkerId& LastVirtualWorkerId)
{
	// If the LayeredLBStrategy wraps { SingletonStrategy, 2x2 grid, Singleton } and is given IDs 1 through 6 it will assign:
	// Singleton : 1
	// Grid : 2 - 5
	// Singleton: 6
	VirtualWorkerId NextWorkerIdToAssign = FirstVirtualWorkerId;
	for (const auto& Elem : LayerNameToLBStrategy)
	{
		UAbstractLBStrategy* LBStrategy = Elem.Value;
		VirtualWorkerId MinimumRequiredWorkers = LBStrategy->GetMinimumRequiredWorkers();

		VirtualWorkerId LastVirtualWorkerIdToAssign = NextWorkerIdToAssign + MinimumRequiredWorkers - 1;
		if (LastVirtualWorkerIdToAssign > LastVirtualWorkerId)
		{
			UE_LOG(LogLayeredLBStrategy, Error, TEXT("LayeredLBStrategy was not given enough VirtualWorkerIds to meet the demands of the layer strategies."));
			return;
		}
		UE_LOG(LogLayeredLBStrategy, Log, TEXT("LayeredLBStrategy assigning VirtualWorkerIds %d to %d to Layer %s"), NextWorkerIdToAssign, LastVirtualWorkerIdToAssign, *Elem.Key.ToString());
		LBStrategy->SetVirtualWorkerIds(NextWorkerIdToAssign, LastVirtualWorkerIdToAssign);

		for (VirtualWorkerId id = NextWorkerIdToAssign; id <= LastVirtualWorkerIdToAssign; id++)
		{
			VirtualWorkerIdToLayerName.Add(id, Elem.Key);
		}

		NextWorkerIdToAssign += MinimumRequiredWorkers;
	}

	// Keep a copy of the VirtualWorkerIds. This is temporary and will be removed in the next PR.
	for (VirtualWorkerId CurrentVirtualWorkerId = FirstVirtualWorkerId; CurrentVirtualWorkerId <= LastVirtualWorkerId; CurrentVirtualWorkerId++)
	{
		VirtualWorkerIds.Add(CurrentVirtualWorkerId);
	}
}

FName ULayeredLBStrategy::GetLayerNameForClass(const TSubclassOf<AActor> Class) const
{
	if (Class == nullptr)
	{
		return NAME_None;
	}

	UClass* FoundClass = Class;
	TSoftClassPtr<AActor> ClassPtr = TSoftClassPtr<AActor>(FoundClass);

	while (FoundClass != nullptr && FoundClass->IsChildOf(AActor::StaticClass()))
	{
		if (const FName* Layer = ClassPathToLayer.Find(ClassPtr))
		{
			FName LayerHolder = *Layer;
			if (FoundClass != Class)
			{
				ClassPathToLayer.Add(TSoftClassPtr<AActor>(Class), LayerHolder);
			}
			return LayerHolder;
		}

		FoundClass = FoundClass->GetSuperClass();
		ClassPtr = TSoftClassPtr<AActor>(FoundClass);
	}

	// No mapping found so set and return default actor group.
	ClassPathToLayer.Add(TSoftClassPtr<AActor>(Class), SpatialConstants::DefaultLayer);
	return SpatialConstants::DefaultLayer;
}

bool ULayeredLBStrategy::IsSameWorkerType(const AActor* ActorA, const AActor* ActorB) const
{
	if (ActorA == nullptr || ActorB == nullptr)
	{
		return false;
	}
	return GetLayerNameForClass(ActorA->GetClass()) == GetLayerNameForClass(ActorB->GetClass());
}

// Note: this is returning whether this is one of the workers which can simulate the layer. If there are
// multiple workers simulating the layer, there's no concept of owner. This is left over from the way
// ActorGroups could own entities, and will be removed in the future.
bool ULayeredLBStrategy::IsLayerOwner(const FName& Layer) const
{
	return *VirtualWorkerIdToLayerName.Find(LocalVirtualWorkerId) == Layer;
}

FName ULayeredLBStrategy::GetLayerNameForActor(const AActor& Actor) const
{
	return GetLayerNameForClass(Actor.GetClass());
}

void ULayeredLBStrategy::AddStrategyForLayer(const FName& LayerName, UAbstractLBStrategy* LBStrategy)
{
	LBStrategy->AddToRoot();
	LayerNameToLBStrategy.Add(LayerName, LBStrategy);
	LayerNameToLBStrategy[LayerName]->Init();
}