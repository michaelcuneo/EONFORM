#include "EonformReferenceFidelityExtendedNodes.h"

#include "EonformGridDomain.h"
#include "EonformScalarField.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformReferenceFidelityExtended
{
	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = Type; return P;
	}
	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}
	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Integer; P.DefaultInteger = Default; P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); return P;
	}
	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Name; P.DefaultName = Default; for (const FName V : Options) P.NameOptions.Add(V); return P;
	}

	uint32 Hash(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16; return X;
	}
	float Hash01(int32 X, int32 Y, int32 Seed, uint32 Salt = 0)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
		H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
		H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U;
		H ^= Salt;
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
			Weight += Amp;
			Frequency *= 2.01f;
			Amp *= Roughness;
		}
		return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
	}

	const FEonformTerrainValue* Input(const FEonformTerrainNodeInputs& Inputs, FName Name)
	{
		const FEonformTerrainValue* const* P = Inputs.Find(Name); return P ? *P : nullptr;
	}
	const FEonformScalarField* AsField(const FEonformTerrainValue* V)
	{
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EEonformTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EEonformTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		return nullptr;
	}
	bool PublishLike(const FEonformTerrainValue& Prototype, FEonformScalarField&& Field, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		if (Prototype.Type == EEonformTerrainValueType::ScalarField)
		{
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Field))); return true;
		}
		if (Prototype.Type == EEonformTerrainValueType::Terrain)
		{
			Field.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = Prototype.TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(Field))) { Error = TEXT("Surface node could not publish Height."); return false; }
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Prototype.HeightScale)); return true;
		}
		Error = TEXT("Surface node received unsupported input type."); return false;
	}

	FEonformGridDomain BuildSourceDomain(const FEonformTerrainEvaluationContext& Context)
	{
		const int32 RX = FMath::Clamp(Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257, 2, 4097);
		const int32 RY = FMath::Clamp(Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : RX, 2, 4097);
		double W = 100000.0, H = 100000.0;
		if (Context.PhysicalMetrics.HasWorldDimensions()) { W = Context.PhysicalMetrics.WorldWidthMeters * 100.0; H = Context.PhysicalMetrics.WorldDepthMeters * 100.0; }
		return FEonformGridDomain::Make(FIntPoint(RX, RY), FVector2d(-W * 0.5, -H * 0.5), FVector2d(W * 0.5, H * 0.5));
	}
	float SourceHeightScale(const FEonformTerrainEvaluationContext& Context)
	{
		return Context.PhysicalMetrics.HasElevationScale() ? static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0) : FMath::Max(Context.HeightScale, 1.0f);
	}
	FEonformScalarField MakeHeight(const FEonformGridDomain& Domain)
	{
		FEonformFieldDescriptor D; D.Name = EonformTerrainFieldNames::Height; D.Unit = EEonformFieldUnit::Normalized; D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField F; F.Initialize(Domain, D, 0.0f); return F;
	}

	float RockStamp(float DX, float DY, float Radius, FName Style, float Variety)
	{
		const float AX = FMath::Abs(DX) / FMath::Max(Radius, 0.001f), AY = FMath::Abs(DY) / FMath::Max(Radius, 0.001f);
		float D = FMath::Sqrt(AX * AX + AY * AY);
		if (Style == TEXT("B")) D = FMath::Max(AX, AY) * FMath::Lerp(0.92f, 1.18f, Variety);
		else if (Style == TEXT("C")) D = FMath::Pow(FMath::Pow(AX, 1.45f) + FMath::Pow(AY, 1.45f), 1.0f / 1.45f);
		if (D >= 1.0f) return 0.0f;
		float V = FMath::Pow(1.0f - D * D, Style == TEXT("A") ? 1.45f : (Style == TEXT("B") ? 0.78f : 1.02f));
		if (Style == TEXT("C")) V *= 0.78f + 0.22f * FMath::Cos((DX + DY) / FMath::Max(Radius, 0.001f) * PI * 2.0f);
		return FMath::Max(V, 0.0f);
	}

	bool EvaluateRockNoise(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs&, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformGridDomain Domain = BuildSourceDomain(Context);
		if (!Domain.IsValid()) { Error = TEXT("RockNoise produced invalid domain."); return false; }
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.02f, 4.0f);
		const float Variety = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Variety"), 0.5)), 0.0f, 1.0f);
		const int32 Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 8);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const FName Style = Node.GetName(TEXT("Style"), TEXT("A"));
		FEonformScalarField Height = MakeHeight(Domain);
		const int32 W = Domain.Dimensions.X, H = Domain.Dimensions.Y;
		for (int32 O = 0; O < Octaves; ++O)
		{
			const float Cells = FMath::Lerp(7.0f, 28.0f, 1.0f / FMath::Max(Size, 0.02f)) * FMath::Pow(2.0f, O);
			const float Amp = FMath::Pow(0.62f, O);
			for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
			{
				const float U = static_cast<float>(X) / FMath::Max(W - 1, 1) * Cells;
				const float V = static_cast<float>(Y) / FMath::Max(H - 1, 1) * Cells;
				const int32 BX = FMath::FloorToInt(U), BY = FMath::FloorToInt(V);
				float Stamp = 0.0f;
				for (int32 CY = BY - 1; CY <= BY + 1; ++CY) for (int32 CX = BX - 1; CX <= BX + 1; ++CX)
				{
					const float Chance = Hash01(CX, CY, Seed + O * 991, 0x27u);
					if (Chance < FMath::Lerp(0.58f, 0.28f, Variety)) continue;
					const float PX = CX + 0.5f + (Hash01(CX, CY, Seed, 0x31u) - 0.5f) * 0.78f;
					const float PY = CY + 0.5f + (Hash01(CY, CX, Seed, 0x47u) - 0.5f) * 0.78f;
					const float Radius = FMath::Lerp(0.16f, 0.46f, Hash01(CX, CY, Seed + O * 101, 0x59u)) * FMath::Lerp(0.72f, 1.32f, Variety);
					Stamp = FMath::Max(Stamp, RockStamp(U - PX, V - PY, Radius, Style, Variety));
				}
				Height.AtInterior(X, Y) = FMath::Max(Height.AtInterior(X, Y), Stamp * Amp);
			}
		}
		FEonformTerrainDataset Dataset; if (!Dataset.SetScalarField(MoveTemp(Height))) { Error = TEXT("RockNoise could not publish Height."); return false; }
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), SourceHeightScale(Context)));
		return true;
	}

	bool EvaluateGroundTexture(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* V = Input(Inputs, TEXT("Terrain")); const FEonformScalarField* Source = AsField(V);
		if (!V || V->Type != EEonformTerrainValueType::Terrain || !Source) { Error = TEXT("GroundTexture requires Terrain input."); return false; }
		const FName Method = Node.GetName(TEXT("Method"), TEXT("Rough"));
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.5)), 0.0f, 2.0f);
		const float Coverage = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Coverage"), 0.65)), 0.0f, 1.0f);
		const float Density = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"), 0.5)), 0.01f, 2.0f);
		FEonformScalarField R = *Source;
		const int32 W = R.Domain.Dimensions.X, H = R.Domain.Dimensions.Y;
		const float Frequency = FMath::Lerp(35.0f, 180.0f, Density * 0.5f);
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			const float U = static_cast<float>(X) / FMath::Max(W - 1, 1), V0 = static_cast<float>(Y) / FMath::Max(H - 1, 1);
			const float CoverNoise = 0.5f + 0.5f * Fbm(U, V0, Frequency * 0.16f, 3, 0.55f, 4751, 0x91u);
			const float Mask = FMath::SmoothStep(1.0f - Coverage, FMath::Min(1.0f, 1.08f - Coverage * 0.25f), CoverNoise);
			const float N1 = Fbm(U, V0, Frequency, 4, 0.52f, 7127, 0xB1u);
			const float N2 = Fbm(U, V0, Frequency * 2.9f, 3, 0.48f, 9419, 0xC1u);
			float Detail = N1 * 0.7f + N2 * 0.3f;
			if (Method == TEXT("Rocky")) Detail = (1.0f - FMath::Abs(Detail)) * 2.0f - 1.0f;
			else if (Method == TEXT("Harsh")) Detail = FMath::Sign(Detail) * FMath::Pow(FMath::Abs(Detail), 0.55f);
			R.AtInterior(X, Y) = FMath::Clamp(Source->AtInterior(X, Y) + Detail * Mask * Strength * 0.035f, -1.0f, 1.0f);
		}
		return PublishLike(*V, MoveTemp(R), Out, Error);
	}

	bool EvaluateStratify(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* V = Input(Inputs, TEXT("Terrain")); const FEonformScalarField* Source = AsField(V);
		if (!V || V->Type != EEonformTerrainValueType::Terrain || !Source) { Error = TEXT("Stratify requires Terrain input."); return false; }
		const float Spacing = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Spacing"), 0.5)), 0.02f, 4.0f);
		const int32 Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 8);
		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 0.5)), 0.0f, 1.5f);
		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"), 0.5)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Tilt = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("TiltAmount"), 0.0)), -1.0f, 1.0f);
		const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		FEonformScalarField R = *Source;
		const int32 W = R.Domain.Dimensions.X, H = R.Domain.Dimensions.Y;
		const float LayerFrequency = FMath::Lerp(48.0f, 6.0f, FMath::Clamp(Spacing / 4.0f, 0.0f, 1.0f));
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
		{
			const float U = static_cast<float>(X) / FMath::Max(W - 1, 1) - 0.5f, V0 = static_cast<float>(Y) / FMath::Max(H - 1, 1) - 0.5f;
			const float Directional = U * FMath::Cos(Direction) + V0 * FMath::Sin(Direction);
			const float Breakup = Fbm(U, V0, 3.5f, Octaves, 0.56f, Seed, 0xD1u);
			const float Zone = FMath::SmoothStep(-0.15f, 0.32f, Fbm(U, V0, 1.7f, 3, 0.55f, Seed + 503, 0xE1u));
			const float H01 = FMath::Clamp(Source->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
			const float Phase = (H01 + Directional * Tilt * 0.24f + Breakup * 0.035f) * LayerFrequency;
			float Layer = 1.0f - 2.0f * FMath::Abs(FMath::Frac(Phase) - 0.5f);
			Layer = FMath::Pow(FMath::Clamp(Layer, 0.0f, 1.0f), FMath::Lerp(0.65f, 4.5f, Shape));
			const float Delta = (Layer - 0.42f) * Intensity * Zone * 0.055f;
			R.AtInterior(X, Y) = FMath::Clamp(Source->AtInterior(X, Y) + Delta, -1.0f, 1.0f);
		}
		return PublishLike(*V, MoveTemp(R), Out, Error);
	}

	void RegisterRockNoise()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::RockNoise; D.DisplayName = TEXT("RockNoise"); D.Category = TEXT("Surface");
		D.Description = TEXT("Generates a flat multi-octave rock field for Embed/Insert workflows."); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
		D.Parameters = { Num(TEXT("Size"), TEXT("Size"), 0.5, 0.02, 4.0), Num(TEXT("Variety"), TEXT("Variety"), 0.5, 0.0, 1.0), Int(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 8), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647), Choice(TEXT("Style"), TEXT("Style"), TEXT("A"), { TEXT("A"), TEXT("B"), TEXT("C") }) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateRockNoise);
	}
	void RegisterGroundTexture()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::GroundTexture; D.DisplayName = TEXT("GroundTexture"); D.Category = TEXT("Surface"); D.Description = TEXT("Adds superficial Harsh, Rocky or Rough detail with controllable strength, coverage and density.");
		D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
		D.Parameters = { Choice(TEXT("Method"), TEXT("Method"), TEXT("Rough"), { TEXT("Harsh"), TEXT("Rocky"), TEXT("Rough") }), Num(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 2.0), Num(TEXT("Coverage"), TEXT("Coverage"), 0.65, 0.0, 1.0), Num(TEXT("Density"), TEXT("Density"), 0.5, 0.01, 2.0) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateGroundTexture);
	}
	void RegisterStratify()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::Stratify; D.DisplayName = TEXT("Stratify"); D.Category = TEXT("Surface"); D.Description = TEXT("Creates broken non-linear rock strata with independent local zones and geological tilt.");
		D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
		D.Parameters = { Num(TEXT("Spacing"), TEXT("Spacing"), 0.5, 0.02, 4.0), Int(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 8), Num(TEXT("Intensity"), TEXT("Intensity"), 0.5, 0.0, 1.5), Num(TEXT("Shape"), TEXT("Shape"), 0.5, 0.0, 1.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647), Num(TEXT("TiltAmount"), TEXT("Tilt Amount"), 0.0, -1.0, 1.0), Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateStratify);
	}
}

void RegisterEonformReferenceFidelityExtendedNodes()
{
	using namespace EonformReferenceFidelityExtended;
	RegisterRockNoise();
	RegisterGroundTexture();
	RegisterStratify();
}
