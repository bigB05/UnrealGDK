// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "SpatialFunctionalTest.h"

#include "EventTracingTest.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEventTracingTest, Log, All);

namespace worker
{
namespace c
{
struct Trace_Item;
} // namespace c
} // namespace worker

UCLASS()
class SPATIALGDKFUNCTIONALTESTS_API AEventTracingTest : public ASpatialFunctionalTest
{
	GENERATED_BODY()

public:
	AEventTracingTest();

	virtual void PrepareTest() override;

protected:
	FName ReceiveOpEventName = "worker.receive_op";
	FName SendPropertyUpdatesEventName = "unreal_gdk.send_property_updates";
	FName ReceivePropertyUpdateEventName = "unreal_gdk.receive_property_update";
	FName SendRPCEventName = "unreal_gdk.send_rpc";
	FName ProcessRPCEventName = "unreal_gdk.process_rpc";
	FName ComponentUpdateEventName = "unreal_gdk.component_update";
	FName MergeComponentUpdateEventName = "unreal_gdk.merge_component_update";

	FWorkerDefinition WorkerDefinition;
	TArray<FName> FilterEventNames;

	float TestTime = 10.0f;

	TMap<FString, FName> TraceEvents;
	TMap<FString, TArray<FString>> TraceSpans;

	bool CheckEventTraceCause(const FString& SpanIdString, const TArray<FName>& CauseEventNames, int MinimumCauses = 1);

	virtual void FinishEventTraceTest();

private:
	FDateTime TestStartTime;

	void StartEventTracingTest();
	void WaitForTestToEnd();
	void GatherData();
	void GatherDataFromFile(const FString& FilePath);
};