#include "GaeaSurfaceNodes.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaSurface
{
	FGaeaTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("Terrain");
		return P;
	}

	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		if (Group) P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Values, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Value : Values) P.NameOptions.Add(Value);
		if (Group) P.Group = Group;
		return P;
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
		const uint32 H = Hash(
			static_cast<uint32>(X) * 0x9e3779b9U
			^ static_cast<uint32>(Y) * 0x85ebca6bU
			^ static_cast<uint32>(Seed));
		return static_cast<float>(H & 0x00ffffffU) / 16777215.0f;
	}

	float Smooth(float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float Fbm(float X, float Y, int32 Seed, int32 Octaves, float Frequency = 1.0f, float Persistence = 0.5f)
	{
		float Sum = 0.0f;
		float Weight = 1.0f;
		float WeightSum = 0.0f;
		FVector2D P(X * Frequency, Y * Frequency);
		const FVector2D Offset(static_cast<float>(Seed) * 0.0137f, static_cast<float>(Seed) * 0.0211f);
		for (int32 I = 0; I < Octaves; ++I)
		{
			Sum += FMath::PerlinNoise2D(P + Offset) * Weight;
			WeightSum += Weight;
			Weight *= Persistence;
			P *= 2.0f;
		}
		return WeightSum > UE_SMALL_NUMBER ? Sum / WeightSum : 0.0f;
	}

	float Sample(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(
			FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1),
			FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	float Bilinear(const FGaeaScalarField& Field, double X, double Y)
	{
		const double CX = FMath::Clamp(X, 0.0, static_cast<double>(Field.Domain.Dimensions.X - 1));
		const double CY = FMath::Clamp(Y, 0.0, static_cast<double>(Field.Domain.Dimensions.Y - 1));
		const int32 X0 = FMath::FloorToInt(CX);
		const int32 Y0 = FMath::FloorToInt(CY);
		const int32 X1 = FMath::Min(X0 + 1, Field.Domain.Dimensions.X - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, Field.Domain.Dimensions.Y - 1);
		const float TX = static_cast<float>(CX - static_cast<double>(X0));
		const float TY = static_cast<float>(CY - static_cast<double>(Y0));
		return FMath::Lerp(
			FMath::Lerp(Field.AtInterior(X0, Y0), Field.AtInterior(X1, Y0), TX),
			FMath::Lerp(Field.AtInterior(X0, Y1), Field.AtInterior(X1, Y1), TX),
			TY);
	}

	float LocalSlope(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		const float DX = Sample(Field, X + 1, Y) - Sample(Field, X - 1, Y);
		const float DY = Sample(Field, X, Y + 1) - Sample(Field, X, Y - 1);
		return FMath::Clamp(FMath::Sqrt(DX * DX + DY * DY) * 2.0f, 0.0f, 1.0f);
	}

	float LocalCurvature(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		const float C = Sample(Field, X, Y);
		return (Sample(Field, X - 1, Y) + Sample(Field, X + 1, Y)
			+ Sample(Field, X, Y - 1) + Sample(Field, X, Y + 1)) * 0.25f - C;
	}

	const FGaeaTerrainValue* RequireInput(const FGaeaTerrainNodeInputs& Inputs, FString& Error)
	{
		const FGaeaTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Surface node requires a valid terrain input 'Terrain'.");
			return nullptr;
		}
		if (!Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height))
		{
			Error = TEXT("Surface node input has no Height field.");
			return nullptr;
		}
		return Input;
	}

	bool Publish(const FGaeaTerrainValue& Input, FGaeaScalarField&& Height, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
		FGaeaTerrainDataset Dataset = Input.TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Surface node could not publish Height.");
			return false;
		}
		FGaeaTerrainValue Value = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input.HeightScale);
		if (!Value.IsValid())
		{
			Error = TEXT("Surface node produced invalid terrain.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
	}

	float VoronoiEdge(float X, float Y, int32 Seed, float Scale)
	{
		X *= Scale;
		Y *= Scale;
		const int32 BX = FMath::FloorToInt(X);
		const int32 BY = FMath::FloorToInt(Y);
		float F1 = 1000.0f;
		float F2 = 1000.0f;
		for (int32 CY = BY - 1; CY <= BY + 1; ++CY)
		{
			for (int32 CX = BX - 1; CX <= BX + 1; ++CX)
			{
				const float FX = static_cast<float>(CX) + 0.5f + (Hash01(CX, CY, Seed) - 0.5f) * 0.9f;
				const float FY = static_cast<float>(CY) + 0.5f + (Hash01(CY, CX, Seed + 37) - 0.5f) * 0.9f;
				const float DX = X - FX;
				const float DY = Y - FY;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				if (D < F1) { F2 = F1; F1 = D; }
				else if (D < F2) F2 = D;
			}
		}
		return FMath::Clamp(F2 - F1, 0.0f, 1.0f);
	}

	bool EvaluateSurface(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireInput(Inputs, Error);
		if (!Input) return false;
		const FGaeaScalarField& Source = *Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		FGaeaScalarField Result = Source;
		const int32 W = Source.Domain.Dimensions.X;
		const int32 H = Source.Domain.Dimensions.Y;
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.5)), 0.0f, 2.0f);
		const float Scale = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.001f);

		for (int32 Y = 0; Y < H; ++Y)
		{
			const float V = H > 1 ? static_cast<float>(Y) / static_cast<float>(H - 1) : 0.0f;
			for (int32 X = 0; X < W; ++X)
			{
				const float U = W > 1 ? static_cast<float>(X) / static_cast<float>(W - 1) : 0.0f;
				const float Base = Source.AtInterior(X, Y);
				float Value = Base;

				if (Node.Type == GaeaTerrainNodeTypes::Roughen)
				{
					const float Detail = Fbm(U, V, Seed, 4, 80.0f / Scale, 0.55f);
					Value = Base + Detail * 0.08f * Strength;
				}
				else if (Node.Type == GaeaTerrainNodeTypes::RockNoise)
				{
					float N = Fbm(U, V, Seed, 5, 26.0f / Scale, 0.58f);
					N = 1.0f - FMath::Abs(N);
					Value = Base + (N - 0.5f) * 0.22f * Strength;
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Craggy)
				{
					const float Slope = LocalSlope(Source, X, Y);
					const float Ridges = FMath::Pow(1.0f - FMath::Abs(Fbm(U, V, Seed, 5, 18.0f / Scale, 0.52f)), 2.2f);
					Value = Base + (Ridges - 0.35f) * 0.24f * Strength * FMath::Lerp(0.3f, 1.0f, Slope);
				}
				else if (Node.Type == GaeaTerrainNodeTypes::GroundTexture)
				{
					const float Macro = Fbm(U, V, Seed, 3, 22.0f / Scale, 0.55f);
					const float Micro = Fbm(U, V, Seed + 73, 3, 120.0f / Scale, 0.45f);
					Value = Base + (Macro * 0.055f + Micro * 0.025f) * Strength;
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Bulbous)
				{
					const float Avg = (Sample(Source, X - 1, Y) + Sample(Source, X + 1, Y) + Sample(Source, X, Y - 1) + Sample(Source, X, Y + 1)) * 0.25f;
					const float Rounded = FMath::Lerp(Base, Avg, 0.45f);
					const float Bulge = FMath::Max(0.0f, -LocalCurvature(Source, X, Y));
					Value = FMath::Lerp(Base, Rounded + Bulge * 0.8f, Strength * 0.65f);
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Contours)
				{
					const float Frequency = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Frequency"), 16.0)), 1.0f);
					const float Width = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Width"), 0.15)), 0.01f, 0.49f);
					const float T = FMath::Abs(FMath::Frac((Base * 0.5f + 0.5f) * Frequency) - 0.5f);
					const float Lines = 1.0f - Smooth(FMath::Clamp((T - Width * 0.25f) / Width, 0.0f, 1.0f));
					Value = FMath::Lerp(Base, FMath::Clamp(Base + Lines * 0.08f, -1.0f, 1.0f), Strength);
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Grid)
				{
					const float Frequency = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Frequency"), 12.0)), 1.0f);
					const float GX = 1.0f - FMath::Clamp(FMath::Abs(FMath::Frac(U * Frequency) - 0.5f) * 12.0f, 0.0f, 1.0f);
					const float GY = 1.0f - FMath::Clamp(FMath::Abs(FMath::Frac(V * Frequency) - 0.5f) * 12.0f, 0.0f, 1.0f);
					Value = Base + FMath::Max(GX, GY) * 0.08f * Strength;
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Distress)
				{
					const float N = Fbm(U, V, Seed, 5, 42.0f / Scale, 0.6f);
					const float Chip = FMath::Pow(FMath::Clamp((N + 0.35f) * 1.2f, 0.0f, 1.0f), 3.0f);
					Value = Base - Chip * 0.14f * Strength * (0.35f + LocalSlope(Source, X, Y));
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Pockmarks)
				{
					const float Density = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Density"), 18.0)), 1.0f);
					const int32 CX = FMath::FloorToInt(U * Density);
					const int32 CY = FMath::FloorToInt(V * Density);
					const float FU = FMath::Frac(U * Density) - 0.5f - (Hash01(CX, CY, Seed) - 0.5f) * 0.5f;
					const float FV = FMath::Frac(V * Density) - 0.5f - (Hash01(CY, CX, Seed + 11) - 0.5f) * 0.5f;
					const float Radius = 0.12f + Hash01(CX, CY, Seed + 29) * 0.24f;
					const float Pit = 1.0f - Smooth(FMath::Clamp(FMath::Sqrt(FU * FU + FV * FV) / Radius, 0.0f, 1.0f));
					Value = Base - Pit * 0.18f * Strength;
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Shatter)
				{
					const float Edge = VoronoiEdge(U, V, Seed, 14.0f / Scale);
					const float Crack = 1.0f - Smooth(FMath::Clamp(Edge * 9.0f, 0.0f, 1.0f));
					Value = Base - Crack * 0.16f * Strength;
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Stratify || Node.Type == GaeaTerrainNodeTypes::Sandstone)
				{
					const float Frequency = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Layers"), 18.0)), 2.0f);
					const float Warp = Fbm(U, V, Seed, 3, 5.0f / Scale, 0.55f) * 0.12f * Strength;
					const float H01 = FMath::Clamp(Base * 0.5f + 0.5f + Warp, 0.0f, 1.0f);
					const float Layer = FMath::FloorToFloat(H01 * Frequency) / Frequency;
					float Shaped = FMath::Lerp(H01, Layer, FMath::Clamp(Strength * 0.65f, 0.0f, 1.0f));
					if (Node.Type == GaeaTerrainNodeTypes::Sandstone)
					{
						Shaped += Fbm(U, V, Seed + 97, 4, 65.0f / Scale, 0.5f) * 0.035f * Strength;
					}
					Value = Shaped * 2.0f - 1.0f;
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Steps)
				{
					const float Count = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Steps"), 8.0)), 2.0f);
					const float H01 = FMath::Clamp(Base * 0.5f + 0.5f, 0.0f, 1.0f);
					const float Stepped = FMath::FloorToFloat(H01 * Count) / Count;
					Value = FMath::Lerp(Base, Stepped * 2.0f - 1.0f, Strength);
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Shear)
				{
					const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
					const float Offset = static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.15)) * Strength;
					const double SX = static_cast<double>(X) - static_cast<double>((Base * Offset) * FMath::Cos(Direction) * W);
					const double SY = static_cast<double>(Y) - static_cast<double>((Base * Offset) * FMath::Sin(Direction) * H);
					Value = Bilinear(Source, SX, SY);
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Sand)
				{
					const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 20.0)));
					const float Frequency = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Frequency"), 35.0)), 1.0f) / Scale;
					const float Along = (U * FMath::Cos(Direction) + V * FMath::Sin(Direction)) * Frequency;
					const float Warp = Fbm(U, V, Seed, 3, 8.0f, 0.5f) * 2.5f;
					const float Ripple = FMath::Sin((Along + Warp) * 2.0f * PI);
					Value = Base + Ripple * 0.045f * Strength * (1.0f - LocalSlope(Source, X, Y));
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Outcrops)
				{
					const float Slope = LocalSlope(Source, X, Y);
					const float Curv = FMath::Max(0.0f, -LocalCurvature(Source, X, Y) * 10.0f);
					const float Ridge = FMath::Pow(1.0f - FMath::Abs(Fbm(U, V, Seed, 4, 20.0f / Scale, 0.55f)), 3.0f);
					Value = Base + Ridge * 0.22f * Strength * FMath::Clamp(Slope + Curv, 0.0f, 1.0f);
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Rockscape)
				{
					const float Macro = 1.0f - FMath::Abs(Fbm(U, V, Seed, 5, 12.0f / Scale, 0.58f));
					const float Micro = Fbm(U, V, Seed + 131, 4, 85.0f / Scale, 0.5f);
					Value = Base + ((Macro - 0.55f) * 0.26f + Micro * 0.055f) * Strength;
				}
				else if (Node.Type == GaeaTerrainNodeTypes::Bomber || Node.Type == GaeaTerrainNodeTypes::Stones)
				{
					const float Density = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Density"), Node.Type == GaeaTerrainNodeTypes::Stones ? 28.0 : 16.0)), 1.0f);
					float Stamp = 0.0f;
					const float GX = U * Density;
					const float GY = V * Density;
					const int32 BX = FMath::FloorToInt(GX);
					const int32 BY = FMath::FloorToInt(GY);
					for (int32 CY = BY - 1; CY <= BY + 1; ++CY)
					{
						for (int32 CX = BX - 1; CX <= BX + 1; ++CX)
						{
							const float Chance = Hash01(CX, CY, Seed);
							if (Chance < 0.42f) continue;
							const float PX = static_cast<float>(CX) + 0.5f + (Hash01(CX, CY, Seed + 5) - 0.5f) * 0.7f;
							const float PY = static_cast<float>(CY) + 0.5f + (Hash01(CY, CX, Seed + 9) - 0.5f) * 0.7f;
							const float DX = GX - PX;
							const float DY = GY - PY;
							const float Radius = 0.16f + Hash01(CX, CY, Seed + 17) * 0.28f;
							const float D = FMath::Sqrt(DX * DX + DY * DY) / Radius;
							const float Shape = D < 1.0f ? FMath::Pow(1.0f - D * D, Node.Type == GaeaTerrainNodeTypes::Stones ? 1.6f : 0.8f) : 0.0f;
							Stamp = FMath::Max(Stamp, Shape);
						}
					}
					Value = Base + Stamp * (Node.Type == GaeaTerrainNodeTypes::Stones ? 0.12f : 0.20f) * Strength;
				}

				Result.AtInterior(X, Y) = FMath::Clamp(Value, -1.0f, 1.0f);
			}
		}

		return Publish(*Input, MoveTemp(Result), Out, Error);
	}

	void RegisterOne(FName Type, const TCHAR* Name, const TCHAR* Description, TArray<FGaeaTerrainParameterDescriptor> Parameters)
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = Type;
		D.DisplayName = Name;
		D.Category = TEXT("Surface");
		D.Description = Description;
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Parameters = MoveTemp(Parameters);
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(Type, EvaluateSurface);
	}
}

void RegisterGaeaSurfaceNodes()
{
	using namespace GaeaSurface;
	RegisterOne(GaeaTerrainNodeTypes::Bomber, TEXT("Bomber"), TEXT("Scatters procedural surface stamps across terrain."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 2.0), Num(TEXT("Density"), TEXT("Density"), 16.0, 1.0, 128.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Bulbous, TEXT("Bulbous"), TEXT("Rounds and bulges local terrain forms while preserving macro shape."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 1.0) });
	RegisterOne(GaeaTerrainNodeTypes::Contours, TEXT("Contours"), TEXT("Embosses elevation contour structure onto terrain."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.45, 0.0, 1.0), Num(TEXT("Frequency"), TEXT("Frequency"), 16.0, 1.0, 128.0), Num(TEXT("Width"), TEXT("Width"), 0.15, 0.01, 0.49) });
	RegisterOne(GaeaTerrainNodeTypes::Craggy, TEXT("Craggy"), TEXT("Adds slope-aware broken crag and ridge structure."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.65, 0.0, 2.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Distress, TEXT("Distress"), TEXT("Chips and weathers exposed terrain with multi-scale damage."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 2.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Grid, TEXT("Grid"), TEXT("Adds a controllable procedural grid/rib surface pattern."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.4, 0.0, 1.0), Num(TEXT("Frequency"), TEXT("Frequency"), 12.0, 1.0, 128.0) });
	RegisterOne(GaeaTerrainNodeTypes::GroundTexture, TEXT("GroundTexture"), TEXT("Adds macro and micro ground roughness while preserving landform structure."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.6, 0.0, 2.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Outcrops, TEXT("Outcrops"), TEXT("Builds exposed rock protrusions preferentially on steep and convex terrain."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.7, 0.0, 2.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Pockmarks, TEXT("Pockmarks"), TEXT("Scatters rounded negative pits over the terrain surface."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.55, 0.0, 2.0), Num(TEXT("Density"), TEXT("Density"), 18.0, 1.0, 128.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::RockNoise, TEXT("RockNoise"), TEXT("Adds ridged multi-scale noise suitable for competent rock surfaces."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.6, 0.0, 2.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Rockscape, TEXT("Rockscape"), TEXT("Combines macro rock breakup and micro rock texture."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.7, 0.0, 2.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Roughen, TEXT("Roughen"), TEXT("Adds deterministic high-frequency terrain roughness."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.45, 0.0, 2.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Sand, TEXT("Sand"), TEXT("Adds directional dune and ripple structure preferentially on flatter terrain."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.5, 0.0, 2.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Num(TEXT("Frequency"), TEXT("Frequency"), 35.0, 1.0, 256.0), Num(TEXT("Direction"), TEXT("Direction"), 20.0, -360.0, 360.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Sandstone, TEXT("Sandstone"), TEXT("Builds warped sedimentary strata with granular surface breakup."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.65, 0.0, 1.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Num(TEXT("Layers"), TEXT("Layers"), 18.0, 2.0, 128.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Shatter, TEXT("Shatter"), TEXT("Fractures terrain using cellular crack boundaries."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.6, 0.0, 2.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Shear, TEXT("Shear"), TEXT("Shears terrain laterally as a function of elevation."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 1.0, 0.0, 1.0), Num(TEXT("Amount"), TEXT("Amount"), 0.15, -1.0, 1.0), Num(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0) });
	RegisterOne(GaeaTerrainNodeTypes::Steps, TEXT("Steps"), TEXT("Quantizes elevation into broad stepped surface levels."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.8, 0.0, 1.0), Num(TEXT("Steps"), TEXT("Steps"), 8.0, 2.0, 128.0) });
	RegisterOne(GaeaTerrainNodeTypes::Stones, TEXT("Stones"), TEXT("Scatters rounded positive stone forms over terrain."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.55, 0.0, 2.0), Num(TEXT("Density"), TEXT("Density"), 28.0, 1.0, 192.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
	RegisterOne(GaeaTerrainNodeTypes::Stratify, TEXT("Stratify"), TEXT("Creates warped elevation strata for layered rock construction."),
		{ Num(TEXT("Strength"), TEXT("Strength"), 0.65, 0.0, 1.0), Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 16.0), Num(TEXT("Layers"), TEXT("Layers"), 18.0, 2.0, 128.0), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647) });
}
