#pragma once

#include "CoreMinimal.h"

enum class EGaeaTerrainParameterType : uint8
{
	Number,
	Integer,
	Boolean,
	Name
};

struct CODENAMEGAEACORE_API FGaeaTerrainPortDescriptor
{
	FName Name = NAME_None;
	FName DataType = NAME_None;
};

struct CODENAMEGAEACORE_API FGaeaTerrainParameterDescriptor
{
	FName Name = NAME_None;
	FString DisplayName;
	EGaeaTerrainParameterType Type = EGaeaTerrainParameterType::Number;

	double DefaultNumber = 0.0;
	int64 DefaultInteger = 0;
	bool DefaultBoolean = false;
	FName DefaultName = NAME_None;

	bool bHasMinimum = false;
	double Minimum = 0.0;
	bool bHasMaximum = false;
	double Maximum = 0.0;
};

struct CODENAMEGAEACORE_API FGaeaTerrainNodeDescriptor
{
	FName Type = NAME_None;
	FString DisplayName;
	FString Category;
	FString Description;
	bool bHiddenInGraph = false;
	TArray<FGaeaTerrainPortDescriptor> Inputs;
	TArray<FGaeaTerrainPortDescriptor> Outputs;
	TArray<FGaeaTerrainParameterDescriptor> Parameters;
};

/**
 * Runtime-safe metadata describing terrain node types.
 *
 * The professional editor and constrained game-facing authoring layers both
 * consume these descriptors, while execution remains owned by the runtime
 * evaluator registry.
 */
class CODENAMEGAEACORE_API FGaeaTerrainNodeDescriptorRegistry
{
public:
	static void Register(const FGaeaTerrainNodeDescriptor& Descriptor);
	static void RegisterBuiltIns();
	static bool Get(FName NodeType, FGaeaTerrainNodeDescriptor& OutDescriptor);
	static void GetAll(TArray<FGaeaTerrainNodeDescriptor>& OutDescriptors);
	static void Reset();
};
