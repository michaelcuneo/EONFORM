#include "EonformSimulateFoundationNodes.h"

#include "EonformTerrainDerivedData.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformSimulateFoundation
{
	FEonformTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("Terrain");
		return P;
	}

	FEonformTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("ScalarField");
		return P;
	}

	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
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
		if (Group) P.Group = Group;
		return P;
	}

	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
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
		if (Group) P.Group = Group;
		return P;
	}

	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		if (Group) P.Group = Group;
		return P;
	}

	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Values, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName V : Values) P.NameOptions.Add(V);
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
		return static_cast<float>(Hash(
			static_cast<uint32>(X) * 0x9e3779b9U
			^ static_cast<uint32>(Y) * 0x85ebca6bU
			^ static_cast<uint32>(Seed)) & 0x00ffffffU) / 16777215.0f;
	}

	float Smooth(float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float Sample(const FEonformScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(
			FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1),
			FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	float LocalSlope01(const FEonformScalarField& Height, int32 X, int32 Y, double ElevationScaleMeters, const FVector2d& SpacingMeters)
	{
		const double DX = static_cast<double>(Sample(Height, X + 1, Y) - Sample(Height, X - 1, Y)) * ElevationScaleMeters
			/ FMath::Max(2.0 * SpacingMeters.X, UE_DOUBLE_SMALL_NUMBER);
		const double DY = static_cast<double>(Sample(Height, X, Y + 1) - Sample(Height, X, Y - 1)) * ElevationScaleMeters
			/ FMath::Max(2.0 * SpacingMeters.Y, UE_DOUBLE_SMALL_NUMBER);
		const double Angle = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY)));
		return static_cast<float>(FMath::Clamp(Angle / 60.0, 0.0, 1.0));
	}

	float LocalConcavity01(const FEonformScalarField& Height, int32 X, int32 Y)
	{
		const float C = Sample(Height, X, Y);
		const float Average = (Sample(Height, X - 1, Y) + Sample(Height, X + 1, Y)
			+ Sample(Height, X, Y - 1) + Sample(Height, X, Y + 1)) * 0.25f;
		return FMath::Clamp((Average - C) * 12.0f + 0.5f, 0.0f, 1.0f);
	}

	FEonformScalarField MakeScalar(const FEonformGridDomain& Domain, FName Name, EEonformFieldUnit Unit = EEonformFieldUnit::Normalized)
	{
		FEonformFieldDescriptor D;
		D.Name = Name;
		D.Unit = Unit;
		D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, D, 0.0f);
		return Field;
	}

	const FEonformTerrainValue* RequireTerrain(const FEonformTerrainNodeInputs& Inputs, FString& Error)
	{
		const FEonformTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Simulate node requires a valid terrain input 'Terrain'.");
			return nullptr;
		}
		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Simulate node input has no valid Height field.");
			return nullptr;
		}
		return Input;
	}

	const FEonformScalarField* OptionalScalar(const FEonformTerrainNodeInputs& Inputs, FName Name, const FEonformGridDomain& Domain, FString& Error)
	{
		const FEonformTerrainValue* const* Ptr = Inputs.Find(Name);
		const FEonformTerrainValue* Value = Ptr ? *Ptr : nullptr;
		if (!Value) return nullptr;
		if (Value->Type != EEonformTerrainValueType::ScalarField || !Value->ScalarField.IsValid() || Value->ScalarField.Domain != Domain)
		{
			Error = FString::Printf(TEXT("Input '%s' must be a scalar field on the same terrain domain."), *Name.ToString());
			return nullptr;
		}
		return &Value->ScalarField;
	}

	bool PublishTerrain(FEonformTerrainDataset&& Dataset, float HeightScale, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		FEonformTerrainValue Value = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Value.IsValid())
		{
			Error = TEXT("Simulate node produced invalid terrain.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
	}

	bool PrepareHydrology(
		const FEonformTerrainValue& Input,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainDataset& Dataset,
		FString& Error)
	{
		Dataset = Input.TerrainDataset;
		return FEonformTerrainDerivedData::EnsureHydrologyNetwork(
			Dataset,
			Input.HeightScale,
			Context.PhysicalMetrics,
			&Error);
	}

	bool EvaluateHydroFix(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* Input = RequireTerrain(Inputs, Error);
		if (!Input) return false;
		FEonformTerrainDataset Dataset;
		if (!PrepareHydrology(*Input, Context, Dataset, Error)) return false;

		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const FEonformScalarField* Direction = Dataset.FindScalarField(EonformTerrainFieldNames::FlowDirection);
		const FEonformScalarField* Catchment = Dataset.FindScalarField(EonformTerrainFieldNames::CatchmentAreaKm2);
		const FEonformScalarField* Distance = Dataset.FindScalarField(EonformTerrainFieldNames::DistanceToOutletKm);
		if (!Source || !Direction || !Catchment || !Distance)
		{
			Error = TEXT("HydroFix could not resolve the physical drainage network.");
			return false;
		}

		const float Downcutting = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Downcutting"), 0.35)), 0.0f, 1.0f);
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		const float MinimumDropNormalized = static_cast<float>((0.15 + 2.0 * Downcutting) / FMath::Max(ElevationScaleMeters, 1.0));
		const int32 W = Source->Domain.Dimensions.X;
		const int32 H = Source->Domain.Dimensions.Y;
		static const int32 DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		static const int32 DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

		TArray<int32> Order;
		Order.Reserve(W * H);
		for (int32 I = 0; I < W * H; ++I) Order.Add(I);
		Order.Sort([&](int32 A, int32 B)
		{
			return Distance->AtInterior(A % W, A / W) > Distance->AtInterior(B % W, B / W);
		});

		float MaxCatchment = 0.0f;
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X) MaxCatchment = FMath::Max(MaxCatchment, Catchment->AtInterior(X, Y));
		FEonformScalarField Height = *Source;
		for (const int32 Index : Order)
		{
			const int32 X = Index % W;
			const int32 Y = Index / W;
			const int32 D = FMath::RoundToInt(Direction->AtInterior(X, Y));
			if (D < 0 || D > 7) continue;
			const int32 NX = X + DX[D];
			const int32 NY = Y + DY[D];
			if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
			const float FlowWeight = MaxCatchment > UE_SMALL_NUMBER
				? FMath::Clamp(FMath::Loge(1.0f + Catchment->AtInterior(X, Y)) / FMath::Loge(1.0f + MaxCatchment), 0.0f, 1.0f)
				: 0.0f;
			const float RequiredDrop = MinimumDropNormalized * FMath::Lerp(0.25f, 1.0f, FlowWeight);
			const float DesiredDownstream = Height.AtInterior(X, Y) - RequiredDrop;
			Height.AtInterior(NX, NY) = FMath::Min(Height.AtInterior(NX, NY), DesiredDownstream);
		}
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		Dataset.SetScalarField(MoveTemp(Height));
		return PublishTerrain(MoveTemp(Dataset), Input->HeightScale, Out, Error);
	}

	bool EvaluateRivers(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* Input = RequireTerrain(Inputs, Error);
		if (!Input) return false;
		FEonformTerrainDataset Dataset;
		if (!PrepareHydrology(*Input, Context, Dataset, Error)) return false;

		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const FEonformScalarField* Direction = Dataset.FindScalarField(EonformTerrainFieldNames::FlowDirection);
		const FEonformScalarField* Catchment = Dataset.FindScalarField(EonformTerrainFieldNames::CatchmentAreaKm2);
		const FEonformScalarField* Distance = Dataset.FindScalarField(EonformTerrainFieldNames::DistanceToOutletKm);
		if (!Source || !Direction || !Catchment || !Distance)
		{
			Error = TEXT("Rivers could not resolve physical hydrology fields.");
			return false;
		}
		const bool bHeadwatersConnected = Inputs.Contains(TEXT("Headwaters"));
		const FEonformScalarField* HeadwatersMask = OptionalScalar(Inputs, TEXT("Headwaters"), Source->Domain, Error);
		if (bHeadwatersConnected && !HeadwatersMask) return false;

		const float Water = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Water"), 0.55)), 0.0f, 1.0f);
		const float WidthControl = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Width"), 0.45)), 0.0f, 1.0f);
		const float DepthControl = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Depth"), 0.45)), 0.0f, 1.0f);
		const float Downcutting = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Downcutting"), 0.45)), 0.0f, 1.0f);
		const int32 ValleyWidth = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("RiverValleyWidth"), 0)), -4, 4);
		const int32 Headwaters = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Headwaters"), 16)), 1, 256);
		const bool bRenderSurface = Node.GetBool(TEXT("RenderSurface"), false);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		(void)Seed;

		const int32 W = Source->Domain.Dimensions.X;
		const int32 H = Source->Domain.Dimensions.Y;
		float MaxCatchment = 0.0f;
		for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X) MaxCatchment = FMath::Max(MaxCatchment, Catchment->AtInterior(X, Y));
		const double WorldAreaKm2 = Context.PhysicalMetrics.HasWorldDimensions()
			? Context.PhysicalMetrics.WorldWidthMeters * Context.PhysicalMetrics.WorldDepthMeters / 1000000.0
			: FMath::Max(static_cast<double>(MaxCatchment), 1.0);
		const float CellAreaKm2 = static_cast<float>(WorldAreaKm2 / static_cast<double>(FMath::Max(W * H, 1)));
		const float ThresholdArea = FMath::Max(CellAreaKm2 * 4.0f, MaxCatchment / static_cast<float>(FMath::Max(Headwaters * 4, 4)));

		FEonformScalarField River = MakeScalar(Source->Domain, TEXT("River"));
		FEonformScalarField RiverDepth = MakeScalar(Source->Domain, TEXT("RiverDepth"), EEonformFieldUnit::Meters);
		TArray<uint8> Active;
		Active.Init(0, W * H);
		TArray<int32> Order;
		Order.Reserve(W * H);
		for (int32 I = 0; I < W * H; ++I) Order.Add(I);
		Order.Sort([&](int32 A, int32 B)
		{
			return Distance->AtInterior(A % W, A / W) > Distance->AtInterior(B % W, B / W);
		});
		static const int32 DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		static const int32 DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

		for (const int32 Index : Order)
		{
			const int32 X = Index % W;
			const int32 Y = Index / W;
			const float Area = Catchment->AtInterior(X, Y);
			if (Area < ThresholdArea) continue;
			const bool bSeed = !HeadwatersMask || HeadwatersMask->AtInterior(X, Y) > 0.05f;
			if (!bSeed && !Active[Index]) continue;
			Active[Index] = 1;
			const int32 D = FMath::RoundToInt(Direction->AtInterior(X, Y));
			if (D < 0 || D > 7) continue;
			const int32 NX = X + DX[D];
			const int32 NY = Y + DY[D];
			if (NX >= 0 && NX < W && NY >= 0 && NY < H) Active[NY * W + NX] = 1;
		}

		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		const float MaxDepthMeters = FMath::Lerp(0.5f, 28.0f, DepthControl) * FMath::Lerp(0.5f, 1.5f, Water);
		const int32 Radius = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(0.0f, 4.0f, WidthControl)) + FMath::Max(ValleyWidth, 0), 0, 8);
		FEonformScalarField Height = *Source;
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				float Network = 0.0f;
				for (int32 OY = -Radius; OY <= Radius; ++OY)
				{
					for (int32 OX = -Radius; OX <= Radius; ++OX)
					{
						const int32 SX = X + OX;
						const int32 SY = Y + OY;
						if (SX < 0 || SX >= W || SY < 0 || SY >= H || !Active[SY * W + SX]) continue;
						const float R = Radius > 0 ? FMath::Sqrt(static_cast<float>(OX * OX + OY * OY)) / static_cast<float>(Radius + 1) : 0.0f;
						Network = FMath::Max(Network, 1.0f - Smooth(FMath::Clamp(R, 0.0f, 1.0f)));
					}
				}
				if (Radius == 0 && Active[Y * W + X]) Network = 1.0f;
				const float Area = Catchment->AtInterior(X, Y);
				const float AreaWeight = Area > ThresholdArea && MaxCatchment > ThresholdArea
					? FMath::Clamp(FMath::Loge(Area / ThresholdArea) / FMath::Loge(MaxCatchment / ThresholdArea), 0.0f, 1.0f)
					: 0.0f;
				const float RiverWeight = FMath::Clamp(Network * FMath::Lerp(0.45f, 1.0f, AreaWeight), 0.0f, 1.0f);
				const float DepthMeters = MaxDepthMeters * RiverWeight;
				River.AtInterior(X, Y) = RiverWeight;
				RiverDepth.AtInterior(X, Y) = DepthMeters;
				const float CutMeters = DepthMeters + Downcutting * FMath::Lerp(0.5f, 18.0f, AreaWeight) * RiverWeight;
				Height.AtInterior(X, Y) = FMath::Clamp(Source->AtInterior(X, Y) - static_cast<float>(CutMeters / FMath::Max(ElevationScaleMeters, 1.0)), -1.0f, 1.0f);
			}
		}

		FEonformScalarField RiverOutput = River;
		FEonformScalarField DepthOutput = RiverDepth;
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		Dataset.SetScalarField(MoveTemp(Height));
		Dataset.SetScalarField(River);
		Dataset.SetScalarField(RiverDepth);
		if (bRenderSurface)
		{
			FEonformScalarField WaterSurface = MakeScalar(Source->Domain, TEXT("WaterSurface"), EEonformFieldUnit::Meters);
			for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
			{
				WaterSurface.AtInterior(X, Y) = RiverOutput.AtInterior(X, Y) > 0.01f
					? static_cast<float>(Context.PhysicalMetrics.NormalizedHeightToMeters(Source->AtInterior(X, Y), Input->HeightScale))
					: 0.0f;
			}
			Dataset.SetScalarField(MoveTemp(WaterSurface));
		}
		if (!PublishTerrain(MoveTemp(Dataset), Input->HeightScale, Out, Error)) return false;
		Out.Outputs.Add(TEXT("River"), FEonformTerrainValue::MakeScalarField(MoveTemp(RiverOutput)));
		Out.Outputs.Add(TEXT("Depth"), FEonformTerrainValue::MakeScalarField(MoveTemp(DepthOutput)));
		return true;
	}

	bool EvaluateSediments(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* Input = RequireTerrain(Inputs, Error);
		if (!Input) return false;
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const FName Type = Node.GetName(TEXT("Type"), TEXT("Classic"));
		const int32 Passes = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Passes"), 4)), 1, 32);
		const float Scale = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.05f);
		const float Angle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Angle"), 32.0)), 0.0f, 80.0f);
		const FName Style = Node.GetName(TEXT("Style"), TEXT("A"));
		const float Grain = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("GrainyDeposits"), 0.25)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const FVector2d Spacing = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Source->Domain.Dimensions, Source->Domain.GetCellSize());
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		FEonformScalarField Height = *Source;
		FEonformScalarField Deposits = MakeScalar(Source->Domain, EonformTerrainFieldNames::Deposits);
		const float TypeScale = Type == TEXT("Fine") ? 0.55f : Type == TEXT("Bulky") ? 1.55f : 1.0f;
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			const FEonformScalarField Previous = Height;
			for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
				{
					const float Slope = LocalSlope01(Previous, X, Y, ElevationScaleMeters, Spacing);
					const float TargetSlope = FMath::Clamp(Angle / 60.0f, 0.0f, 1.0f);
					const float LowSlope = 1.0f - Smooth(FMath::Clamp(Slope / FMath::Max(TargetSlope, 0.05f), 0.0f, 1.0f));
					const float Concavity = LocalConcavity01(Previous, X, Y);
					const float Noise = (Hash01(X, Y, Seed + Pass * 101) - 0.5f) * Grain;
					float Amount = FMath::Clamp((LowSlope * 0.65f + Concavity * 0.35f + Noise * 0.1f) * 0.006f * TypeScale * Scale, 0.0f, 0.025f);
					if (Style == TEXT("B")) Amount *= FMath::Lerp(0.5f, 1.4f, Concavity);
					Height.AtInterior(X, Y) = FMath::Clamp(Previous.AtInterior(X, Y) + Amount, -1.0f, 1.0f);
					Deposits.AtInterior(X, Y) = FMath::Clamp(Deposits.AtInterior(X, Y) + Amount * 20.0f, 0.0f, 1.0f);
				}
			}
		}
		FEonformScalarField DepositsOutput = Deposits;
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		Dataset.SetScalarField(MoveTemp(Height));
		Dataset.SetScalarField(Deposits);
		if (!PublishTerrain(MoveTemp(Dataset), Input->HeightScale, Out, Error)) return false;
		Out.Outputs.Add(TEXT("Deposits"), FEonformTerrainValue::MakeScalarField(MoveTemp(DepositsOutput)));
		return true;
	}

	bool EvaluateLooseRock(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error,
		bool bScree)
	{
		const FEonformTerrainValue* Input = RequireTerrain(Inputs, Error);
		if (!Input) return false;
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Scale = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.05f);
		const float Density = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"), bScree ? 0.65 : 0.5)), 0.0f, 1.0f);
		const float HeightControl = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), bScree ? 0.35 : 0.4)), 0.0f, 1.0f);
		const FVector2d Spacing = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Source->Domain.Dimensions, Source->Domain.GetCellSize());
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		FEonformScalarField Height = *Source;
		FEonformScalarField Mask = MakeScalar(Source->Domain, bScree ? TEXT("Scree") : TEXT("Debris"));
		for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
			{
				const float Slope = LocalSlope01(*Source, X, Y, ElevationScaleMeters, Spacing);
				const float Concavity = LocalConcavity01(*Source, X, Y);
				const float Edge = FMath::Clamp(FMath::Abs(Sample(*Source, X + 1, Y) - Sample(*Source, X - 1, Y))
					+ FMath::Abs(Sample(*Source, X, Y + 1) - Sample(*Source, X, Y - 1)), 0.0f, 1.0f);
				const float Natural = bScree
					? FMath::Clamp(Slope * 0.65f + Concavity * 0.25f + Edge * 0.35f, 0.0f, 1.0f)
					: FMath::Clamp(Slope * 0.55f + Concavity * 0.45f, 0.0f, 1.0f);
				const float Noise = Hash01(FMath::FloorToInt(static_cast<float>(X) / Scale), FMath::FloorToInt(static_cast<float>(Y) / Scale), Seed);
				const float Presence = Noise < Density ? Natural * Smooth(FMath::Clamp((Density - Noise) / FMath::Max(Density, 0.001f), 0.0f, 1.0f)) : 0.0f;
				Mask.AtInterior(X, Y) = Presence;
				const float LiftMeters = FMath::Lerp(0.1f, bScree ? 2.5f : 4.5f, HeightControl) * Presence;
				Height.AtInterior(X, Y) = FMath::Clamp(Source->AtInterior(X, Y) + static_cast<float>(LiftMeters / FMath::Max(ElevationScaleMeters, 1.0)), -1.0f, 1.0f);
			}
		}
		FEonformScalarField MaskOutput = Mask;
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		Dataset.SetScalarField(MoveTemp(Height));
		Dataset.SetScalarField(Mask);
		if (!PublishTerrain(MoveTemp(Dataset), Input->HeightScale, Out, Error)) return false;
		Out.Outputs.Add(bScree ? TEXT("Scree") : TEXT("Debris"), FEonformTerrainValue::MakeScalarField(MoveTemp(MaskOutput)));
		return true;
	}

	bool EvaluateDebris(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		return EvaluateLooseRock(Node, Inputs, Context, Out, Error, false);
	}

	bool EvaluateScree(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		return EvaluateLooseRock(Node, Inputs, Context, Out, Error, true);
	}
}

void RegisterEonformSimulateFoundationNodes()
{
	using namespace EonformSimulateFoundation;

	{
		FEonformTerrainNodeDescriptor D;
		D.Type = EonformTerrainNodeTypes::HydroFix;
		D.DisplayName = TEXT("HydroFix");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Makes subtle physical drainage adjustments to create longer unbroken flow paths before erosion and other flow processes.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Parameters.Add(Num(TEXT("Downcutting"), TEXT("Downcutting"), 0.35, 0.0, 1.0));
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(D.Type, EvaluateHydroFix);
	}

	{
		FEonformTerrainNodeDescriptor D;
		D.Type = EonformTerrainNodeTypes::Rivers;
		D.DisplayName = TEXT("Rivers");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Builds a physically routed river network, subtly corrects drainage, carves channels and valleys, and exposes river/depth masks.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Inputs.Add(ScalarPort(TEXT("Headwaters"), TEXT("Headwaters Mask")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("River"), TEXT("River")));
		D.Outputs.Add(ScalarPort(TEXT("Depth"), TEXT("Depth")));
		D.Parameters.Add(Num(TEXT("Water"), TEXT("Water"), 0.55, 0.0, 1.0, TEXT("Rivers")));
		D.Parameters.Add(Num(TEXT("Width"), TEXT("Width"), 0.45, 0.0, 1.0, TEXT("Rivers")));
		D.Parameters.Add(Num(TEXT("Depth"), TEXT("Depth"), 0.45, 0.0, 1.0, TEXT("Rivers")));
		D.Parameters.Add(Num(TEXT("Downcutting"), TEXT("Downcutting"), 0.45, 0.0, 1.0, TEXT("Rivers")));
		D.Parameters.Add(Int(TEXT("RiverValleyWidth"), TEXT("River Valley Width"), 0, -4, 4, TEXT("Rivers")));
		D.Parameters.Add(Int(TEXT("Headwaters"), TEXT("Headwaters"), 16, 1, 256, TEXT("Rivers")));
		D.Parameters.Add(Bool(TEXT("RenderSurface"), TEXT("Render Surface"), false, TEXT("Output")));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Rivers")));
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(D.Type, EvaluateRivers);
	}

	{
		FEonformTerrainNodeDescriptor D;
		D.Type = EonformTerrainNodeTypes::Sediments;
		D.DisplayName = TEXT("Sediments");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Accumulates generic sediment preferentially on low-slope and concave terrain for alluvium, sand, snow, and later composite processes.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Deposits"), TEXT("Deposits")));
		D.Parameters.Add(Choice(TEXT("Type"), TEXT("Type"), TEXT("Classic"), { TEXT("Fine"), TEXT("Bulky"), TEXT("Classic") }));
		D.Parameters.Add(Int(TEXT("Passes"), TEXT("Passes"), 4, 1, 32));
		D.Parameters.Add(Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 8.0));
		D.Parameters.Add(Num(TEXT("Angle"), TEXT("Angle"), 32.0, 0.0, 80.0));
		D.Parameters.Add(Choice(TEXT("Style"), TEXT("Style"), TEXT("A"), { TEXT("A"), TEXT("B") }));
		D.Parameters.Add(Num(TEXT("GrainyDeposits"), TEXT("Grainy Deposits"), 0.25, 0.0, 1.0));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(D.Type, EvaluateSediments);
	}

	{
		FEonformTerrainNodeDescriptor D;
		D.Type = EonformTerrainNodeTypes::Debris;
		D.DisplayName = TEXT("Debris");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Simulates loose broken-rock accumulation with slope and concavity biased distribution.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Debris"), TEXT("Debris")));
		D.Parameters.Add(Num(TEXT("Density"), TEXT("Debris Amount"), 0.5, 0.0, 1.0));
		D.Parameters.Add(Num(TEXT("AmountMultiplier"), TEXT("Amount Multiplier"), 1.0, 0.0, 4.0));
		D.Parameters.Add(Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 8.0));
		D.Parameters.Add(Num(TEXT("Friction"), TEXT("Friction"), 0.65, 0.0, 1.0));
		D.Parameters.Add(Num(TEXT("Restitution"), TEXT("Restitution"), 0.1, 0.0, 1.0));
		D.Parameters.Add(Num(TEXT("Size"), TEXT("Size"), 0.5, 0.0, 1.0));
		D.Parameters.Add(Num(TEXT("Height"), TEXT("Height"), 0.4, 0.0, 1.0));
		D.Parameters.Add(Choice(TEXT("Shape"), TEXT("Shape"), TEXT("Sharp"), { TEXT("Rounded"), TEXT("Sharp") }));
		D.Parameters.Add(Bool(TEXT("RenderStillRocks"), TEXT("Render Still Rocks"), true));
		D.Parameters.Add(Choice(TEXT("Distribution"), TEXT("Distribution"), TEXT("Natural"), { TEXT("Uniform"), TEXT("Natural") }));
		D.Parameters.Add(Bool(TEXT("ExportPointCloud"), TEXT("Export Point Cloud"), false));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(D.Type, EvaluateDebris);
	}

	{
		FEonformTerrainNodeDescriptor D;
		D.Type = EonformTerrainNodeTypes::Scree;
		D.DisplayName = TEXT("Scree");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Creates loose rock-fragment accumulations on slopes and terrain edges.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Scree"), TEXT("Scree")));
		D.Parameters.Add(Num(TEXT("Stones"), TEXT("Stones"), 0.5, 0.0, 1.0));
		D.Parameters.Add(Num(TEXT("Scale"), TEXT("Scale"), 1.0, 0.05, 8.0));
		D.Parameters.Add(Num(TEXT("Height"), TEXT("Height"), 0.35, 0.0, 1.0));
		D.Parameters.Add(Num(TEXT("Density"), TEXT("Density"), 0.65, 0.0, 1.0));
		D.Parameters.Add(Num(TEXT("Spread"), TEXT("Spread"), 0.5, 0.0, 1.0));
		D.Parameters.Add(Num(TEXT("Edge"), TEXT("Edge"), 0.5, 0.0, 1.0));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(D.Type, EvaluateScree);
	}
}
