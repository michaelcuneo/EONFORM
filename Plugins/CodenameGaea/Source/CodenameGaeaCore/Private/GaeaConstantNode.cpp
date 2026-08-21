#include "GaeaConstantNode.h"

#include "GaeaColorField.h"
#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor ConstantAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor ConstantNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options = {})
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor ConstantNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	uint32 ConstantNoiseHash(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7feb352dU;
		Value ^= Value >> 15;
		Value *= 0x846ca68bU;
		Value ^= Value >> 16;
		return Value;
	}

	FLinearColor ConstantParseColor(FName ColorName)
	{
		FString Text = ColorName.ToString();
		Text.RemoveFromStart(TEXT("#"));
		if (Text.Len() != 6 && Text.Len() != 8) return FLinearColor::White;

		const FColor SRGB = FColor::FromHex(Text);
		return FLinearColor::FromSRGBColor(SRGB);
	}

	bool EvaluateConstantNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const FName OutputMode = Node.GetName(TEXT("Output"), TEXT("Height"));

		const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(Resolution, Resolution),
			FVector2d(-HalfWorldSize, -HalfWorldSize),
			FVector2d(HalfWorldSize, HalfWorldSize));
		if (!Domain.IsValid())
		{
			Error = TEXT("Constant produced an invalid grid domain.");
			return false;
		}

		if (OutputMode == TEXT("Color"))
		{
			FGaeaColorField ColorField;
			ColorField.Initialize(Domain, ConstantParseColor(Node.GetName(TEXT("Color"), TEXT("#FFFFFF"))));
			if (!ColorField.IsValid())
			{
				Error = TEXT("Constant produced an invalid color field.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeColor(MoveTemp(ColorField)));
			return true;
		}

		if (OutputMode != TEXT("Height") && OutputMode != TEXT("Noise"))
		{
			Error = TEXT("Constant Output must be Height, Color, or Noise.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField HeightField;
		HeightField.Initialize(Domain, Descriptor);

		const float ConstantHeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 0.5)), -1.0f, 1.0f);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				float Value = ConstantHeight;
				if (OutputMode == TEXT("Noise"))
				{
					uint32 Hash = static_cast<uint32>(X) * 0x9e3779b9U;
					Hash ^= static_cast<uint32>(Y) * 0x85ebca6bU;
					const float Noise = static_cast<float>(ConstantNoiseHash(Hash) & 0x00ffffffU) / static_cast<float>(0x01000000U);
					Value = FMath::Clamp(ConstantHeight + (Noise - 0.5f) * 0.1f, -1.0f, 1.0f);
				}
				HeightField.AtInterior(X, Y) = Value;
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(HeightField)))
		{
			Error = TEXT("Constant could not publish its Height field.");
			return false;
		}
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Constant produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaConstantNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Constant;
	Descriptor.DisplayName = TEXT("Constant");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Creates a flat heightfield, flat color map, or simple noise reference layer.");
	Descriptor.Outputs.Add(ConstantAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ConstantNameParameter(TEXT("Output"), TEXT("Output"), TEXT("Height"), { TEXT("Height"), TEXT("Color"), TEXT("Noise") }));
	Descriptor.Parameters.Add(ConstantNumberParameter(TEXT("Height"), TEXT("Height"), 0.5, -1.0, 1.0));
	Descriptor.Parameters.Add(ConstantNameParameter(TEXT("Color"), TEXT("Color"), TEXT("#FFFFFF")));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Constant, EvaluateConstantNode);
}
