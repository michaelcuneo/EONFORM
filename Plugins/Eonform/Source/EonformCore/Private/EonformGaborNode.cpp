#include "EonformGaborNode.h"

#include "EonformGridDomain.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor GaborTerrainPort(FName Name)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}
	FEonformTerrainParameterDescriptor GaborNumber(FName Name, const TCHAR *Label, double Default, double Min, double Max)
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
	FEonformTerrainParameterDescriptor GaborInteger(FName Name, const TCHAR *Label, int64 Default)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		return P;
	}
	bool EvaluateGaborNode(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 4097);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const float Size = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Size"), 1.0)), 0.001f);
		const float Entropy = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Entropy"), 0.4)), 0.0f, 1.0f);
		const float Anisotropy = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Anisotropy"), 0.5)), 0.0f, 1.0f);
		const float Azimuth = static_cast<float>(Node.GetNumber(TEXT("Azimuth"), 0.0));
		const float Gain = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Gain"), 1.0)), 0.0f, 4.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const double Half = static_cast<double>(WorldSize) * 0.5;
		const FEonformGridDomain Domain = FEonformGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-Half, -Half), FVector2d(Half, Half));
		if (!Domain.IsValid())
		{
			Error = TEXT("Gabor produced an invalid grid domain.");
			return false;
		}
		FEonformFieldDescriptor D;
		D.Name = EonformTerrainFieldNames::Height;
		D.Unit = EEonformFieldUnit::Normalized;
		D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, D);
		const float A = FMath::DegreesToRadians(Azimuth);
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		const FVector2D Side(-Dir.Y, Dir.X);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d W = Domain.InteriorSampleToWorld(X, Y);
				const FVector2D P(static_cast<float>(W.X / WorldSize), static_cast<float>(W.Y / WorldSize));
				const float Along = FVector2D::DotProduct(P, Dir) * 18.0f / Size;
				const float Across = FVector2D::DotProduct(P, Side) * FMath::Lerp(18.0f, 3.0f, Anisotropy) / Size;
				const float RandomPhase = FMath::PerlinNoise2D(FVector2D(P.X * 8.0f + Seed * 0.017f, P.Y * 8.0f)) * Entropy * PI;
				const float Envelope = 0.5f + 0.5f * FMath::PerlinNoise2D(FVector2D(Across * 0.4f, Along * 0.13f + Seed * 0.01f));
				const float Wave = 0.5f + 0.5f * FMath::Cos(Along * 2.0f * PI + RandomPhase);
				Field.AtInterior(X, Y) = FMath::Clamp(FMath::Lerp(Wave, Wave * Envelope, Entropy) * Gain, 0.0f, 1.0f);
			}
		}
		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Field)))
		{
			Error = TEXT("Gabor could not publish Height.");
			return false;
		}
		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Gabor produced invalid terrain.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterEonformGaborNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Gabor;
	Descriptor.DisplayName = TEXT("Gabor");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates directional, pattern-friendly Gabor noise.");
	Descriptor.Outputs.Add(GaborTerrainPort(TEXT("Out")));
	Descriptor.Parameters.Add(GaborNumber(TEXT("Size"), TEXT("Size"), 1.0, 0.01, 10.0));
	Descriptor.Parameters.Add(GaborNumber(TEXT("Entropy"), TEXT("Entropy"), 0.4, 0.0, 1.0));
	Descriptor.Parameters.Add(GaborNumber(TEXT("Anisotropy"), TEXT("Anisotropy"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(GaborNumber(TEXT("Azimuth"), TEXT("Azimuth"), 0.0, -360.0, 360.0));
	Descriptor.Parameters.Add(GaborNumber(TEXT("Gain"), TEXT("Gain"), 1.0, 0.0, 4.0));
	Descriptor.Parameters.Add(GaborInteger(TEXT("Seed"), TEXT("Seed"), 1337));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Gabor, EvaluateGaborNode);
}
