#include "EonformReferenceFidelityMountainNodes.h"

#include "EonformScalarField.h"
#include "EonformTerrainDerivedData.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "EonformThermalErosion.h"

namespace EonformReferenceFidelityMountain
{
	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = Type; return P;
	}

	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default;
		P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; if (Group) P.Group = Group; return P;
	}

	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Integer; P.DefaultInteger = Default;
		P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); if (Group) P.Group = Group; return P;
	}

	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Boolean; P.DefaultBoolean = Default; if (Group) P.Group = Group; return P;
	}

	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Name; P.DefaultName = Default;
		for (const FName V : Options) P.NameOptions.Add(V); if (Group) P.Group = Group; return P;
	}

	const FEonformTerrainValue* Input(const FEonformTerrainNodeInputs& Inputs, FName Name)
	{
		const FEonformTerrainValue* const* P = Inputs.Find(Name); return P ? *P : nullptr;
	}

	const FEonformScalarField* AsField(const FEonformTerrainValue* Value)
	{
		if (!Value || !Value->IsValid()) return nullptr;
		if (Value->Type == EEonformTerrainValueType::ScalarField) return &Value->ScalarField;
		if (Value->Type == EEonformTerrainValueType::Terrain) return Value->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		return nullptr;
	}

	const FEonformTerrainValue* RequireTerrain(const FEonformTerrainNodeInputs& Inputs, FString& Error)
	{
		const FEonformTerrainValue* Value = Input(Inputs, TEXT("Terrain"));
		if (!Value || Value->Type != EEonformTerrainValueType::Terrain || !Value->IsValid())
		{
			Error = TEXT("Node requires a valid Terrain input.");
			return nullptr;
		}
		const FEonformScalarField* Height = Value->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Terrain input has no valid Height field.");
			return nullptr;
		}
		return Value;
	}

	FEonformScalarField MakeScalar(const FEonformGridDomain& Domain, FName Name)
	{
		FEonformFieldDescriptor D; D.Name = Name; D.Unit = EEonformFieldUnit::Normalized; D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField F; F.Initialize(Domain, D, 0.0f); return F;
	}

	float Sample(const FEonformScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1), FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	float Bilinear(const FEonformScalarField& Field, float X, float Y)
	{
		X = FMath::Clamp(X, 0.0f, static_cast<float>(Field.Domain.Dimensions.X - 1));
		Y = FMath::Clamp(Y, 0.0f, static_cast<float>(Field.Domain.Dimensions.Y - 1));
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, Field.Domain.Dimensions.X - 1), Y1 = FMath::Min(Y0 + 1, Field.Domain.Dimensions.Y - 1);
		const float TX = X - X0, TY = Y - Y0;
		return FMath::Lerp(FMath::Lerp(Field.AtInterior(X0, Y0), Field.AtInterior(X1, Y0), TX), FMath::Lerp(Field.AtInterior(X0, Y1), Field.AtInterior(X1, Y1), TX), TY);
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

	float SmoothNoise(float X, float Y, int32 Seed, uint32 Salt = 0)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float FX = X - X0, FY = Y - Y0;
		const float SX = FX * FX * (3.0f - 2.0f * FX), SY = FY * FY * (3.0f - 2.0f * FY);
		const float A = FMath::Lerp(Hash01(X0, Y0, Seed, Salt), Hash01(X0 + 1, Y0, Seed, Salt), SX);
		const float B = FMath::Lerp(Hash01(X0, Y0 + 1, Seed, Salt), Hash01(X0 + 1, Y0 + 1, Seed, Salt), SX);
		return FMath::Lerp(A, B, SY) * 2.0f - 1.0f;
	}

	float Fbm(float X, float Y, float Frequency, int32 Octaves, float Roughness, int32 Seed, uint32 Salt = 0)
	{
		float Sum = 0.0f, Weight = 0.0f, Amp = 1.0f;
		for (int32 I = 0; I < Octaves; ++I)
		{
			Sum += SmoothNoise(X * Frequency, Y * Frequency, Seed + I * 193, Salt + I * 7919u) * Amp;
			Weight += Amp;
			Frequency *= 2.03f;
			Amp *= Roughness;
		}
		return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
	}

	float Smooth01(float V)
	{
		V = FMath::Clamp(V, 0.0f, 1.0f); return V * V * (3.0f - 2.0f * V);
	}

	float EffectiveSolverHeightScale(const FEonformScalarField& Height, float LegacyHeightScale, const FEonformTerrainEvaluationContext& Context)
	{
		const FVector2d DomainCell = Height.Domain.GetCellSize();
		const double DomainRepresentative = FMath::Max(FMath::Min(FMath::Abs(DomainCell.X), FMath::Abs(DomainCell.Y)), UE_DOUBLE_SMALL_NUMBER);
		const double PhysicalSpacing = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Height.Domain.Dimensions, DomainCell);
		const double PhysicalElevation = Context.PhysicalMetrics.ResolveElevationScaleMeters(LegacyHeightScale);
		return static_cast<float>(FMath::Max(PhysicalElevation / FMath::Max(PhysicalSpacing, UE_DOUBLE_SMALL_NUMBER) * DomainRepresentative, 1.0));
	}

	bool PublishTerrain(FEonformTerrainDataset&& Dataset, float HeightScale, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		if (!FEonformTerrainDerivedData::EnsureContext(Dataset, FMath::Max(HeightScale, 1.0f), Context.PhysicalMetrics, &Error)) return false;
		FEonformTerrainValue Value = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Value.IsValid()) { Error = TEXT("Audited node produced an invalid terrain output."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
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
			if (!Dataset.SetScalarField(MoveTemp(Field))) { Error = TEXT("Audited node could not publish Height."); return false; }
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Prototype.HeightScale)); return true;
		}
		Error = TEXT("Audited node received an unsupported value type."); return false;
	}

	float PhysicalSlopeDegrees(const FEonformScalarField& Height, int32 X, int32 Y, double ElevationMeters, const FVector2d& Spacing)
	{
		const int32 XL = FMath::Max(0, X - 1), XR = FMath::Min(Height.Domain.Dimensions.X - 1, X + 1);
		const int32 YD = FMath::Max(0, Y - 1), YU = FMath::Min(Height.Domain.Dimensions.Y - 1, Y + 1);
		const double GX = static_cast<double>(Height.AtInterior(XR, Y) - Height.AtInterior(XL, Y)) * ElevationMeters / FMath::Max((XR - XL) * Spacing.X, UE_DOUBLE_SMALL_NUMBER);
		const double GY = static_cast<double>(Height.AtInterior(X, YU) - Height.AtInterior(X, YD)) * ElevationMeters / FMath::Max((YU - YD) * Spacing.Y, UE_DOUBLE_SMALL_NUMBER);
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(GX * GX + GY * GY))));
	}

	float Concavity01(const FEonformScalarField& Height, int32 X, int32 Y)
	{
		const float C = Height.AtInterior(X, Y);
		const float Mean = (Sample(Height, X - 1, Y) + Sample(Height, X + 1, Y) + Sample(Height, X, Y - 1) + Sample(Height, X, Y + 1)) * 0.25f;
		return FMath::Clamp((Mean - C) * 16.0f + 0.5f, 0.0f, 1.0f);
	}

	bool EvaluateThermalCommon(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error, bool bClassic)
	{
		const FEonformTerrainValue* InputTerrain = RequireTerrain(Inputs, Error);
		if (!InputTerrain) return false;
		FEonformTerrainDataset Dataset = InputTerrain->TerrainDataset;
		FEonformTerrainDerivedDataSettings Derived;
		if (!FEonformTerrainDerivedData::EnsureHydraulicInputs(Dataset, InputTerrain->HeightScale, Context.PhysicalMetrics, Derived, &Error)) return false;
		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Source) { Error = TEXT("Thermal input has no Height field."); return false; }

		const int32 Duration = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), bClassic ? 12 : 16)), 1, 4096);
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), bClassic ? 0.35 : 0.45)), 0.0f, 1.0f);
		const float Anisotropy = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Anisotropy"), 0.0)), 0.0f, 1.0f);
		const float Angle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Angle"), 34.0)), 0.0f, 89.9f);
		const float SedimentRemoval = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SedimentRemoval"), 0.0)), 0.0f, 1.0f);
		const float Settling = bClassic ? FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Settling"), 0.5)), 0.0f, 1.0f) : 0.68f;
		const int32 Seed = bClassic ? static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337)) : 1337;
		const double AuthoredFeatureMeters = FMath::Clamp(Node.GetNumber(TEXT("FeatureScale"), bClassic ? 1000.0 : 80.0), 1.0, 20000.0);
		const bool bRealScale = bClassic ? Node.GetBool(TEXT("RealScale"), true) : true;
		const double TerrainScale = bClassic ? FMath::Clamp(Node.GetNumber(TEXT("TerrainScale"), 1.0), 0.01, 100.0) : 1.0;
		const float Verticality = bClassic ? FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Verticality"), 1.0)), 0.01f, 10.0f) : 1.0f;
		const double FeatureMeters = bRealScale ? AuthoredFeatureMeters : AuthoredFeatureMeters * TerrainScale;
		const double SampleSpacing = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Source->Domain.Dimensions, Source->Domain.GetCellSize());

		FEonformThermalErosionSettings Settings;
		Settings.Iterations = Duration;
		Settings.TalusAngleDegrees = FMath::Clamp(Angle / Verticality, 0.0f, 89.9f);
		Settings.Strength = Strength;
		Settings.FeatureScaleSamples = static_cast<float>(FeatureMeters / FMath::Max(SampleSpacing, UE_DOUBLE_SMALL_NUMBER));
		Settings.Anisotropy = Anisotropy;
		Settings.Settling = Settling;
		Settings.SedimentRemoval = SedimentRemoval;
		Settings.Seed = Seed;

		FEonformScalarField Height = *Source;
		FEonformScalarField SeedMask;
		const FEonformScalarField* AreaMask = nullptr;
		if (bClassic)
		{
			SeedMask = MakeScalar(Source->Domain, TEXT("ThermalSeedVariation"));
			for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
				{
					SeedMask.AtInterior(X, Y) = 0.88f + Hash01(X, Y, Seed, 0x71u) * 0.12f;
				}
			}
			AreaMask = &SeedMask;
		}

		if (!FEonformThermalErosion::ApplyInPlace(
			Height,
			EffectiveSolverHeightScale(*Source, InputTerrain->HeightScale, Context),
			Settings,
			Dataset.FindScalarField(EonformTerrainFieldNames::Thermal),
			Dataset.FindScalarField(EonformTerrainFieldNames::RockHardness),
			AreaMask,
			&Error)) return false;

		FEonformScalarField Talus = MakeScalar(Source->Domain, TEXT("Talus"));
		for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
			{
				Talus.AtInterior(X, Y) = FMath::Clamp((Height.AtInterior(X, Y) - Source->AtInterior(X, Y)) * 24.0f, 0.0f, 1.0f);
			}
		}
		FEonformScalarField TalusOutput = Talus;
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(Height)) || !Dataset.SetScalarField(Talus)) { Error = TEXT("Thermal could not publish terrain fields."); return false; }
		if (!PublishTerrain(MoveTemp(Dataset), InputTerrain->HeightScale, Context, Out, Error)) return false;
		Out.Outputs.Add(TEXT("Talus"), FEonformTerrainValue::MakeScalarField(MoveTemp(TalusOutput)));
		return true;
	}

	void GradientAt(const FEonformScalarField& Field, int32 X, int32 Y, int32 Radius, float& GX, float& GY)
	{
		GX = 0.5f * (Sample(Field, X + Radius, Y) - Sample(Field, X - Radius, Y)) / FMath::Max(Radius, 1);
		GY = 0.5f * (Sample(Field, X, Y + Radius) - Sample(Field, X, Y - Radius)) / FMath::Max(Radius, 1);
	}

	bool EvaluateSlopeWarp(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* SourceValue = Input(Inputs, TEXT("Input"));
		const FEonformScalarField* Source = AsField(SourceValue);
		if (!SourceValue || !Source) { Error = TEXT("SlopeWarp requires Input."); return false; }
		const FEonformScalarField* GuideInput = AsField(Input(Inputs, TEXT("Guide")));
		if (GuideInput && GuideInput->Domain != Source->Domain) { Error = TEXT("SlopeWarp Guide must share the Input domain."); return false; }
		const FEonformScalarField& Guide = GuideInput ? *GuideInput : *Source;

		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 0.2)), 0.0f, 1.0f);
		const int32 Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 16);
		const float DirectionRadians = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		const bool bNormalized = Node.GetBool(TEXT("Normalized"), true);
		const FName Quality = Node.GetName(TEXT("Quality"), TEXT("High"));
		const FName Antialiasing = Node.GetName(TEXT("Antialiasing"), TEXT("X 4"));
		const int32 GradientRadius = Quality == TEXT("Low") ? 1 : Quality == TEXT("Medium") ? 2 : Quality == TEXT("Ultra") ? 4 : 3;
		const int32 AASamples = Antialiasing == TEXT("Off") ? 1 : Antialiasing == TEXT("X 16") ? 16 : 4;
		const float CS = FMath::Cos(DirectionRadians), SN = FMath::Sin(DirectionRadians);
		const float MaxDisplacement = Intensity * FMath::Min(Source->Domain.Dimensions.X, Source->Domain.Dimensions.Y) * 0.035f;
		FEonformScalarField Current = *Source;

		for (int32 Iter = 0; Iter < Iterations; ++Iter)
		{
			FEonformScalarField Next = Current;
			for (int32 Y = 0; Y < Current.Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Current.Domain.Dimensions.X; ++X)
				{
					float GX = 0.0f, GY = 0.0f;
					GradientAt(Guide, X, Y, GradientRadius, GX, GY);
					const float Magnitude = FMath::Sqrt(GX * GX + GY * GY);
					if (Magnitude <= UE_SMALL_NUMBER) continue;
					float DX = GX, DY = GY;
					if (bNormalized) { DX /= Magnitude; DY /= Magnitude; }
					const float RX = DX * CS - DY * SN;
					const float RY = DX * SN + DY * CS;
					const float D = MaxDisplacement * (bNormalized ? 1.0f : FMath::Clamp(Magnitude * 16.0f, 0.0f, 1.0f));
					const float TargetX = X - RX * D;
					const float TargetY = Y - RY * D;
					if (AASamples == 1)
					{
						Next.AtInterior(X, Y) = Bilinear(Current, TargetX, TargetY);
					}
					else
					{
						const FVector2D Tangent(-RY, RX);
						float Sum = 0.0f;
						for (int32 S = 0; S < AASamples; ++S)
						{
							const float Offset = (static_cast<float>(S) + 0.5f) / AASamples - 0.5f;
							Sum += Bilinear(Current, TargetX + Tangent.X * Offset, TargetY + Tangent.Y * Offset);
						}
						Next.AtInterior(X, Y) = Sum / AASamples;
					}
				}
			}
			Current = MoveTemp(Next);
		}
		return PublishLike(*SourceValue, MoveTemp(Current), Out, Error);
	}

	bool EvaluateOutcrops(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* InputTerrain = RequireTerrain(Inputs, Error);
		if (!InputTerrain) return false;
		const FEonformScalarField& Source = *InputTerrain->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		FEonformScalarField Height = Source;
		const int32 Variations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Variations"), 6)), 1, 32);
		const float Strata = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strata"), 0.25)), 0.0f, 1.0f);
		const float Density = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"), 0.45)), 0.0f, 1.0f);
		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"), 0.55)), 0.0f, 1.0f);
		const float Chipped = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Chipped"), 0.35)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.35)), 0.02f, 1.0f);
		const float OutcropHeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 0.45)), 0.0f, 1.0f);
		const float Rotation = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Rotation"), 0.0)));
		const float MinDim = static_cast<float>(FMath::Min(Source.Domain.Dimensions.X, Source.Domain.Dimensions.Y));
		const float CellSize = FMath::Max(5.0f, Size * MinDim * 0.12f);

		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const int32 CX = FMath::FloorToInt(X / CellSize), CY = FMath::FloorToInt(Y / CellSize);
				float Best = 0.0f;
				for (int32 OY = -1; OY <= 1; ++OY)
				{
					for (int32 OX = -1; OX <= 1; ++OX)
					{
						const int32 GX = CX + OX, GY = CY + OY;
						const float Spawn = Hash01(GX, GY, Seed, 0x11u);
						if (Spawn > Density) continue;
						const int32 Variation = FMath::FloorToInt(Hash01(GX, GY, Seed, 0x19u) * Variations) % Variations;
						const float CenterX = (GX + 0.18f + Hash01(GX, GY, Seed, 0x21u) * 0.64f) * CellSize;
						const float CenterY = (GY + 0.18f + Hash01(GX, GY, Seed, 0x31u) * 0.64f) * CellSize;
						const float Angle = Rotation + Hash01(GX, GY, Seed, 0x41u) * 2.0f * PI;
						const float AxisA = CellSize * FMath::Lerp(0.20f, 0.46f, Hash01(Variation, GX, Seed, 0x51u));
						const float AxisB = AxisA * FMath::Lerp(0.38f, 0.82f, Hash01(Variation, GY, Seed, 0x61u));
						const float DX = X - CenterX, DY = Y - CenterY;
						const float RX = DX * FMath::Cos(Angle) + DY * FMath::Sin(Angle);
						const float RY = -DX * FMath::Sin(Angle) + DY * FMath::Cos(Angle);
						const float R = FMath::Sqrt(FMath::Square(RX / FMath::Max(AxisA, 1.0f)) + FMath::Square(RY / FMath::Max(AxisB, 1.0f)));
						if (R >= 1.0f) continue;
						float Bump = FMath::Pow(1.0f - R, FMath::Lerp(0.55f, 2.8f, Shape));
						const float ChipNoise = 0.5f + 0.5f * Fbm(X / MinDim, Y / MinDim, 45.0f / FMath::Max(Size, 0.02f), 3, 0.55f, Seed + Variation * 101, 0x71u);
						Bump *= FMath::Lerp(1.0f, Smooth01(ChipNoise), Chipped * Smooth01(R * 1.4f));
						if (Strata > UE_SMALL_NUMBER)
						{
							const float Bands = 0.72f + 0.28f * FMath::Abs(FMath::Sin(Bump * PI * FMath::Lerp(18.0f, 4.0f, Strata)));
							Bump *= FMath::Lerp(1.0f, Bands, Strata);
						}
						Best = FMath::Max(Best, Bump);
					}
				}
				Height.AtInterior(X, Y) = FMath::Clamp(Source.AtInterior(X, Y) + Best * OutcropHeight * 0.16f, -1.0f, 1.0f);
			}
		}
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		FEonformTerrainDataset Dataset = InputTerrain->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Height))) { Error = TEXT("Outcrops could not publish Height."); return false; }
		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), InputTerrain->HeightScale);
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}

	bool EvaluateCraggy(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* InputTerrain = RequireTerrain(Inputs, Error);
		if (!InputTerrain) return false;
		const FEonformScalarField& Source = *InputTerrain->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		FEonformScalarField Height = Source;
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.35)), 0.01f, 1.0f);
		const float Depth = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Depth"), 0.45)), 0.0f, 1.0f);
		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"), 0.55)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const int32 W = Source.Domain.Dimensions.X, H = Source.Domain.Dimensions.Y;
		for (int32 Y = 0; Y < H; ++Y)
		{
			const float V = H > 1 ? static_cast<float>(Y) / (H - 1) : 0.0f;
			for (int32 X = 0; X < W; ++X)
			{
				const float U = W > 1 ? static_cast<float>(X) / (W - 1) : 0.0f;
				const float Frequency = FMath::Lerp(52.0f, 7.0f, Size);
				const float N0 = Fbm(U, V, Frequency, 5, 0.53f, Seed, 0x17u);
				const float N1 = Fbm(U, V, Frequency * 1.83f, 4, 0.57f, Seed + 811, 0x31u);
				const float Ridge = FMath::Pow(FMath::Clamp(1.0f - FMath::Abs(N0), 0.0f, 1.0f), FMath::Lerp(0.8f, 3.2f, Shape));
				const float Crack = FMath::Pow(FMath::Clamp(FMath::Abs(N1), 0.0f, 1.0f), FMath::Lerp(2.8f, 0.9f, Shape));
				const float Broken = (Ridge - 0.48f) * 0.70f - Crack * 0.30f;
				Height.AtInterior(X, Y) = FMath::Clamp(Source.AtInterior(X, Y) + Broken * Depth * 0.15f, -1.0f, 1.0f);
			}
		}
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		FEonformTerrainDataset Dataset = InputTerrain->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Height))) { Error = TEXT("Craggy could not publish Height."); return false; }
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), InputTerrain->HeightScale));
		return true;
	}

	bool EvaluateSediments(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* InputTerrain = RequireTerrain(Inputs, Error);
		if (!InputTerrain) return false;
		FEonformTerrainDataset Dataset = InputTerrain->TerrainDataset;
		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		FEonformScalarField Height = *Source;
		FEonformScalarField Deposits = MakeScalar(Source->Domain, EonformTerrainFieldNames::Deposits);
		const FName Type = Node.GetName(TEXT("Type"), TEXT("Classic"));
		const int32 Passes = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Passes"), 4)), 1, 32);
		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.05f, 8.0f);
		const float Angle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Angle"), 32.0)), 0.0f, 80.0f);
		const FName Style = Node.GetName(TEXT("Style"), TEXT("A"));
		const float Grain = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("GrainyDeposits"), 0.25)), 0.0f, 1.0f);
		const FVector2d Spacing = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Source->Domain.Dimensions, Source->Domain.GetCellSize());
		const double ElevationMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(InputTerrain->HeightScale);
		const float TypeAmount = Type == TEXT("Fine") ? 0.55f : Type == TEXT("Bulky") ? 1.65f : 1.0f;
		const int32 Radius = FMath::Clamp(FMath::RoundToInt(Scale * (Type == TEXT("Fine") ? 1.0f : Type == TEXT("Bulky") ? 2.0f : 1.5f)), 1, 10);

		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			const FEonformScalarField Previous = Height;
			for (int32 Y = 0; Y < Previous.Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Previous.Domain.Dimensions.X; ++X)
				{
					const float Slope = PhysicalSlopeDegrees(Previous, X, Y, ElevationMeters, Spacing);
					const float SlopeSuitability = 1.0f - Smooth01(Slope / FMath::Max(Angle, 1.0f));
					float Basin = 0.0f;
					int32 Count = 0;
					const float C = Previous.AtInterior(X, Y);
					for (int32 OY = -Radius; OY <= Radius; ++OY)
					{
						for (int32 OX = -Radius; OX <= Radius; ++OX)
						{
							if (OX == 0 && OY == 0) continue;
							Basin += FMath::Max(Sample(Previous, X + OX, Y + OY) - C, 0.0f);
							++Count;
						}
					}
					Basin = Count > 0 ? FMath::Clamp(Basin / Count * 18.0f, 0.0f, 1.0f) : 0.0f;
					const float StyleBias = Style == TEXT("B") ? FMath::Lerp(0.35f, 1.0f, Basin) : 1.0f;
					const float GrainNoise = 0.5f + 0.5f * SmoothNoise(X / FMath::Max(Scale * 2.0f, 1.0f), Y / FMath::Max(Scale * 2.0f, 1.0f), 1337 + Pass * 101, 0x91u);
					const float GrainFactor = FMath::Lerp(1.0f, FMath::Lerp(0.55f, 1.35f, GrainNoise), Grain);
					const float Amount = FMath::Clamp((SlopeSuitability * 0.62f + Basin * 0.38f) * StyleBias * GrainFactor * TypeAmount * 0.0045f * Scale, 0.0f, 0.028f);
					Height.AtInterior(X, Y) = FMath::Clamp(C + Amount, -1.0f, 1.0f);
					Deposits.AtInterior(X, Y) = FMath::Clamp(Deposits.AtInterior(X, Y) + Amount * 24.0f, 0.0f, 1.0f);
				}
			}
		}
		FEonformScalarField DepositsOut = Deposits;
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(Height)) || !Dataset.SetScalarField(Deposits)) { Error = TEXT("Sediments could not publish fields."); return false; }
		if (!PublishTerrain(MoveTemp(Dataset), InputTerrain->HeightScale, Context, Out, Error)) return false;
		Out.Outputs.Add(TEXT("Deposits"), FEonformTerrainValue::MakeScalarField(MoveTemp(DepositsOut)));
		return true;
	}

	bool EvaluateScree(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* InputTerrain = RequireTerrain(Inputs, Error);
		if (!InputTerrain) return false;
		FEonformTerrainDataset Dataset = InputTerrain->TerrainDataset;
		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const float Stones = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Stones"), 0.5)), 0.0f, 1.0f);
		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.05f, 8.0f);
		const float HeightControl = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 0.35)), 0.0f, 1.0f);
		const float Density = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"), 0.65)), 0.0f, 1.0f);
		const float Spread = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Spread"), 0.5)), 0.0f, 1.0f);
		const float Edge = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Edge"), 0.5)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const FVector2d Spacing = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Source->Domain.Dimensions, Source->Domain.GetCellSize());
		const double ElevationMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(InputTerrain->HeightScale);
		FEonformScalarField Suitability = MakeScalar(Source->Domain, TEXT("ScreeSuitability"));

		for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
			{
				const float Slope = PhysicalSlopeDegrees(*Source, X, Y, ElevationMeters, Spacing);
				const float SlopeBand = Smooth01((Slope - 18.0f) / 27.0f) * (1.0f - Smooth01((Slope - 58.0f) / 24.0f));
				const float GX = FMath::Abs(Sample(*Source, X + 1, Y) - Sample(*Source, X - 1, Y));
				const float GY = FMath::Abs(Sample(*Source, X, Y + 1) - Sample(*Source, X, Y - 1));
				const float EdgeMeasure = FMath::Clamp((GX + GY) * 9.0f + FMath::Abs(Concavity01(*Source, X, Y) - 0.5f), 0.0f, 1.0f);
				Suitability.AtInterior(X, Y) = FMath::Clamp(SlopeBand * FMath::Lerp(0.55f, 1.0f, EdgeMeasure * Edge), 0.0f, 1.0f);
			}
		}

		const int32 SpreadRadius = FMath::Clamp(FMath::RoundToInt(Spread * 8.0f), 0, 8);
		FEonformScalarField SpreadField = Suitability;
		if (SpreadRadius > 0)
		{
			for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
				{
					float Best = 0.0f;
					for (int32 OY = -SpreadRadius; OY <= SpreadRadius; ++OY)
					{
						for (int32 OX = -SpreadRadius; OX <= SpreadRadius; ++OX)
						{
							const float Distance = FMath::Sqrt(static_cast<float>(OX * OX + OY * OY)) / FMath::Max(static_cast<float>(SpreadRadius), 1.0f);
							if (Distance > 1.0f) continue;
							Best = FMath::Max(Best, Sample(Suitability, X + OX, Y + OY) * (1.0f - Smooth01(Distance)));
						}
					}
					SpreadField.AtInterior(X, Y) = Best;
				}
			}
		}

		FEonformScalarField Height = *Source;
		FEonformScalarField Scree = MakeScalar(Source->Domain, TEXT("Scree"));
		const float StoneCell = FMath::Max(1.0f, Scale * FMath::Lerp(0.8f, 5.0f, Stones));
		for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
			{
				const float Noise = Hash01(FMath::FloorToInt(X / StoneCell), FMath::FloorToInt(Y / StoneCell), Seed, 0x51u);
				const float Presence = Noise < Density ? Smooth01((Density - Noise) / FMath::Max(Density, 0.001f)) * SpreadField.AtInterior(X, Y) : 0.0f;
				const float StoneVariation = 0.65f + Hash01(X, Y, Seed, 0x73u) * 0.70f;
				const float LiftMeters = FMath::Lerp(0.05f, 5.5f, HeightControl) * FMath::Lerp(0.55f, 1.4f, Stones) * Presence * StoneVariation;
				Scree.AtInterior(X, Y) = Presence;
				Height.AtInterior(X, Y) = FMath::Clamp(Source->AtInterior(X, Y) + static_cast<float>(LiftMeters / FMath::Max(ElevationMeters, 1.0)), -1.0f, 1.0f);
			}
		}
		FEonformScalarField ScreeOut = Scree;
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(Height)) || !Dataset.SetScalarField(Scree)) { Error = TEXT("Scree could not publish fields."); return false; }
		if (!PublishTerrain(MoveTemp(Dataset), InputTerrain->HeightScale, Context, Out, Error)) return false;
		Out.Outputs.Add(TEXT("Scree"), FEonformTerrainValue::MakeScalarField(MoveTemp(ScreeOut)));
		return true;
	}

	void RegisterThermal()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::ThermalErosion; D.DisplayName = TEXT("Thermal"); D.Category = TEXT("Simulate");
		D.Description = TEXT("Simulates thermal erosion to create talus and debris using physical-scale feature controls.");
		D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Talus"), TEXT("Talus"), TEXT("ScalarField")));
		D.Parameters = {
			Int(TEXT("Duration"), TEXT("Duration"), 12, 1, 4096, TEXT("Erosion")), Num(TEXT("Strength"), TEXT("Strength"), 0.35, 0.0, 1.0, TEXT("Erosion")),
			Num(TEXT("Anisotropy"), TEXT("Anisotropy"), 0.0, 0.0, 1.0, TEXT("Erosion")), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Erosion")),
			Num(TEXT("Angle"), TEXT("Angle"), 34.0, 0.0, 89.9, TEXT("Talus")), Num(TEXT("Settling"), TEXT("Settling"), 0.5, 0.0, 1.0, TEXT("Talus")),
			Num(TEXT("SedimentRemoval"), TEXT("Sediment Removal"), 0.0, 0.0, 1.0, TEXT("Talus")), Num(TEXT("FeatureScale"), TEXT("Feature Scale"), 1000.0, 1.0, 20000.0, TEXT("Scale")),
			Bool(TEXT("RealScale"), TEXT("Real Scale"), true, TEXT("Scale")), Num(TEXT("TerrainScale"), TEXT("Terrain Scale"), 1.0, 0.01, 100.0, TEXT("Scale")),
			Num(TEXT("Verticality"), TEXT("Verticality"), 1.0, 0.01, 10.0, TEXT("Scale")) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, [](const FEonformTerrainNode& N,const FEonformTerrainNodeInputs& I,const FEonformTerrainEvaluationContext& C,FEonformTerrainNodeEvaluation& O,FString& E){return EvaluateThermalCommon(N,I,C,O,E,true);});
	}

	void RegisterThermal2()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::Thermal2; D.DisplayName = TEXT("Thermal2"); D.Category = TEXT("Simulate");
		D.Description = TEXT("Creates physical-scale thermal weathering, talus, debris, and slope breakdown.");
		D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Talus"), TEXT("Talus"), TEXT("ScalarField")));
		D.Parameters = { Int(TEXT("Duration"),TEXT("Duration"),16,1,1024,TEXT("Erosion")), Num(TEXT("Strength"),TEXT("Strength"),0.45,0.0,1.0,TEXT("Erosion")), Num(TEXT("Anisotropy"),TEXT("Anisotropy"),0.0,0.0,1.0,TEXT("Erosion")), Num(TEXT("Angle"),TEXT("Angle"),34.0,0.0,89.9,TEXT("Talus")), Num(TEXT("SedimentRemoval"),TEXT("Sediment Removal"),0.0,0.0,1.0,TEXT("Talus")), Num(TEXT("FeatureScale"),TEXT("Feature Scale"),80.0,1.0,20000.0,TEXT("Scale")) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, [](const FEonformTerrainNode& N,const FEonformTerrainNodeInputs& I,const FEonformTerrainEvaluationContext& C,FEonformTerrainNodeEvaluation& O,FString& E){return EvaluateThermalCommon(N,I,C,O,E,false);});
	}

	void RegisterSlopeWarp()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::SlopeWarp; D.DisplayName = TEXT("SlopeWarp"); D.Category = TEXT("Modify");
		D.Description = TEXT("Applies directional warping based on slopes of the input or Guide field with selectable precision and antialiasing.");
		D.Inputs.Add(Port(TEXT("Input"),TEXT("Input"),TEXT("Any"))); D.Inputs.Add(Port(TEXT("Guide"),TEXT("Guide"),TEXT("Any"))); D.Outputs.Add(Port(TEXT("Out"),TEXT("Out"),TEXT("Any")));
		D.Parameters = { Num(TEXT("Intensity"),TEXT("Intensity"),0.2,0.0,1.0), Int(TEXT("Iterations"),TEXT("Iterations"),1,1,16), Num(TEXT("Direction"),TEXT("Direction"),0.0,-360.0,360.0), Bool(TEXT("Normalized"),TEXT("Normalized"),true), Choice(TEXT("Quality"),TEXT("Quality"),TEXT("High"),{TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Ultra")},TEXT("Quality")), Choice(TEXT("Antialiasing"),TEXT("Antialiasing"),TEXT("X 4"),{TEXT("Off"),TEXT("X 4"),TEXT("X 16")},TEXT("Quality")) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateSlopeWarp);
	}

	void RegisterOutcrops()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::Outcrops; D.DisplayName = TEXT("Outcrops"); D.Category = TEXT("Surface"); D.Description = TEXT("Creates rocky outcrops with controllable coverage, variation, strata, breakage, scale, height, and rotation.");
		D.Inputs.Add(Port(TEXT("Terrain"),TEXT("Input"),TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"),TEXT("Out"),TEXT("Terrain")));
		D.Parameters = { Int(TEXT("Variations"),TEXT("Variations"),6,1,32), Num(TEXT("Strata"),TEXT("Strata"),0.25,0.0,1.0), Num(TEXT("Density"),TEXT("Density"),0.45,0.0,1.0), Num(TEXT("Shape"),TEXT("Shape"),0.55,0.0,1.0), Num(TEXT("Chipped"),TEXT("Chipped"),0.35,0.0,1.0), Int(TEXT("Seed"),TEXT("Seed"),1337,-2147483647,2147483647), Num(TEXT("Size"),TEXT("Size"),0.35,0.02,1.0), Num(TEXT("Height"),TEXT("Height"),0.45,0.0,1.0), Num(TEXT("Rotation"),TEXT("Rotation"),0.0,-360.0,360.0) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateOutcrops);
	}

	void RegisterCraggy()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::Craggy; D.DisplayName = TEXT("Craggy"); D.Category = TEXT("Surface"); D.Description = TEXT("Turns a terrain surface into a craggy, rocky, broken landscape.");
		D.Inputs.Add(Port(TEXT("Terrain"),TEXT("Input"),TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"),TEXT("Out"),TEXT("Terrain")));
		D.Parameters = { Num(TEXT("Size"),TEXT("Size"),0.35,0.01,1.0), Num(TEXT("Depth"),TEXT("Depth"),0.45,0.0,1.0), Num(TEXT("Shape"),TEXT("Shape"),0.55,0.0,1.0), Int(TEXT("Seed"),TEXT("Seed"),1337,-2147483647,2147483647) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateCraggy);
	}

	void RegisterSediments()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::Sediments; D.DisplayName = TEXT("Sediments"); D.Category = TEXT("Simulate"); D.Description = TEXT("Creates a thick layer of generic sedimentation over terrain for sediment, sand, or snow workflows.");
		D.Inputs.Add(Port(TEXT("Terrain"),TEXT("Input"),TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"),TEXT("Out"),TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Deposits"),TEXT("Deposits"),TEXT("ScalarField")));
		D.Parameters = { Choice(TEXT("Type"),TEXT("Type"),TEXT("Classic"),{TEXT("Fine"),TEXT("Bulky"),TEXT("Classic")}), Int(TEXT("Passes"),TEXT("Passes"),4,1,32), Num(TEXT("Scale"),TEXT("Scale"),1.0,0.05,8.0), Num(TEXT("Angle"),TEXT("Angle"),32.0,0.0,80.0), Choice(TEXT("Style"),TEXT("Style"),TEXT("A"),{TEXT("A"),TEXT("B")}), Num(TEXT("GrainyDeposits"),TEXT("Grainy Deposits"),0.25,0.0,1.0) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateSediments);
	}

	void RegisterScree()
	{
		FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::Scree; D.DisplayName = TEXT("Scree"); D.Category = TEXT("Simulate"); D.Description = TEXT("Creates accumulations of loose rock fragments scattered across slopes and terrain edges.");
		D.Inputs.Add(Port(TEXT("Terrain"),TEXT("Input"),TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"),TEXT("Out"),TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Scree"),TEXT("Scree"),TEXT("ScalarField")));
		D.Parameters = { Num(TEXT("Stones"),TEXT("Stones"),0.5,0.0,1.0), Num(TEXT("Scale"),TEXT("Scale"),1.0,0.05,8.0), Num(TEXT("Height"),TEXT("Height"),0.35,0.0,1.0), Num(TEXT("Density"),TEXT("Density"),0.65,0.0,1.0), Num(TEXT("Spread"),TEXT("Spread"),0.5,0.0,1.0), Num(TEXT("Edge"),TEXT("Edge"),0.5,0.0,1.0), Int(TEXT("Seed"),TEXT("Seed"),1337,-2147483647,2147483647) };
		FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateScree);
	}
}

void RegisterEonformReferenceFidelityMountainNodes()
{
	using namespace EonformReferenceFidelityMountain;
	RegisterThermal();
	RegisterThermal2();
	RegisterSlopeWarp();
	RegisterOutcrops();
	RegisterCraggy();
	RegisterSediments();
	RegisterScree();
}
