#include "GaeaConeNode.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor ConeTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor ConeNumberParameter(
		FName Name,
		const TCHAR* DisplayName,
		double DefaultValue,
		double Minimum,
		double Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	bool EvaluateConeNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);

		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.001f, 10.0f);
		const float HeightAmount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), -4.0f, 4.0f);
		const float OffsetX = static_cast<float>(Node.GetNumber(TEXT("X"), 0.0));
		const float OffsetY = static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0));

		const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(Resolution, Resolution),
			FVector2d(-HalfWorldSize, -HalfWorldSize),
			FVector2d(HalfWorldSize, HalfWorldSize));
		if (!Domain.IsValid())
		{
			Error = TEXT("Cone produced an invalid grid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField HeightField;
		HeightField.Initialize(Domain, Descriptor);

		const float Radius = FMath::Max(WorldSize * 0.5f * Scale, 1.0f);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d World = Domain.InteriorSampleToWorld(X, Y);
				const float DX = static_cast<float>(World.X) - OffsetX;
				const float DY = static_cast<float>(World.Y) - OffsetY;
				const float Distance = FMath::Sqrt(DX * DX + DY * DY);
				const float Cone = FMath::Clamp(1.0f - Distance / Radius, 0.0f, 1.0f);
				HeightField.AtInterior(X, Y) = FMath::Clamp(Cone * HeightAmount, -1.0f, 1.0f);
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(HeightField)))
		{
			Error = TEXT("Cone could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Cone produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaConeNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Cone;
	Descriptor.DisplayName = TEXT("Cone");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates a simple conical gradient for primitive-shape and mask workflows.");
	Descriptor.Outputs.Add(ConeTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ConeNumberParameter(TEXT("Scale"), TEXT("Scale"), 0.5, 0.001, 10.0));
	Descriptor.Parameters.Add(ConeNumberParameter(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0));
	Descriptor.Parameters.Add(ConeNumberParameter(TEXT("X"), TEXT("X"), 0.0, -1000000.0, 1000000.0));
	Descriptor.Parameters.Add(ConeNumberParameter(TEXT("Y"), TEXT("Y"), 0.0, -1000000.0, 1000000.0));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Cone, EvaluateConeNode);
}
