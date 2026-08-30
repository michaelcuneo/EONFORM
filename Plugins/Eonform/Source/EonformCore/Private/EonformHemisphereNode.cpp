#include "EonformHemisphereNode.h"

#include "EonformGridDomain.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor HemisphereTerrainPort(FName Name)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}
	FEonformTerrainParameterDescriptor HemisphereNumber(FName Name, const TCHAR *Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		return P;
	}
	bool EvaluateHemisphereNode(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 4097);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.001f, 10.0f);
		const float HeightAmount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), -4.0f, 4.0f);
		const float OffsetX = static_cast<float>(Node.GetNumber(TEXT("X"), 0.0));
		const float OffsetY = static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0));
		const float Flatten = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Flatten"), 0.0)), 0.0f, 1.0f);
		const double Half = static_cast<double>(WorldSize) * 0.5;
		const FEonformGridDomain Domain = FEonformGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-Half, -Half), FVector2d(Half, Half));
		if (!Domain.IsValid())
		{
			Error = TEXT("Hemisphere produced an invalid grid domain.");
			return false;
		}
		FEonformFieldDescriptor D;
		D.Name = EonformTerrainFieldNames::Height;
		D.Unit = EEonformFieldUnit::Normalized;
		D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, D);
		const float Radius = FMath::Max(WorldSize * 0.5f * Scale, 1.0f);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d W = Domain.InteriorSampleToWorld(X, Y);
				const float NX = (static_cast<float>(W.X) - OffsetX) / Radius;
				const float NY = (static_cast<float>(W.Y) - OffsetY) / Radius;
				const float R2 = NX * NX + NY * NY;
				float H = R2 < 1.0f ? FMath::Sqrt(FMath::Max(1.0f - R2, 0.0f)) : 0.0f;
				const float Plateau = FMath::Lerp(1.0f, 0.45f, Flatten);
				if (Flatten > UE_SMALL_NUMBER && H > Plateau)
					H = FMath::Lerp(H, Plateau, Flatten);
				Field.AtInterior(X, Y) = FMath::Clamp(H * HeightAmount, -1.0f, 1.0f);
			}
		}
		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Field)))
		{
			Error = TEXT("Hemisphere could not publish Height.");
			return false;
		}
		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Hemisphere produced invalid terrain.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterEonformHemisphereNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Hemisphere;
	Descriptor.DisplayName = TEXT("Hemisphere");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates a hemispherical gradient for domes, mounds, and masks.");
	Descriptor.Outputs.Add(HemisphereTerrainPort(TEXT("Out")));
	Descriptor.Parameters.Add(HemisphereNumber(TEXT("Scale"), TEXT("Scale"), 0.5, 0.001, 10.0));
	Descriptor.Parameters.Add(HemisphereNumber(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0));
	Descriptor.Parameters.Add(HemisphereNumber(TEXT("X"), TEXT("X"), 0.0, -1000000.0, 1000000.0));
	Descriptor.Parameters.Add(HemisphereNumber(TEXT("Y"), TEXT("Y"), 0.0, -1000000.0, 1000000.0));
	Descriptor.Parameters.Add(HemisphereNumber(TEXT("Flatten"), TEXT("Flatten"), 0.0, 0.0, 1.0));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Hemisphere, EvaluateHemisphereNode);
}
