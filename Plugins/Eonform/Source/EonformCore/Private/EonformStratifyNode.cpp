#include "EonformStratifyNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* Label)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = TEXT("Terrain"); return P;
	}
	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}
	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Integer; P.DefaultInteger = Default; P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); return P;
	}
	uint32 Hash(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16; return X;
	}
	float Hash01(int32 X, int32 Y, int32 Seed, uint32 Salt)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U; H ^= static_cast<uint32>(Y) * 0x85ebca6bU; H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U; H ^= Salt;
		return static_cast<float>(Hash(H) & 0x00ffffffU) / 16777215.0f;
	}
	float SmoothNoise(float X, float Y, int32 Seed, uint32 Salt)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float FX = X - X0, FY = Y - Y0;
		const float SX = FX * FX * (3.0f - 2.0f * FX), SY = FY * FY * (3.0f - 2.0f * FY);
		const float A = FMath::Lerp(Hash01(X0, Y0, Seed, Salt), Hash01(X0 + 1, Y0, Seed, Salt), SX);
		const float B = FMath::Lerp(Hash01(X0, Y0 + 1, Seed, Salt), Hash01(X0 + 1, Y0 + 1, Seed, Salt), SX);
		return FMath::Lerp(A, B, SY) * 2.0f - 1.0f;
	}
	float Fbm(float X, float Y, float Frequency, int32 Octaves, float Roughness, int32 Seed, uint32 Salt)
	{
		float Sum = 0.0f, Amp = 1.0f, Weight = 0.0f;
		for (int32 I = 0; I < Octaves; ++I)
		{
			Sum += SmoothNoise(X * Frequency, Y * Frequency, Seed + I * 313, Salt + I * 3571u) * Amp;
			Weight += Amp; Frequency *= 2.01f; Amp *= Roughness;
		}
		return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
	}
	bool EvaluateStratify(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid()) { Error = TEXT("Stratify requires Terrain input."); return false; }
		const FEonformScalarField* Source = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Source || !Source->IsValid()) { Error = TEXT("Stratify terrain input has no valid Height."); return false; }
		const float Spacing = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Spacing"), 0.5)), 0.02f, 4.0f);
		const int32 Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 8);
		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 0.5)), 0.0f, 1.5f);
		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"), 0.5)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Tilt = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("TiltAmount"), 0.0)), -1.0f, 1.0f);
		const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		FEonformScalarField Result = *Source;
		const int32 W = Result.Domain.Dimensions.X, H = Result.Domain.Dimensions.Y;
		const float LayerFrequency = FMath::Lerp(48.0f, 6.0f, FMath::Clamp(Spacing / 4.0f, 0.0f, 1.0f));
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			const float U = static_cast<float>(X) / FMath::Max(W - 1, 1) - 0.5f, V = static_cast<float>(Y) / FMath::Max(H - 1, 1) - 0.5f;
			const float Directional = U * FMath::Cos(Direction) + V * FMath::Sin(Direction);
			const float Breakup = Fbm(U, V, 3.5f, Octaves, 0.56f, Seed, 0xD1u);
			const float Zone = FMath::SmoothStep(-0.15f, 0.32f, Fbm(U, V, 1.7f, 3, 0.55f, Seed + 503, 0xE1u));
			const float H01 = FMath::Clamp(Source->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
			const float Phase = (H01 + Directional * Tilt * 0.24f + Breakup * 0.035f) * LayerFrequency;
			float Layer = 1.0f - 2.0f * FMath::Abs(FMath::Frac(Phase) - 0.5f);
			Layer = FMath::Pow(FMath::Clamp(Layer, 0.0f, 1.0f), FMath::Lerp(0.65f, 4.5f, Shape));
			const float Delta = (Layer - 0.42f) * Intensity * Zone * 0.055f;
			Result.AtInterior(X, Y) = FMath::Clamp(Source->AtInterior(X, Y) + Delta, -1.0f, 1.0f);
		}
		Result.Descriptor.Name = EonformTerrainFieldNames::Height;
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Result))) { Error = TEXT("Stratify could not publish Height."); return false; }
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		return true;
	}
}

void RegisterEonformStratifyNode()
{
	FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::Stratify; D.DisplayName = TEXT("Stratify"); D.Category = TEXT("Surface"); D.Description = TEXT("Creates broken non-linear rock strata with independent local zones and geological tilt.");
	D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input"))); D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
	D.Parameters = { Num(TEXT("Spacing"), TEXT("Spacing"), 0.5, 0.02, 4.0), Int(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 8), Num(TEXT("Intensity"), TEXT("Intensity"), 0.5, 0.0, 1.5), Num(TEXT("Shape"), TEXT("Shape"), 0.5, 0.0, 1.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647), Num(TEXT("TiltAmount"), TEXT("Tilt Amount"), 0.0, -1.0, 1.0), Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0) };
	FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateStratify);
}
