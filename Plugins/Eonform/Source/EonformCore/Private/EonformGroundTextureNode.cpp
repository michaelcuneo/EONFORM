#include "EonformGroundTextureNode.h"

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
	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Name; P.DefaultName = Default; for (const FName V : Options) P.NameOptions.Add(V); return P;
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
	bool EvaluateGroundTexture(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid()) { Error = TEXT("GroundTexture requires Terrain input."); return false; }
		const FEonformScalarField* Source = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Source || !Source->IsValid()) { Error = TEXT("GroundTexture terrain input has no valid Height."); return false; }
		const FName Method = Node.GetName(TEXT("Method"), TEXT("Rough"));
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.5)), 0.0f, 2.0f);
		const float Coverage = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Coverage"), 0.65)), 0.0f, 1.0f);
		const float Density = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"), 0.5)), 0.01f, 2.0f);
		FEonformScalarField Result = *Source;
		const int32 W = Result.Domain.Dimensions.X, H = Result.Domain.Dimensions.Y;
		const float Frequency = FMath::Lerp(35.0f, 180.0f, Density * 0.5f);
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			const float U = static_cast<float>(X) / FMath::Max(W - 1, 1), V = static_cast<float>(Y) / FMath::Max(H - 1, 1);
			const float CoverNoise = 0.5f + 0.5f * Fbm(U, V, Frequency * 0.16f, 3, 0.55f, 4751, 0x91u);
			const float Mask = FMath::SmoothStep(1.0f - Coverage, FMath::Min(1.0f, 1.08f - Coverage * 0.25f), CoverNoise);
			const float N1 = Fbm(U, V, Frequency, 4, 0.52f, 7127, 0xB1u);
			const float N2 = Fbm(U, V, Frequency * 2.9f, 3, 0.48f, 9419, 0xC1u);
			float Detail = N1 * 0.7f + N2 * 0.3f;
			if (Method == TEXT("Rocky")) Detail = (1.0f - FMath::Abs(Detail)) * 2.0f - 1.0f;
			else if (Method == TEXT("Harsh")) Detail = FMath::Sign(Detail) * FMath::Pow(FMath::Abs(Detail), 0.55f);
			Result.AtInterior(X, Y) = FMath::Clamp(Source->AtInterior(X, Y) + Detail * Mask * Strength * 0.035f, -1.0f, 1.0f);
		}
		Result.Descriptor.Name = EonformTerrainFieldNames::Height;
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Result))) { Error = TEXT("GroundTexture could not publish Height."); return false; }
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		return true;
	}
}

void RegisterEonformGroundTextureNode()
{
	FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::GroundTexture; D.DisplayName = TEXT("GroundTexture"); D.Category = TEXT("Surface"); D.Description = TEXT("Adds superficial Harsh, Rocky or Rough detail with controllable strength, coverage and density.");
	D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input"))); D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
	D.Parameters = { Choice(TEXT("Method"), TEXT("Method"), TEXT("Rough"), { TEXT("Harsh"), TEXT("Rocky"), TEXT("Rough") }), Num(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 2.0), Num(TEXT("Coverage"), TEXT("Coverage"), 0.65, 0.0, 1.0), Num(TEXT("Density"), TEXT("Density"), 0.5, 0.01, 2.0) };
	FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateGroundTexture);
}
