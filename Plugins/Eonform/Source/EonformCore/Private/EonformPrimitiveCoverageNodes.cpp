#include "EonformPrimitiveCoverageNodes.h"

#include "EonformGridDomain.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformTerrainNodeTypes
{
	const FName Crater(TEXT("Crater"));
	const FName Island(TEXT("Island"));
	const FName Plateau(TEXT("Plateau"));
}

namespace EonformPrimitiveCoverage
{
	FEonformTerrainPortDescriptor TerrainOut()
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = TEXT("Out");
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}
	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR *Label, double Default, double Min = -1000000.0, double Max = 1000000.0)
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
	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR *Label, int64 Default, int64 Min = -2147483647LL, int64 Max = 2147483647LL)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		return P;
	}
	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR *Label, bool Default)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		return P;
	}
	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR *Label, FName Default, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Option : Options)
			P.NameOptions.Add(Option);
		return P;
	}
	bool MakeDomain(const FEonformTerrainNode &Node, FEonformGridDomain &Domain, float &HeightScale, FString &Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 4097);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const double Half = static_cast<double>(WorldSize) * 0.5;
		Domain = FEonformGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-Half, -Half), FVector2d(Half, Half));
		if (!Domain.IsValid())
		{
			Error = TEXT("Primitive produced an invalid grid domain.");
			return false;
		}
		return true;
	}
	template <typename Sampler>
	bool BuildTerrain(const FEonformTerrainNode &Node, FEonformTerrainNodeEvaluation &Out, FString &Error, Sampler &&Sample)
	{
		FEonformGridDomain Domain;
		float HeightScale = 8000.0f;
		if (!MakeDomain(Node, Domain, HeightScale, Error))
			return false;
		FEonformFieldDescriptor D;
		D.Name = EonformTerrainFieldNames::Height;
		D.Unit = EEonformFieldUnit::Normalized;
		D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, D);
		const FIntPoint Dim = Domain.Dimensions;
		for (int32 Y = 0; Y < Dim.Y; ++Y)
		{
			for (int32 X = 0; X < Dim.X; ++X)
			{
				const FVector2d W = Domain.InteriorSampleToWorld(X, Y);
				Field.AtInterior(X, Y) = FMath::Clamp(Sample(X, Y, W, Domain), -1.0f, 1.0f);
			}
		}
		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Field)))
		{
			Error = TEXT("Primitive could not publish Height.");
			return false;
		}
		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Primitive produced invalid terrain.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
	uint32 Hash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return X;
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
			Total += Amp;
			Amp *= Roughness;
			P *= 2.0f;
		}
		return Total > UE_SMALL_NUMBER ? Sum / Total : 0.0f;
	}
	bool EvalCrater(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const float Radius = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Radius"), 0.55)), 0.01f, 1.0f);
		const float Depth = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Depth"), 0.65)), 0.0f, 4.0f);
		const float RimHeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RimHeight"), 0.25)), 0.0f, 4.0f);
		const float RimWidth = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RimWidth"), 0.18)), 0.01f, 0.8f);
		const float Height = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), -4.0f, 4.0f);
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d &W, const FEonformGridDomain &D)
												{
			const float X = static_cast<float>(W.X / (D.WorldSize().X * 0.5));
			const float Y = static_cast<float>(W.Y / (D.WorldSize().Y * 0.5));
			const float Angle = FMath::Atan2(Y, X);
			const float RadialWarp = 1.0f + Fbm(FVector2D(X, Y) * 2.2f, 3, 0.55f, 1721) * 0.10f;
			const float R = FMath::Sqrt(X * X + Y * Y) / FMath::Max(Radius * RadialWarp, 0.01f);
			const float RimVariation = 1.0f + FMath::Sin(Angle + 0.7f) * 0.08f + Fbm(FVector2D(X, Y) * 1.6f, 3, 0.55f, 8311) * 0.12f;
			const float BowlMask = FMath::Pow(FMath::Clamp(1.0f - R, 0.0f, 1.0f), 0.58f);
			const float InnerWall = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp((R - 0.34f) / 0.42f, 0.0f, 1.0f)) * (1.0f - FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp((R - 0.86f) / 0.16f, 0.0f, 1.0f)));
			const float RimCenter = 0.92f * RimVariation;
			const float RimDistance = FMath::Abs(R - RimCenter) / FMath::Max(RimWidth, 0.01f);
			const float RimMask = FMath::Pow(FMath::Clamp(1.0f - RimDistance, 0.0f, 1.0f), 0.72f);
			const float OuterEjecta = FMath::Exp(-FMath::Square(FMath::Max(R - 0.98f, 0.0f) / 0.52f)) * FMath::Pow(FMath::Clamp(1.30f - R, 0.0f, 1.0f), 0.80f);
			const float PeakRing = FMath::Exp(-FMath::Square((R - 0.25f) / 0.10f)) * 0.45f;
			const float CentralPeak = FMath::Exp(-FMath::Square(R / FMath::Max(0.16f + Depth * 0.02f, 0.05f))) * FMath::Lerp(0.035f, 0.14f, FMath::Clamp(Depth, 0.0f, 1.0f));
			const float Bench = FMath::Sin(R * 27.0f + Fbm(FVector2D(X, Y) * 5.0f, 2, 0.5f, 4811) * 1.5f) * InnerWall * 0.025f;
			const float Breach = FMath::Exp(-FMath::Square(FMath::Sin(Angle - 1.15f) / 0.16f)) * RimMask;
			float Secondary = 0.0f;
			for (int32 Impact = 0; Impact < 7; ++Impact)
			{
				const float ImpactAngle = static_cast<float>(Impact) * 2.399f + Hash01(Impact, 7, 1921) * 0.8f;
				const float ImpactRadius = 1.12f + Hash01(Impact, 11, 1921) * 0.42f;
				const FVector2D ImpactCenter(FMath::Cos(ImpactAngle) * ImpactRadius, FMath::Sin(ImpactAngle) * ImpactRadius);
				const float ImpactDistance = FVector2D::Distance(FVector2D(X, Y), ImpactCenter);
				Secondary += FMath::Exp(-FMath::Square(ImpactDistance / (0.025f + Hash01(Impact, 19, 1921) * 0.035f))) * (0.025f + Hash01(Impact, 23, 1921) * 0.05f);
			}
			const float Detail = Fbm(FVector2D(X, Y) * 9.0f, 4, 0.52f, 1721) * (0.025f + 0.035f * FMath::Clamp(Depth, 0.0f, 1.0f));
			const float Bowl = -BowlMask * Depth;
			return (Bowl * 0.78f + InnerWall * RimHeight * 0.13f + RimMask * RimHeight * 0.52f - Breach * RimHeight * 0.58f + OuterEjecta * RimHeight * 0.24f + PeakRing * RimHeight * 0.22f + CentralPeak + Bench + Secondary + Detail) * Height; });
	}
	bool EvalPlateau(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const float Radius = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Radius"), 0.55)), 0.01f, 1.0f);
		const float Height = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), -4.0f, 4.0f);
		const float Edge = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Edge"), 0.18)), 0.01f, 1.0f);
		const float Roughness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Roughness"), 0.08)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d &W, const FEonformGridDomain &D)
												{
			const float X = static_cast<float>(W.X / (D.WorldSize().X * 0.5));
			const float Y = static_cast<float>(W.Y / (D.WorldSize().Y * 0.5));
			const float R = FMath::Sqrt(X * X + Y * Y) / Radius;
			const float Mask = 1.0f - FMath::SmoothStep(1.0f - Edge, 1.0f, R);
			const float Detail = Fbm(FVector2D(X, Y) * 5.0f, 3, 0.5f, Seed) * Roughness;
			const float Top = FMath::Clamp(1.0f + Detail * 0.25f, 0.0f, 1.12f);
			return Mask * Top * Height; });
	}
	bool EvalIsland(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const float Radius = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Radius"), 0.8)), 0.05f, 1.5f);
		const float Height = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), -4.0f, 4.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 1.4)), 0.1f, 6.0f);
		const float NoiseScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("NoiseScale"), 3.0)), 0.1f, 20.0f);
		const float NoiseStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("NoiseStrength"), 0.25)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d &W, const FEonformGridDomain &D)
												{
			const float X = static_cast<float>(W.X / (D.WorldSize().X * 0.5));
			const float Y = static_cast<float>(W.Y / (D.WorldSize().Y * 0.5));
			const FVector2D P(X, Y);
			const float CoastNoise = Fbm(P * 1.6f, 4, 0.55f, Seed) * NoiseStrength * 0.18f;
			const float R = (FMath::Sqrt(X * X + Y * Y) - CoastNoise) / Radius;
			const float Mask = FMath::Pow(FMath::Clamp(1.0f - R, 0.0f, 1.0f), Falloff);
			const float Noise = Fbm(P * NoiseScale, 5, 0.55f, Seed + 17) * NoiseStrength;
			const float MountainMask = FMath::Pow(FMath::Clamp(1.0f - R, 0.0f, 1.0f), 0.35f);
			const float MountainNoise = FMath::Abs(Fbm(P * 2.2f, 4, 0.52f, Seed + 31));
			const float InteriorRelief = FMath::Lerp(0.78f, 1.22f, MountainNoise) * MountainMask;
			return Mask * FMath::Clamp(0.72f + InteriorRelief * 0.42f + Noise * 0.32f, 0.0f, 1.6f) * Height; });
	}
	bool EvalLineNoise(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const FName Bucket = Node.GetName(TEXT("BucketSize"), TEXT("X 1"));
		const FName Style = Node.GetName(TEXT("Style"), TEXT("Sharp"));
		const float ClampV = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Clamp"), 0.0)), 0.0f, 1.0f);
		const float Direction = static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		float Mul = 1.0f;
		if (Bucket == TEXT("X 2"))
			Mul = 2.0f;
		else if (Bucket == TEXT("X 3"))
			Mul = 3.0f;
		else if (Bucket == TEXT("X 4"))
			Mul = 4.0f;
		const float A = FMath::DegreesToRadians(Direction);
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d &W, const FEonformGridDomain &D)
												{
			const FVector2D P(static_cast<float>(W.X / D.WorldSize().X), static_cast<float>(W.Y / D.WorldSize().Y));
			const float Along = FVector2D::DotProduct(P, Dir) * 24.0f * Mul + FMath::PerlinNoise2D(P * 5.0f + FVector2D(Seed * 0.01f, 0.0f)) * 2.5f;
			float V = 0.5f + 0.5f * FMath::Cos(Along * 2.0f * PI);
			if (Style == TEXT("Sharp")) V = FMath::Pow(V, 4.0f); else if (Style == TEXT("Flat")) V = V > 0.55f ? 1.0f : 0.0f; else if (Style == TEXT("Vague")) V = FMath::SmoothStep(0.1f, 0.9f, V);
			return FMath::Max(V - ClampV, 0.0f); });
	}
	bool EvalNoise(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const FName Type = Node.GetName(TEXT("NoiseType"), TEXT("Random"));
		const float Height = static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0));
		const float Density = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Density"), 1.0)), 0.001f);
		const bool Relative = Node.GetBool(TEXT("RelativeDensity"), true);
		const FName Blend = Node.GetName(TEXT("BlendMode"), TEXT("Add"));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		return BuildTerrain(Node, Out, Error, [=](int32 X, int32 Y, const FVector2d &W, const FEonformGridDomain &D)
												{
			float V = Hash01(X, Y, Seed);
			if (Type == TEXT("Perlin")) { const FVector2D P(static_cast<float>(W.X / D.WorldSize().X), static_cast<float>(W.Y / D.WorldSize().Y)); V = FMath::PerlinNoise2D(P * 64.0f * Density + FVector2D(Seed * 0.01f)) * 0.5f + 0.5f; }
			else if (Type == TEXT("Gaussian")) { const float U1 = FMath::Max(Hash01(X, Y, Seed), 0.0001f); const float U2 = Hash01(Y, X, Seed + 17); V = FMath::Clamp(0.5f + FMath::Sqrt(-2.0f * FMath::Loge(U1)) * FMath::Cos(2.0f * PI * U2) * 0.16f, 0.0f, 1.0f); }
			else if (Type == TEXT("Fixed")) V = Hash01(FMath::FloorToInt(X / FMath::Max(1.0f, 8.0f / Density)), FMath::FloorToInt(Y / FMath::Max(1.0f, 8.0f / Density)), Seed);
			else if (Type == TEXT("Micro")) V = Hash01(X * 7, Y * 11, Seed);
			if (!Relative) V = FMath::Pow(V, 1.0f / Density);
			if (Blend == TEXT("Multiply")) V *= 0.5f; else if (Blend == TEXT("Subtract")) V = -V;
			return V * Height; });
	}
	bool EvalPattern(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const float Size = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Size"), 1.0)), 0.001f);
		const float DotSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("DotSize"), 0.35)), 0.001f, 1.0f);
		const float Spacing = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Spacing"), 1.0)), 0.001f);
		const int32 Steps = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Steps"), 8)), 1, 128);
		const float Direction = static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0));
		const FName Type = Node.GetName(TEXT("Type"), TEXT("Lines"));
		const float PixelSize = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("PixelSize"), 1.0)), 0.001f);
		const float A = FMath::DegreesToRadians(Direction);
		const float C = FMath::Cos(A), S = FMath::Sin(A);
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d &W, const FEonformGridDomain &D)
												{
			float U = static_cast<float>(W.X / D.WorldSize().X) * C - static_cast<float>(W.Y / D.WorldSize().Y) * S;
			float V = static_cast<float>(W.X / D.WorldSize().X) * S + static_cast<float>(W.Y / D.WorldSize().Y) * C;
			U *= Steps / (Size * Spacing); V *= Steps / (Size * Spacing);
			const float FU = FMath::Abs(FMath::Frac(U + 0.5f) - 0.5f); const float FV = FMath::Abs(FMath::Frac(V + 0.5f) - 0.5f);
			float R = 0.0f; if (Type == TEXT("Grid")) R = (FU < 0.08f * PixelSize || FV < 0.08f * PixelSize) ? 1.0f : 0.0f;
			else if (Type == TEXT("Dots")) R = FMath::Sqrt(FU * FU + FV * FV) < DotSize * 0.5f ? 1.0f : 0.0f;
			else if (Type == TEXT("Engraving")) R = FMath::Pow(1.0f - FMath::Clamp(FU * 6.0f, 0.0f, 1.0f), 2.0f); else R = FU < 0.1f * PixelSize ? 1.0f : 0.0f;
			return R; });
	}
	bool EvalShape(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const FName Geometry = Node.GetName(TEXT("Geometry"), TEXT("Circle"));
		const int32 Sides = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Sides"), 32)), 3, 128);
		const bool Uniform = Node.GetBool(TEXT("Uniform"), true);
		const float Scale = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.001f);
		const float SX = Uniform ? Scale : FMath::Max(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), Scale)), 0.001f);
		const float SY = Uniform ? Scale : FMath::Max(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), Scale)), 0.001f);
		const float Thickness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Thickness"), 1.0)), 0.0f, 1.0f);
		const float Height = static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0));
		const float OX = static_cast<float>(Node.GetNumber(TEXT("X"), 0.0));
		const float OY = static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0));
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d &W, const FEonformGridDomain &D)
												{
			const float X = (static_cast<float>(W.X) - OX) / (static_cast<float>(D.WorldSize().X) * 0.5f * SX); const float Y = (static_cast<float>(W.Y) - OY) / (static_cast<float>(D.WorldSize().Y) * 0.5f * SY);
			float Dist = 0.0f; if (Geometry == TEXT("Rectangle")) Dist = FMath::Max(FMath::Abs(X), FMath::Abs(Y)); else { const float A = FMath::Atan2(Y, X); const float R = FMath::Sqrt(X * X + Y * Y); const float Sector = 2.0f * PI / Sides; Dist = R * FMath::Cos(FMath::Fmod(A + PI, Sector) - Sector * 0.5f) / FMath::Cos(Sector * 0.5f); }
			const float Outer = Dist <= 1.0f ? 1.0f : 0.0f; const float Inner = Dist < (1.0f - Thickness) ? 1.0f : 0.0f; return (Outer - Inner) * Height; });
	}
	bool EvalMultiFractal(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const FName NoiseType = Node.GetName(TEXT("NoiseType"), TEXT("FBM"));
		const bool Auto = Node.GetBool(TEXT("AutoOctaves"), true);
		const int32 Oct = Auto ? 6 : FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 6)), 1, 16);
		const float Scale = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.001f);
		const float Roughness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Roughness"), 0.5)), 0.01f, 0.99f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Rotation = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Rotation"), 0.0)));
		const float Aniso = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Anisotropy"), 0.0)), 0.0f, 1.0f);
		const float OX = static_cast<float>(Node.GetNumber(TEXT("OffsetX"), 0.0));
		const float OY = static_cast<float>(Node.GetNumber(TEXT("OffsetY"), 0.0));
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d &W, const FEonformGridDomain &D)
												{
			float X = static_cast<float>(W.X / D.WorldSize().X) + OX; float Y = static_cast<float>(W.Y / D.WorldSize().Y) + OY; const float RX = X * FMath::Cos(Rotation) - Y * FMath::Sin(Rotation); const float RY = X * FMath::Sin(Rotation) + Y * FMath::Cos(Rotation);
			FVector2D P(RX * 6.0f / Scale * FMath::Lerp(1.0f, 4.0f, Aniso), RY * 6.0f / Scale); float V = Fbm(P, Oct, Roughness, Seed);
			if (NoiseType == TEXT("Billowy")) V = FMath::Abs(V) * 2.0f - 1.0f; else if (NoiseType == TEXT("Ridged")) V = 1.0f - FMath::Abs(V);
			return V * 0.5f + 0.5f; });
	}
	bool EvalWaveShine(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const int32 Frame = static_cast<int32>(Node.GetInteger(TEXT("Frame"), 0));
		const int32 Oct = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 12);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Anim = static_cast<float>(Node.GetNumber(TEXT("RippleAnimationSpeed"), 1.0));
		const float Size = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("RippleSize"), 1.0)), 0.001f);
		const float Amp = static_cast<float>(Node.GetNumber(TEXT("RippleAmplitude"), 1.0));
		const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("RippleDirection"), 0.0)));
		const float Speed = static_cast<float>(Node.GetNumber(TEXT("RippleSpeed"), 1.0));
		const float Wind = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WindIntensity"), 0.5)), 0.0f, 2.0f);
		const float Brightness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Brightness"), 1.0)), 0.0f, 4.0f);
		return BuildTerrain(Node, Out, Error, [=](int32, int32, const FVector2d &W, const FEonformGridDomain &D)
												{
			const FVector2D P(static_cast<float>(W.X / D.WorldSize().X), static_cast<float>(W.Y / D.WorldSize().Y)); const FVector2D Dir(FMath::Cos(Direction), FMath::Sin(Direction)); float Sum = 0.0f, Weight = 0.0f;
			for (int32 I = 0; I < Oct; ++I) { const float F = FMath::Pow(2.0f, static_cast<float>(I)) / Size; const float Phase = FVector2D::DotProduct(P, Dir) * F * 2.0f * PI + Frame * Anim * Speed * 0.05f + Fbm(P * F * 0.5f, 2, 0.5f, Seed + I) * Wind; const float A = FMath::Pow(0.55f, static_cast<float>(I)); Sum += FMath::Sin(Phase) * A; Weight += A; }
			return FMath::Clamp((0.5f + 0.5f * Sum / FMath::Max(Weight, UE_SMALL_NUMBER)) * Amp * Brightness, 0.0f, 1.0f); });
	}
}

void RegisterEonformLineNoiseNode()
{
	using namespace EonformPrimitiveCoverage;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::LineNoise;
	D.DisplayName = TEXT("LineNoise");
	D.Category = TEXT("Primitive");
	D.Description = TEXT("Generates directional line sets for layered ridges and procedural patterns.");
	D.Outputs.Add(TerrainOut());
	D.Parameters = {Choice(TEXT("BucketSize"), TEXT("Bucket Size"), TEXT("X 1"), {TEXT("X 1"), TEXT("X 2"), TEXT("X 3"), TEXT("X 4")}), Choice(TEXT("Style"), TEXT("Style"), TEXT("Sharp"), {TEXT("Sharp"), TEXT("Soft"), TEXT("Flat"), TEXT("Vague")}), Num(TEXT("Clamp"), TEXT("Clamp"), 0.0, 0.0, 1.0), Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0), Int(TEXT("Seed"), TEXT("Seed"), 1337)};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::LineNoise, EvalLineNoise);
}

void RegisterEonformCraterNode()
{
	using namespace EonformPrimitiveCoverage;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Crater;
	D.DisplayName = TEXT("Crater");
	D.Category = TEXT("Terrain");
	D.Description = TEXT("Generates an impact crater with a raised rim and adjustable bowl depth.");
	D.Outputs.Add(TerrainOut());
	D.Parameters = {Num(TEXT("Radius"), TEXT("Radius"), 0.55, 0.01, 1.0), Num(TEXT("Depth"), TEXT("Depth"), 0.65, 0.0, 4.0), Num(TEXT("RimHeight"), TEXT("Rim Height"), 0.25, 0.0, 4.0), Num(TEXT("RimWidth"), TEXT("Rim Width"), 0.18, 0.01, 0.8), Num(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0)};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Crater, EvalCrater);
}

void RegisterEonformPlateauNode()
{
	using namespace EonformPrimitiveCoverage;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Plateau;
	D.DisplayName = TEXT("Plateau");
	D.Category = TEXT("Terrain");
	D.Description = TEXT("Generates a flat-topped plateau with softened edges and optional surface roughness.");
	D.Outputs.Add(TerrainOut());
	D.Parameters = {Num(TEXT("Radius"), TEXT("Radius"), 0.55, 0.01, 1.0), Num(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0), Num(TEXT("Edge"), TEXT("Edge Softness"), 0.18, 0.01, 1.0), Num(TEXT("Roughness"), TEXT("Roughness"), 0.08, 0.0, 1.0), Int(TEXT("Seed"), TEXT("Seed"), 1337)};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Plateau, EvalPlateau);
}

void RegisterEonformIslandNode()
{
	using namespace EonformPrimitiveCoverage;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Island;
	D.DisplayName = TEXT("Island");
	D.Category = TEXT("Terrain");
	D.Description = TEXT("Generates an island mass with radial falloff and seeded terrain variation.");
	D.Outputs.Add(TerrainOut());
	D.Parameters = {Num(TEXT("Radius"), TEXT("Radius"), 0.8, 0.05, 1.5), Num(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0), Num(TEXT("Falloff"), TEXT("Falloff"), 1.4, 0.1, 6.0), Num(TEXT("NoiseScale"), TEXT("Noise Scale"), 3.0, 0.1, 20.0), Num(TEXT("NoiseStrength"), TEXT("Noise Strength"), 0.25, 0.0, 1.0), Int(TEXT("Seed"), TEXT("Seed"), 1337)};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Island, EvalIsland);
}

void RegisterEonformNoiseNode()
{
	using namespace EonformPrimitiveCoverage;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Noise;
	D.DisplayName = TEXT("Noise");
	D.Category = TEXT("Primitive");
	D.Description = TEXT("Applies controlled pixel and procedural noise.");
	D.Outputs.Add(TerrainOut());
	D.Parameters = {Choice(TEXT("NoiseType"), TEXT("Noise Type"), TEXT("Random"), {TEXT("Random"), TEXT("Perlin"), TEXT("Gaussian"), TEXT("Fixed"), TEXT("Micro")}), Num(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0), Num(TEXT("Density"), TEXT("Density"), 1.0, 0.001, 64.0), Bool(TEXT("RelativeDensity"), TEXT("Relative Density"), true), Choice(TEXT("BlendMode"), TEXT("Blend Mode"), TEXT("Add"), {TEXT("Add"), TEXT("Multiply"), TEXT("Subtract")}), Int(TEXT("Seed"), TEXT("Seed"), 1337)};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Noise, EvalNoise);
}

void RegisterEonformPatternNode()
{
	using namespace EonformPrimitiveCoverage;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Pattern;
	D.DisplayName = TEXT("Pattern");
	D.Category = TEXT("Primitive");
	D.Description = TEXT("Generates repeatable geometric patterns for masks and stylized terrain.");
	D.Outputs.Add(TerrainOut());
	D.Parameters = {Num(TEXT("Size"), TEXT("Size"), 1.0, 0.001, 16.0), Num(TEXT("DotSize"), TEXT("Dot Size"), 0.35, 0.001, 1.0), Num(TEXT("Spacing"), TEXT("Spacing"), 1.0, 0.001, 16.0), Int(TEXT("Steps"), TEXT("Steps"), 8, 1, 128), Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0), Choice(TEXT("Type"), TEXT("Type"), TEXT("Lines"), {TEXT("Lines"), TEXT("Engraving"), TEXT("Grid"), TEXT("Dots")}), Num(TEXT("PixelSize"), TEXT("Pixel Size"), 1.0, 0.001, 16.0)};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Pattern, EvalPattern);
}

void RegisterEonformShapeNode()
{
	using namespace EonformPrimitiveCoverage;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Shape;
	D.DisplayName = TEXT("Shape");
	D.Category = TEXT("Primitive");
	D.Description = TEXT("Generates basic geometric terrain shapes for primitives and masks.");
	D.Outputs.Add(TerrainOut());
	D.Parameters = {Choice(TEXT("Geometry"), TEXT("Geometry"), TEXT("Circle"), {TEXT("Circle"), TEXT("Rectangle")}), Int(TEXT("Sides"), TEXT("Sides"), 32, 3, 128), Bool(TEXT("Uniform"), TEXT("Uniform"), true), Num(TEXT("Scale"), TEXT("Scale"), 0.5, 0.001, 10.0), Num(TEXT("ScaleX"), TEXT("Scale X"), 0.5, 0.001, 10.0), Num(TEXT("ScaleY"), TEXT("Scale Y"), 0.5, 0.001, 10.0), Num(TEXT("Thickness"), TEXT("Thickness"), 1.0, 0.0, 1.0), Num(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0), Num(TEXT("X"), TEXT("X"), 0.0), Num(TEXT("Y"), TEXT("Y"), 0.0)};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Shape, EvalShape);
}

void RegisterEonformMultiFractalNode()
{
	using namespace EonformPrimitiveCoverage;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::MultiFractal;
	D.DisplayName = TEXT("MultiFractal");
	D.Category = TEXT("Primitive");
	D.Description = TEXT("Generates highly variable multi-fractal noise with modulation, positioning, and warp controls.");
	D.Outputs.Add(TerrainOut());
	D.Parameters = {Choice(TEXT("NoiseType"), TEXT("Noise Type"), TEXT("FBM"), {TEXT("FBM"), TEXT("Billowy"), TEXT("Ridged")}), Bool(TEXT("AutoOctaves"), TEXT("Auto Octaves"), true), Int(TEXT("Octaves"), TEXT("Octaves"), 6, 1, 16), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.001, 16.0), Num(TEXT("RelativeFeatureScale"), TEXT("Relative Feature Scale"), 1.0, 0.001, 16.0), Num(TEXT("Roughness"), TEXT("Roughness"), 0.5, 0.0, 1.0), Num(TEXT("EdgeSmoothing"), TEXT("Edge Smoothing"), 0.0, 0.0, 1.0), Int(TEXT("Seed"), TEXT("Seed"), 1337), Choice(TEXT("VariationType"), TEXT("Type"), TEXT("Secondary Fractal"), {TEXT("Secondary Fractal"), TEXT("Self Modulation")}), Num(TEXT("Variation"), TEXT("Variation"), 0.5, 0.0, 1.0), Num(TEXT("Smoothness"), TEXT("Smoothness"), 0.5, 0.0, 1.0), Num(TEXT("Contrast"), TEXT("Contrast"), 1.0, 0.0, 4.0), Num(TEXT("Damping"), TEXT("Damping"), 0.0, 0.0, 1.0), Num(TEXT("Bias"), TEXT("Bias"), 0.0, -1.0, 1.0), Num(TEXT("Rotation"), TEXT("Rotation"), 0.0, -360.0, 360.0), Num(TEXT("Anisotropy"), TEXT("Anisotropy"), 0.0, 0.0, 1.0), Num(TEXT("OffsetX"), TEXT("Offset X"), 0.0), Num(TEXT("OffsetY"), TEXT("Offset Y"), 0.0), Choice(TEXT("Perturb"), TEXT("Perturb"), TEXT("None"), {TEXT("None"), TEXT("Simple"), TEXT("Complex")}), Num(TEXT("RelativeSize"), TEXT("Relative Size"), 1.0, 0.001, 16.0), Num(TEXT("Strength"), TEXT("Strength"), 0.0, 0.0, 4.0), Num(TEXT("Complexity"), TEXT("Complexity"), 0.5, 0.0, 1.0), Num(TEXT("WarpRoughness"), TEXT("Roughness"), 0.5, 0.0, 1.0), Num(TEXT("Attenuation"), TEXT("Attenuation"), 0.0, 0.0, 1.0), Int(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 16), Num(TEXT("RelativeAnisotropy"), TEXT("Relative Anisotropy"), 0.0, 0.0, 1.0)};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::MultiFractal, EvalMultiFractal);
}

void RegisterEonformWaveShineNode()
{
	using namespace EonformPrimitiveCoverage;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::WaveShine;
	D.DisplayName = TEXT("WaveShine");
	D.Category = TEXT("Primitive");
	D.Description = TEXT("Generates animated ripple and wind-patch patterns with lighting controls.");
	D.Outputs.Add(TerrainOut());
	D.Parameters = {Int(TEXT("Frame"), TEXT("Frame"), 0), Int(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 12), Int(TEXT("Seed"), TEXT("Seed"), 1337), Num(TEXT("RippleAnimationSpeed"), TEXT("Animation Speed"), 1.0, -10.0, 10.0), Num(TEXT("RippleSize"), TEXT("Size"), 1.0, 0.001, 16.0), Num(TEXT("RippleAmplitude"), TEXT("Amplitude"), 1.0, 0.0, 4.0), Num(TEXT("RippleDirection"), TEXT("Direction"), 0.0, -360.0, 360.0), Num(TEXT("RippleSpeed"), TEXT("Speed"), 1.0, -10.0, 10.0), Num(TEXT("WindIntensity"), TEXT("Intensity"), 0.5, 0.0, 2.0), Num(TEXT("WindAnimationSpeed"), TEXT("Animation Speed"), 1.0, -10.0, 10.0), Num(TEXT("WindSize"), TEXT("Size"), 1.0, 0.001, 16.0), Num(TEXT("WindContrast"), TEXT("Contrast"), 1.0, 0.0, 4.0), Num(TEXT("Brightness"), TEXT("Brightness"), 1.0, 0.0, 4.0), Num(TEXT("SunAzimuth"), TEXT("Sun Azimuth"), 45.0, -360.0, 360.0), Num(TEXT("SunElevation"), TEXT("Sun Elevation"), 35.0, -90.0, 90.0)};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::WaveShine, EvalWaveShine);
}
