#include "GaeaPrimitiveCoverageNodes.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaPrimitiveCoverage
{
	FGaeaTerrainPortDescriptor TerrainOut()
	{
		FGaeaTerrainPortDescriptor Port; Port.Name = TEXT("Out"); Port.DisplayName = TEXT("Out"); Port.DataType = TEXT("Terrain"); return Port;
	}
	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min = -1000000.0, double Max = 1000000.0)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Number; P.DefaultNumber = Default;
		P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}
	FGaeaTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min = -2147483647LL, int64 Max = 2147483647LL)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Integer; P.DefaultInteger = Default;
		P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); return P;
	}
	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Boolean; P.DefaultBoolean = Default; return P;
	}
	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Name; P.DefaultName = Default;
		for (const FName Option : Options) P.NameOptions.Add(Option); return P;
	}
	bool MakeDomain(const FGaeaTerrainNode& Node, FGaeaGridDomain& Domain, float& HeightScale, FString& Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const double Half = static_cast<double>(WorldSize) * 0.5;
		Domain = FGaeaGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-Half, -Half), FVector2d(Half, Half));
		if (!Domain.IsValid()) { Error = TEXT("Primitive produced an invalid grid domain."); return false; }
		return true;
	}
	template<typename Sampler>
	bool BuildTerrain(const FGaeaTerrainNode& Node, FGaeaTerrainNodeEvaluation& Out, FString& Error, Sampler&& Sample)
	{
		FGaeaGridDomain Domain; float HeightScale = 8000.0f;
		if (!MakeDomain(Node, Domain, HeightScale, Error)) return false;
		FGaeaFieldDescriptor D; D.Name = GaeaTerrainFieldNames::Height; D.Unit = EGaeaFieldUnit::Normalized; D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field; Field.Initialize(Domain, D);
		const FIntPoint Dim = Domain.Dimensions;
		for (int32 Y = 0; Y < Dim.Y; ++Y)
		{
			for (int32 X = 0; X < Dim.X; ++X)
			{
				const FVector2d W = Domain.InteriorSampleToWorld(X, Y);
				Field.AtInterior(X, Y) = FMath::Clamp(Sample(X, Y, W, Domain), -1.0f, 1.0f);
			}
		}
		FGaeaTerrainDataset Dataset; if (!Dataset.SetScalarField(MoveTemp(Field))) { Error = TEXT("Primitive could not publish Height."); return false; }
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale); if (!Result.IsValid()) { Error = TEXT("Primitive produced invalid terrain."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result)); return true;
	}
	uint32 Hash(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16; return X;
	}
	float Hash01(int32 X, int32 Y, int32 Seed)
	{
		return static_cast<float>(Hash(static_cast<uint32>(X) * 0x9e3779b9U ^ static_cast<uint32>(Y) * 0x85ebca6bU ^ static_cast<uint32>(Seed)) & 0x00ffffffU) / 16777216.0f;
	}
	float Fbm(FVector2D P, int32 Octaves, float Roughness, int32 Seed)
	{
		float Sum = 0.0f, Amp = 1.0f, Total = 0.0f;
		for (int32 I = 0; I < Octaves; ++I)
		{
			Sum += FMath::PerlinNoise2D(P + FVector2D(Seed * 0.013f, Seed * 0.021f)) * Amp;
			Total += Amp; Amp *= Roughness; P *= 2.0f;
		}
		return Total > UE_SMALL_NUMBER ? Sum / Total : 0.0f;
	}
	bool EvalLineNoise(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FName Bucket = Node.GetName(TEXT("BucketSize"), TEXT("X 1"));
		const FName Style = Node.GetName(TEXT("Style"), TEXT("Sharp"));
		const float ClampV = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Clamp"), 0.0)), 0.0f, 1.0f);
		const float Direction = static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		float Mul = 1.0f; if (Bucket == TEXT("X 2")) Mul = 2.0f; else if (Bucket == TEXT("X 3")) Mul = 3.0f; else if (Bucket == TEXT("X 4")) Mul = 4.0f;
		const float A = FMath::DegreesToRadians(Direction); const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d& W, const FGaeaGridDomain& D)
		{
			const FVector2D P(static_cast<float>(W.X / D.WorldSize().X), static_cast<float>(W.Y / D.WorldSize().Y));
			const float Along = FVector2D::DotProduct(P, Dir) * 24.0f * Mul + FMath::PerlinNoise2D(P * 5.0f + FVector2D(Seed * 0.01f, 0.0f)) * 2.5f;
			float V = 0.5f + 0.5f * FMath::Cos(Along * 2.0f * PI);
			if (Style == TEXT("Sharp")) V = FMath::Pow(V, 4.0f); else if (Style == TEXT("Flat")) V = V > 0.55f ? 1.0f : 0.0f; else if (Style == TEXT("Vague")) V = FMath::SmoothStep(0.1f, 0.9f, V);
			return FMath::Max(V - ClampV, 0.0f);
		});
	}
	bool EvalNoise(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FName Type = Node.GetName(TEXT("NoiseType"), TEXT("Random")); const float Height = static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0));
		const float Density = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Density"), 1.0)), 0.001f); const bool Relative = Node.GetBool(TEXT("RelativeDensity"), true);
		const FName Blend = Node.GetName(TEXT("BlendMode"), TEXT("Add")); const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		return BuildTerrain(Node, Out, Error, [=](int32 X, int32 Y, const FVector2d& W, const FGaeaGridDomain& D)
		{
			float V = Hash01(X, Y, Seed);
			if (Type == TEXT("Perlin")) { const FVector2D P(static_cast<float>(W.X / D.WorldSize().X), static_cast<float>(W.Y / D.WorldSize().Y)); V = FMath::PerlinNoise2D(P * 64.0f * Density + FVector2D(Seed * 0.01f)) * 0.5f + 0.5f; }
			else if (Type == TEXT("Gaussian")) { const float U1 = FMath::Max(Hash01(X, Y, Seed), 0.0001f); const float U2 = Hash01(Y, X, Seed + 17); V = FMath::Clamp(0.5f + FMath::Sqrt(-2.0f * FMath::Loge(U1)) * FMath::Cos(2.0f * PI * U2) * 0.16f, 0.0f, 1.0f); }
			else if (Type == TEXT("Fixed")) V = Hash01(FMath::FloorToInt(X / FMath::Max(1.0f, 8.0f / Density)), FMath::FloorToInt(Y / FMath::Max(1.0f, 8.0f / Density)), Seed);
			else if (Type == TEXT("Micro")) V = Hash01(X * 7, Y * 11, Seed);
			if (!Relative) V = FMath::Pow(V, 1.0f / Density);
			if (Blend == TEXT("Multiply")) V *= 0.5f; else if (Blend == TEXT("Subtract")) V = -V;
			return V * Height;
		});
	}
	bool EvalPattern(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const float Size = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Size"), 1.0)), 0.001f); const float DotSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("DotSize"), 0.35)), 0.001f, 1.0f);
		const float Spacing = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Spacing"), 1.0)), 0.001f); const int32 Steps = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Steps"), 8)), 1, 128);
		const float Direction = static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)); const FName Type = Node.GetName(TEXT("Type"), TEXT("Lines")); const float PixelSize = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("PixelSize"), 1.0)), 0.001f);
		const float A = FMath::DegreesToRadians(Direction); const float C = FMath::Cos(A), S = FMath::Sin(A);
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d& W, const FGaeaGridDomain& D)
		{
			float U = static_cast<float>(W.X / D.WorldSize().X) * C - static_cast<float>(W.Y / D.WorldSize().Y) * S;
			float V = static_cast<float>(W.X / D.WorldSize().X) * S + static_cast<float>(W.Y / D.WorldSize().Y) * C;
			U *= Steps / (Size * Spacing); V *= Steps / (Size * Spacing);
			const float FU = FMath::Abs(FMath::Frac(U + 0.5f) - 0.5f); const float FV = FMath::Abs(FMath::Frac(V + 0.5f) - 0.5f);
			float R = 0.0f; if (Type == TEXT("Grid")) R = (FU < 0.08f * PixelSize || FV < 0.08f * PixelSize) ? 1.0f : 0.0f;
			else if (Type == TEXT("Dots")) R = FMath::Sqrt(FU * FU + FV * FV) < DotSize * 0.5f ? 1.0f : 0.0f;
			else if (Type == TEXT("Engraving")) R = FMath::Pow(1.0f - FMath::Clamp(FU * 6.0f, 0.0f, 1.0f), 2.0f); else R = FU < 0.1f * PixelSize ? 1.0f : 0.0f;
			return R;
		});
	}
	bool EvalShape(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FName Geometry = Node.GetName(TEXT("Geometry"), TEXT("Circle")); const int32 Sides = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Sides"), 32)), 3, 128); const bool Uniform = Node.GetBool(TEXT("Uniform"), true);
		const float Scale = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.001f); const float SX = Uniform ? Scale : FMath::Max(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), Scale)), 0.001f); const float SY = Uniform ? Scale : FMath::Max(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), Scale)), 0.001f);
		const float Thickness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Thickness"), 1.0)), 0.0f, 1.0f); const float Height = static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)); const float OX = static_cast<float>(Node.GetNumber(TEXT("X"), 0.0)); const float OY = static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0));
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d& W, const FGaeaGridDomain& D)
		{
			const float X = (static_cast<float>(W.X) - OX) / (static_cast<float>(D.WorldSize().X) * 0.5f * SX); const float Y = (static_cast<float>(W.Y) - OY) / (static_cast<float>(D.WorldSize().Y) * 0.5f * SY);
			float Dist = 0.0f; if (Geometry == TEXT("Rectangle")) Dist = FMath::Max(FMath::Abs(X), FMath::Abs(Y)); else { const float A = FMath::Atan2(Y, X); const float R = FMath::Sqrt(X * X + Y * Y); const float Sector = 2.0f * PI / Sides; Dist = R * FMath::Cos(FMath::Fmod(A + PI, Sector) - Sector * 0.5f) / FMath::Cos(Sector * 0.5f); }
			const float Outer = Dist <= 1.0f ? 1.0f : 0.0f; const float Inner = Dist < (1.0f - Thickness) ? 1.0f : 0.0f; return (Outer - Inner) * Height;
		});
	}
	bool EvalMultiFractal(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FName NoiseType = Node.GetName(TEXT("NoiseType"), TEXT("FBM")); const bool Auto = Node.GetBool(TEXT("AutoOctaves"), true); const int32 Oct = Auto ? 6 : FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 6)), 1, 16);
		const float Scale = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.001f); const float Roughness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Roughness"), 0.5)), 0.01f, 0.99f); const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Rotation = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Rotation"), 0.0))); const float Aniso = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Anisotropy"), 0.0)), 0.0f, 1.0f); const float OX = static_cast<float>(Node.GetNumber(TEXT("OffsetX"), 0.0)); const float OY = static_cast<float>(Node.GetNumber(TEXT("OffsetY"), 0.0));
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d& W, const FGaeaGridDomain& D)
		{
			float X = static_cast<float>(W.X / D.WorldSize().X) + OX; float Y = static_cast<float>(W.Y / D.WorldSize().Y) + OY; const float RX = X * FMath::Cos(Rotation) - Y * FMath::Sin(Rotation); const float RY = X * FMath::Sin(Rotation) + Y * FMath::Cos(Rotation);
			FVector2D P(RX * 6.0f / Scale * FMath::Lerp(1.0f, 4.0f, Aniso), RY * 6.0f / Scale); float V = Fbm(P, Oct, Roughness, Seed);
			if (NoiseType == TEXT("Billowy")) V = FMath::Abs(V) * 2.0f - 1.0f; else if (NoiseType == TEXT("Ridged")) V = 1.0f - FMath::Abs(V);
			return V * 0.5f + 0.5f;
		});
	}
	bool EvalVoronoi(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const float Scale = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.001f); const float Jitter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Jitter"), 1.0)), 0.0f, 2.0f); const FName Function = Node.GetName(TEXT("Function"), TEXT("Euclidean")); const FName Form = Node.GetName(TEXT("Form"), TEXT("P")); const float Gain = static_cast<float>(Node.GetNumber(TEXT("Gain"), 1.0)); const float ClampV = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Clamp"), 1.0)), 0.0f, 1.0f); const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d& W, const FGaeaGridDomain& D)
		{
			const float PX = static_cast<float>(W.X / D.WorldSize().X) * 18.0f * Scale; const float PY = static_cast<float>(W.Y / D.WorldSize().Y) * 18.0f * Scale; const int32 BX = FMath::FloorToInt(PX), BY = FMath::FloorToInt(PY); float F1 = 9999.0f, F2 = 9999.0f; float Cell = 0.0f;
			for (int32 CY = BY - 1; CY <= BY + 1; ++CY) for (int32 CX = BX - 1; CX <= BX + 1; ++CX)
			{
				const float FX = CX + 0.5f + (Hash01(CX, CY, Seed) - 0.5f) * Jitter; const float FY = CY + 0.5f + (Hash01(CY, CX, Seed + 31) - 0.5f) * Jitter; const float DX = PX - FX, DY = PY - FY; const float Dist = Function == TEXT("Manhattan") ? FMath::Abs(DX) + FMath::Abs(DY) : FMath::Sqrt(DX * DX + DY * DY);
				if (Dist < F1) { F2 = F1; F1 = Dist; Cell = Hash01(CX, CY, Seed + 71); } else if (Dist < F2) F2 = Dist;
			}
			float V = 1.0f - FMath::Clamp(F1, 0.0f, 1.0f); if (Form == TEXT("C")) V = Cell; else if (Form == TEXT("N")) V = 1.0f - FMath::Clamp(F2, 0.0f, 1.0f); else if (Form == TEXT("R") || Form == TEXT("D")) V = FMath::Clamp(F2 - F1, 0.0f, 1.0f); else if (Form == TEXT("S")) V = FMath::Pow(V, 3.0f); else if (Form == TEXT("M")) V *= Cell; else if (Form == TEXT("A")) V = FMath::Lerp(V, Cell, 0.35f);
			return FMath::Min(V * Gain, ClampV);
		});
	}
	bool EvalWaveShine(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const int32 Frame = static_cast<int32>(Node.GetInteger(TEXT("Frame"), 0)); const int32 Oct = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 12); const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Anim = static_cast<float>(Node.GetNumber(TEXT("RippleAnimationSpeed"), 1.0)); const float Size = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("RippleSize"), 1.0)), 0.001f); const float Amp = static_cast<float>(Node.GetNumber(TEXT("RippleAmplitude"), 1.0)); const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("RippleDirection"), 0.0))); const float Speed = static_cast<float>(Node.GetNumber(TEXT("RippleSpeed"), 1.0)); const float Wind = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WindIntensity"), 0.5)), 0.0f, 2.0f); const float Brightness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Brightness"), 1.0)), 0.0f, 4.0f);
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d& W, const FGaeaGridDomain& D)
		{
			const FVector2D P(static_cast<float>(W.X / D.WorldSize().X), static_cast<float>(W.Y / D.WorldSize().Y)); const FVector2D Dir(FMath::Cos(Direction), FMath::Sin(Direction)); float Sum = 0.0f, Weight = 0.0f;
			for (int32 I = 0; I < Oct; ++I) { const float F = FMath::Pow(2.0f, static_cast<float>(I)) / Size; const float Phase = FVector2D::DotProduct(P, Dir) * F * 2.0f * PI + Frame * Anim * Speed * 0.05f + Fbm(P * F * 0.5f, 2, 0.5f, Seed + I) * Wind; const float A = FMath::Pow(0.55f, static_cast<float>(I)); Sum += FMath::Sin(Phase) * A; Weight += A; }
			return FMath::Clamp((0.5f + 0.5f * Sum / FMath::Max(Weight, UE_SMALL_NUMBER)) * Amp * Brightness, 0.0f, 1.0f);
		});
	}
}

void RegisterGaeaLineNoiseNode()
{
	using namespace GaeaPrimitiveCoverage; FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::LineNoise; D.DisplayName = TEXT("LineNoise"); D.Category = TEXT("Primitive"); D.Description = TEXT("Generates directional line sets for layered ridges and procedural patterns."); D.Outputs.Add(TerrainOut());
	D.Parameters = { Choice(TEXT("BucketSize"), TEXT("Bucket Size"), TEXT("X 1"), {TEXT("X 1"),TEXT("X 2"),TEXT("X 3"),TEXT("X 4")}), Choice(TEXT("Style"), TEXT("Style"), TEXT("Sharp"), {TEXT("Sharp"),TEXT("Soft"),TEXT("Flat"),TEXT("Vague")}), Num(TEXT("Clamp"), TEXT("Clamp"), 0.0, 0.0, 1.0), Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0), Int(TEXT("Seed"), TEXT("Seed"), 1337) };
	FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::LineNoise, EvalLineNoise);
}

void RegisterGaeaNoiseNode()
{
	using namespace GaeaPrimitiveCoverage; FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::Noise; D.DisplayName = TEXT("Noise"); D.Category = TEXT("Primitive"); D.Description = TEXT("Applies controlled pixel and procedural noise."); D.Outputs.Add(TerrainOut());
	D.Parameters = { Choice(TEXT("NoiseType"), TEXT("Noise Type"), TEXT("Random"), {TEXT("Random"),TEXT("Perlin"),TEXT("Gaussian"),TEXT("Fixed"),TEXT("Micro")}), Num(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0), Num(TEXT("Density"), TEXT("Density"), 1.0, 0.001, 64.0), Bool(TEXT("RelativeDensity"), TEXT("Relative Density"), true), Choice(TEXT("BlendMode"), TEXT("Blend Mode"), TEXT("Add"), {TEXT("Add"),TEXT("Multiply"),TEXT("Subtract")}), Int(TEXT("Seed"), TEXT("Seed"), 1337) };
	FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Noise, EvalNoise);
}

void RegisterGaeaPatternNode()
{
	using namespace GaeaPrimitiveCoverage; FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::Pattern; D.DisplayName = TEXT("Pattern"); D.Category = TEXT("Primitive"); D.Description = TEXT("Generates repeatable geometric patterns for masks and stylized terrain."); D.Outputs.Add(TerrainOut());
	D.Parameters = { Num(TEXT("Size"), TEXT("Size"), 1.0, 0.001, 16.0), Num(TEXT("DotSize"), TEXT("Dot Size"), 0.35, 0.001, 1.0), Num(TEXT("Spacing"), TEXT("Spacing"), 1.0, 0.001, 16.0), Int(TEXT("Steps"), TEXT("Steps"), 8, 1, 128), Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0), Choice(TEXT("Type"), TEXT("Type"), TEXT("Lines"), {TEXT("Lines"),TEXT("Engraving"),TEXT("Grid"),TEXT("Dots")}), Num(TEXT("PixelSize"), TEXT("Pixel Size"), 1.0, 0.001, 16.0) };
	FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Pattern, EvalPattern);
}

void RegisterGaeaShapeNode()
{
	using namespace GaeaPrimitiveCoverage; FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::Shape; D.DisplayName = TEXT("Shape"); D.Category = TEXT("Primitive"); D.Description = TEXT("Generates basic geometric terrain shapes for primitives and masks."); D.Outputs.Add(TerrainOut());
	D.Parameters = { Choice(TEXT("Geometry"), TEXT("Geometry"), TEXT("Circle"), {TEXT("Circle"),TEXT("Rectangle")}), Int(TEXT("Sides"), TEXT("Sides"), 32, 3, 128), Bool(TEXT("Uniform"), TEXT("Uniform"), true), Num(TEXT("Scale"), TEXT("Scale"), 0.5, 0.001, 10.0), Num(TEXT("ScaleX"), TEXT("Scale X"), 0.5, 0.001, 10.0), Num(TEXT("ScaleY"), TEXT("Scale Y"), 0.5, 0.001, 10.0), Num(TEXT("Thickness"), TEXT("Thickness"), 1.0, 0.0, 1.0), Num(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0), Num(TEXT("X"), TEXT("X"), 0.0), Num(TEXT("Y"), TEXT("Y"), 0.0) };
	FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Shape, EvalShape);
}

void RegisterGaeaMultiFractalNode()
{
	using namespace GaeaPrimitiveCoverage; FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::MultiFractal; D.DisplayName = TEXT("MultiFractal"); D.Category = TEXT("Primitive"); D.Description = TEXT("Generates highly variable multi-fractal noise with modulation, positioning, and warp controls."); D.Outputs.Add(TerrainOut());
	D.Parameters = { Choice(TEXT("NoiseType"), TEXT("Noise Type"), TEXT("FBM"), {TEXT("FBM"),TEXT("Billowy"),TEXT("Ridged")}), Bool(TEXT("AutoOctaves"), TEXT("Auto Octaves"), true), Int(TEXT("Octaves"), TEXT("Octaves"), 6, 1, 16), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.001, 16.0), Num(TEXT("RelativeFeatureScale"), TEXT("Relative Feature Scale"), 1.0, 0.001, 16.0), Num(TEXT("Roughness"), TEXT("Roughness"), 0.5, 0.0, 1.0), Num(TEXT("EdgeSmoothing"), TEXT("Edge Smoothing"), 0.0, 0.0, 1.0), Int(TEXT("Seed"), TEXT("Seed"), 1337), Choice(TEXT("VariationType"), TEXT("Type"), TEXT("Secondary Fractal"), {TEXT("Secondary Fractal"),TEXT("Self Modulation")}), Num(TEXT("Variation"), TEXT("Variation"), 0.5, 0.0, 1.0), Num(TEXT("Smoothness"), TEXT("Smoothness"), 0.5, 0.0, 1.0), Num(TEXT("Contrast"), TEXT("Contrast"), 1.0, 0.0, 4.0), Num(TEXT("Damping"), TEXT("Damping"), 0.0, 0.0, 1.0), Num(TEXT("Bias"), TEXT("Bias"), 0.0, -1.0, 1.0), Num(TEXT("Rotation"), TEXT("Rotation"), 0.0, -360.0, 360.0), Num(TEXT("Anisotropy"), TEXT("Anisotropy"), 0.0, 0.0, 1.0), Num(TEXT("OffsetX"), TEXT("Offset X"), 0.0), Num(TEXT("OffsetY"), TEXT("Offset Y"), 0.0), Choice(TEXT("Perturb"), TEXT("Perturb"), TEXT("None"), {TEXT("None"),TEXT("Simple"),TEXT("Complex")}), Num(TEXT("RelativeSize"), TEXT("Relative Size"), 1.0, 0.001, 16.0), Num(TEXT("Strength"), TEXT("Strength"), 0.0, 0.0, 4.0), Num(TEXT("Complexity"), TEXT("Complexity"), 0.5, 0.0, 1.0), Num(TEXT("WarpRoughness"), TEXT("Roughness"), 0.5, 0.0, 1.0), Num(TEXT("Attenuation"), TEXT("Attenuation"), 0.0, 0.0, 1.0), Int(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 16), Num(TEXT("RelativeAnisotropy"), TEXT("Relative Anisotropy"), 0.0, 0.0, 1.0) };
	FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::MultiFractal, EvalMultiFractal);
}

void RegisterGaeaVoronoiNode()
{
	using namespace GaeaPrimitiveCoverage; FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::Voronoi; D.DisplayName = TEXT("Voronoi"); D.Category = TEXT("Primitive"); D.Description = TEXT("Generates Gaea-style geometric Voronoi terrain patterns."); D.Outputs.Add(TerrainOut());
	D.Parameters = { Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.001, 16.0), Num(TEXT("Jitter"), TEXT("Jitter"), 1.0, 0.0, 2.0), Choice(TEXT("Function"), TEXT("Function"), TEXT("Euclidean"), {TEXT("Euclidean"),TEXT("Manhattan")}), Choice(TEXT("Form"), TEXT("Form"), TEXT("P"), {TEXT("C"),TEXT("N"),TEXT("R"),TEXT("P"),TEXT("A"),TEXT("S"),TEXT("M"),TEXT("D")}), Num(TEXT("Gain"), TEXT("Gain"), 1.0, 0.0, 4.0), Num(TEXT("Clamp"), TEXT("Clamp"), 1.0, 0.0, 1.0), Int(TEXT("Seed"), TEXT("Seed"), 1337), Choice(TEXT("WarpType"), TEXT("Type"), TEXT("None"), {TEXT("None"),TEXT("Simple"),TEXT("Complex")}), Num(TEXT("WarpFrequency"), TEXT("Frequency"), 1.0, 0.001, 16.0), Num(TEXT("WarpAmplitude"), TEXT("Amplitude"), 0.0, 0.0, 4.0), Int(TEXT("WarpOctaves"), TEXT("Octaves"), 1, 1, 16), Num(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.001, 16.0), Num(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.001, 16.0), Num(TEXT("X"), TEXT("X"), 0.0), Num(TEXT("Y"), TEXT("Y"), 0.0) };
	FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Voronoi, EvalVoronoi);
}

void RegisterGaeaWaveShineNode()
{
	using namespace GaeaPrimitiveCoverage; FGaeaTerrainNodeDescriptor D; D.Type = GaeaTerrainNodeTypes::WaveShine; D.DisplayName = TEXT("WaveShine"); D.Category = TEXT("Primitive"); D.Description = TEXT("Generates animated ripple and wind-patch patterns with lighting controls."); D.Outputs.Add(TerrainOut());
	D.Parameters = { Int(TEXT("Frame"), TEXT("Frame"), 0), Int(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 12), Int(TEXT("Seed"), TEXT("Seed"), 1337), Num(TEXT("RippleAnimationSpeed"), TEXT("Animation Speed"), 1.0, -10.0, 10.0), Num(TEXT("RippleSize"), TEXT("Size"), 1.0, 0.001, 16.0), Num(TEXT("RippleAmplitude"), TEXT("Amplitude"), 1.0, 0.0, 4.0), Num(TEXT("RippleDirection"), TEXT("Direction"), 0.0, -360.0, 360.0), Num(TEXT("RippleSpeed"), TEXT("Speed"), 1.0, -10.0, 10.0), Num(TEXT("WindIntensity"), TEXT("Intensity"), 0.5, 0.0, 2.0), Num(TEXT("WindAnimationSpeed"), TEXT("Animation Speed"), 1.0, -10.0, 10.0), Num(TEXT("WindSize"), TEXT("Size"), 1.0, 0.001, 16.0), Num(TEXT("WindContrast"), TEXT("Contrast"), 1.0, 0.0, 4.0), Num(TEXT("Brightness"), TEXT("Brightness"), 1.0, 0.0, 4.0), Num(TEXT("SunAzimuth"), TEXT("Sun Azimuth"), 45.0, -360.0, 360.0), Num(TEXT("SunElevation"), TEXT("Sun Elevation"), 35.0, -90.0, 90.0) };
	FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::WaveShine, EvalWaveShine);
}
