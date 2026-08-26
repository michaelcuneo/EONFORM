#include "EonformNetworkProcessNodes.h"

#include "EonformTerrainDerivedData.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformNetworkProcess
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

	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
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
		P.Group = Group;
		return P;
	}

	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
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
		P.Group = Group;
		return P;
	}

	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		P.Group = Group;
		return P;
	}

	FEonformScalarField MakeScalar(const FEonformGridDomain& Domain, FName Name, EEonformFieldUnit Unit = EEonformFieldUnit::Normalized)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
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

	float Smooth01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float Sample(const FEonformScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(
			FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1),
			FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	bool RequireTerrain(
		const FEonformTerrainNodeInputs& Inputs,
		FEonformTerrainDataset& Dataset,
		const FEonformTerrainValue*& Input,
		FString& Error)
	{
		const FEonformTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Network process requires a valid terrain input 'Terrain'.");
			return false;
		}
		Dataset = Input->TerrainDataset;
		return true;
	}

	bool EvaluateAnastomosis(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		FEonformTerrainDataset Dataset;
		const FEonformTerrainValue* Input = nullptr;
		if (!RequireTerrain(Inputs, Dataset, Input, Error)) return false;
		if (!FEonformTerrainDerivedData::EnsureContext(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;
		if (!FEonformTerrainDerivedData::EnsureFlowAnalysis(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;

		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const FEonformScalarField* Catchment = Dataset.FindScalarField(EonformTerrainFieldNames::CatchmentAreaKm2);
		const FEonformScalarField* Slope = Dataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees);
		const FEonformScalarField* Concavity = Dataset.FindScalarField(EonformTerrainFieldNames::Concavity);
		if (!Source || !Catchment || !Slope || !Concavity)
		{
			Error = TEXT("Anastomosis could not resolve Height, CatchmentAreaKm2, SlopeDegrees and Concavity.");
			return false;
		}

		const int32 W = Source->Domain.Dimensions.X;
		const int32 H = Source->Domain.Dimensions.Y;
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		if (ElevationScaleMeters <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Anastomosis could not resolve physical elevation scale.");
			return false;
		}

		float MaxCatchment = 0.0f;
		for (int32 Y = 0; Y < H; ++Y)
			for (int32 X = 0; X < W; ++X)
				MaxCatchment = FMath::Max(MaxCatchment, Catchment->AtInterior(X, Y));

		const float NetworkThreshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("NetworkThreshold"), 0.28)), 0.0f, 1.0f);
		const float FloodplainSlopeDegrees = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FloodplainSlopeDegrees"), 8.0)), 0.1f, 45.0f);
		const float Braiding = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Braiding"), 0.62)), 0.0f, 1.0f);
		const float Reconnection = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Reconnection"), 0.55)), 0.0f, 1.0f);
		const float ChannelDepthMeters = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("ChannelDepthMeters"), 3.0)), 0.0f);
		const int32 WidthCells = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("WidthCells"), 2)), 1, 12);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 7331));
		const bool bAffectHeight = Node.GetBool(TEXT("AffectHeight"), true);

		FEonformScalarField Height = *Source;
		FEonformScalarField Network = MakeScalar(Source->Domain, TEXT("Anastomosis"));
		FEonformScalarField Depth = MakeScalar(Source->Domain, TEXT("AnastomosisDepth"), EEonformFieldUnit::Meters);
		FEonformScalarField Reconnect = MakeScalar(Source->Domain, TEXT("AnastomosisReconnection"));

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const float Catchment01 = MaxCatchment > UE_SMALL_NUMBER
					? FMath::Clamp(FMath::Loge(1.0f + Catchment->AtInterior(X, Y)) / FMath::Loge(1.0f + MaxCatchment), 0.0f, 1.0f)
					: 0.0f;
				if (Catchment01 < NetworkThreshold) continue;

				const float Flatness = 1.0f - Smooth01(Slope->AtInterior(X, Y) / FloodplainSlopeDegrees);
				const float Valley = FMath::Clamp(0.35f + 0.65f * Concavity->AtInterior(X, Y), 0.0f, 1.0f);
				const float BasePotential = FMath::Clamp((Catchment01 - NetworkThreshold) / FMath::Max(1.0f - NetworkThreshold, 0.001f), 0.0f, 1.0f)
					* Flatness * Valley;
				if (BasePotential <= 0.0f) continue;

				for (int32 OY = -WidthCells; OY <= WidthCells; ++OY)
				{
					for (int32 OX = -WidthCells; OX <= WidthCells; ++OX)
					{
						const int32 NX = X + OX;
						const int32 NY = Y + OY;
						if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
						const float Distance = FMath::Sqrt(static_cast<float>(OX * OX + OY * OY));
						if (Distance > static_cast<float>(WidthCells)) continue;
						const float Radial = 1.0f - Distance / FMath::Max(static_cast<float>(WidthCells), 1.0f);
						const float Noise = Hash01(NX, NY, Seed);
						const float Split = FMath::Clamp((Noise - (1.0f - Braiding)) / FMath::Max(Braiding, 0.001f), 0.0f, 1.0f);
						const float Channel = BasePotential * Radial * FMath::Lerp(0.35f, 1.0f, Split);
						Network.AtInterior(NX, NY) = FMath::Max(Network.AtInterior(NX, NY), Channel);
						const float LoopPotential = BasePotential * Radial * Reconnection * (1.0f - FMath::Abs(Noise * 2.0f - 1.0f));
						Reconnect.AtInterior(NX, NY) = FMath::Max(Reconnect.AtInterior(NX, NY), LoopPotential);
					}
				}
			}
		}

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const float Channel = FMath::Clamp(Network.AtInterior(X, Y) + 0.45f * Reconnect.AtInterior(X, Y), 0.0f, 1.0f);
				Network.AtInterior(X, Y) = Channel;
				const float DepthMeters = ChannelDepthMeters * Channel * (0.55f + 0.45f * FMath::Clamp(Concavity->AtInterior(X, Y), 0.0f, 1.0f));
				Depth.AtInterior(X, Y) = DepthMeters;
				if (bAffectHeight && DepthMeters > 0.0f)
				{
					Height.AtInterior(X, Y) = FMath::Clamp(
						Height.AtInterior(X, Y) - static_cast<float>(DepthMeters / ElevationScaleMeters),
						-1.0f,
						1.0f);
				}
			}
		}

		if (bAffectHeight)
		{
			Height.Descriptor.Name = EonformTerrainFieldNames::Height;
			if (!Dataset.SetScalarField(MoveTemp(Height)))
			{
				Error = TEXT("Anastomosis could not publish modified Height.");
				return false;
			}
		}

		FEonformScalarField NetworkOut = Network;
		FEonformScalarField DepthOut = Depth;
		FEonformScalarField ReconnectOut = Reconnect;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Network))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Depth))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Reconnect)))
		{
			Error = TEXT("Anastomosis could not publish network fields.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Network"), FEonformTerrainValue::MakeScalarField(MoveTemp(NetworkOut)));
		Out.Outputs.Add(TEXT("Depth"), FEonformTerrainValue::MakeScalarField(MoveTemp(DepthOut)));
		Out.Outputs.Add(TEXT("Reconnection"), FEonformTerrainValue::MakeScalarField(MoveTemp(ReconnectOut)));
		Error.Reset();
		return true;
	}

	bool EvaluateLichtenberg(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		FEonformTerrainDataset Dataset;
		const FEonformTerrainValue* Input = nullptr;
		if (!RequireTerrain(Inputs, Dataset, Input, Error)) return false;
		if (!FEonformTerrainDerivedData::EnsureContext(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;

		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const FEonformScalarField* Slope = Dataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees);
		const FEonformScalarField* Concavity = Dataset.FindScalarField(EonformTerrainFieldNames::Concavity);
		if (!Source || !Slope || !Concavity)
		{
			Error = TEXT("Lichtenberg could not resolve Height, SlopeDegrees and Concavity.");
			return false;
		}

		const int32 W = Source->Domain.Dimensions.X;
		const int32 H = Source->Domain.Dimensions.Y;
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		if (ElevationScaleMeters <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Lichtenberg could not resolve physical elevation scale.");
			return false;
		}

		const int32 Seeds = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Seeds"), 7)), 1, 128);
		const int32 Steps = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Steps"), 96)), 4, 4096);
		const float BranchChance = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("BranchChance"), 0.18)), 0.0f, 1.0f);
		const float TerrainGuidance = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("TerrainGuidance"), 0.65)), 0.0f, 1.0f);
		const float Persistence = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Persistence"), 0.86)), 0.1f, 1.0f);
		const float CarveDepthMeters = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("CarveDepthMeters"), 2.0)), 0.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 9119));
		const bool bAffectHeight = Node.GetBool(TEXT("AffectHeight"), false);

		FEonformScalarField Height = *Source;
		FEonformScalarField Pattern = MakeScalar(Source->Domain, TEXT("Lichtenberg"));
		FEonformScalarField BranchOrder = MakeScalar(Source->Domain, TEXT("LichtenbergBranchOrder"), EEonformFieldUnit::Scalar);
		FEonformScalarField Depth = MakeScalar(Source->Domain, TEXT("LichtenbergDepth"), EEonformFieldUnit::Meters);

		struct FTip
		{
			int32 X = 0;
			int32 Y = 0;
			int32 DX = 0;
			int32 DY = 1;
			int32 Order = 0;
			float Energy = 1.0f;
		};

		TArray<FTip> Tips;
		Tips.Reserve(Seeds * 4);
		for (int32 I = 0; I < Seeds; ++I)
		{
			const int32 X = FMath::Clamp(FMath::RoundToInt(Hash01(I, 0, Seed) * static_cast<float>(W - 1)), 0, W - 1);
			const int32 Y = FMath::Clamp(FMath::RoundToInt(Hash01(I, 1, Seed) * static_cast<float>(H - 1)), 0, H - 1);
			FTip Tip;
			Tip.X = X;
			Tip.Y = Y;
			Tip.DX = Hash01(I, 2, Seed) > 0.5f ? 1 : -1;
			Tip.DY = Hash01(I, 3, Seed) > 0.5f ? 1 : -1;
			Tips.Add(Tip);
		}

		static const int32 DX8[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		static const int32 DY8[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };

		for (int32 Step = 0; Step < Steps && !Tips.IsEmpty(); ++Step)
		{
			TArray<FTip> NextTips;
			NextTips.Reserve(Tips.Num() * 2);
			for (int32 TipIndex = 0; TipIndex < Tips.Num(); ++TipIndex)
			{
				FTip Tip = Tips[TipIndex];
				if (Tip.Energy < 0.05f) continue;

				Pattern.AtInterior(Tip.X, Tip.Y) = FMath::Max(Pattern.AtInterior(Tip.X, Tip.Y), Tip.Energy);
				BranchOrder.AtInterior(Tip.X, Tip.Y) = FMath::Max(BranchOrder.AtInterior(Tip.X, Tip.Y), static_cast<float>(Tip.Order + 1));

				int32 BestDir = 0;
				float BestScore = -FLT_MAX;
				const float CurrentHeight = Source->AtInterior(Tip.X, Tip.Y);
				for (int32 D = 0; D < 8; ++D)
				{
					const int32 NX = Tip.X + DX8[D];
					const int32 NY = Tip.Y + DY8[D];
					if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
					const float Drop = CurrentHeight - Source->AtInterior(NX, NY);
					const float DirectionDot = static_cast<float>(DX8[D] * Tip.DX + DY8[D] * Tip.DY) * 0.5f;
					const float TerrainScore = Drop * 40.0f + 0.20f * Concavity->AtInterior(NX, NY) + 0.10f * (Slope->AtInterior(NX, NY) / 90.0f);
					const float RandomScore = Hash01(NX + Step * 17, NY + TipIndex * 31, Seed) - 0.5f;
					const float Score = TerrainGuidance * TerrainScore + (1.0f - TerrainGuidance) * RandomScore + 0.20f * DirectionDot;
					if (Score > BestScore)
					{
						BestScore = Score;
						BestDir = D;
					}
				}

				Tip.X += DX8[BestDir];
				Tip.Y += DY8[BestDir];
				if (Tip.X < 0 || Tip.X >= W || Tip.Y < 0 || Tip.Y >= H) continue;
				Tip.DX = DX8[BestDir];
				Tip.DY = DY8[BestDir];
				Tip.Energy *= Persistence;
				NextTips.Add(Tip);

				const float SplitRoll = Hash01(Tip.X + Step, Tip.Y - Step, Seed + Tip.Order * 101);
				if (SplitRoll < BranchChance * Tip.Energy && Tip.Order < 8)
				{
					FTip Branch = Tip;
					const int32 Turn = Hash01(Tip.X, Tip.Y, Seed + Step) > 0.5f ? 2 : -2;
					int32 DirIndex = BestDir + Turn;
					while (DirIndex < 0) DirIndex += 8;
					DirIndex %= 8;
					Branch.DX = DX8[DirIndex];
					Branch.DY = DY8[DirIndex];
					Branch.Order = Tip.Order + 1;
					Branch.Energy *= 0.78f;
					NextTips.Add(Branch);
				}
			}
			Tips = MoveTemp(NextTips);
		}

		float MaxOrder = 1.0f;
		for (const float Value : BranchOrder.Values) MaxOrder = FMath::Max(MaxOrder, Value);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const float P = FMath::Clamp(Pattern.AtInterior(X, Y), 0.0f, 1.0f);
				const float Order01 = FMath::Clamp(BranchOrder.AtInterior(X, Y) / MaxOrder, 0.0f, 1.0f);
				const float DepthMeters = CarveDepthMeters * P * FMath::Lerp(1.0f, 0.45f, Order01);
				Depth.AtInterior(X, Y) = DepthMeters;
				if (bAffectHeight && DepthMeters > 0.0f)
				{
					Height.AtInterior(X, Y) = FMath::Clamp(
						Height.AtInterior(X, Y) - static_cast<float>(DepthMeters / ElevationScaleMeters),
						-1.0f,
						1.0f);
				}
			}
		}

		if (bAffectHeight)
		{
			Height.Descriptor.Name = EonformTerrainFieldNames::Height;
			if (!Dataset.SetScalarField(MoveTemp(Height)))
			{
				Error = TEXT("Lichtenberg could not publish modified Height.");
				return false;
			}
		}

		FEonformScalarField PatternOut = Pattern;
		FEonformScalarField OrderOut = BranchOrder;
		FEonformScalarField DepthOut = Depth;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Pattern))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(BranchOrder))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Depth)))
		{
			Error = TEXT("Lichtenberg could not publish branching fields.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Pattern"), FEonformTerrainValue::MakeScalarField(MoveTemp(PatternOut)));
		Out.Outputs.Add(TEXT("BranchOrder"), FEonformTerrainValue::MakeScalarField(MoveTemp(OrderOut)));
		Out.Outputs.Add(TEXT("Depth"), FEonformTerrainValue::MakeScalarField(MoveTemp(DepthOut)));
		Error.Reset();
		return true;
	}
}

void RegisterEonformNetworkProcessNodes()
{
	using namespace EonformNetworkProcess;

	{
		FEonformTerrainNodeDescriptor D;
		D.Type = EonformTerrainNodeTypes::Anastomosis;
		D.DisplayName = TEXT("Anastomosis");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Forms a braided, reconnecting channel network where physical catchment, low floodplain slope and concave terrain support anastomosing drainage rather than stamping a generic pattern across the terrain.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Network"), TEXT("Network")));
		D.Outputs.Add(ScalarPort(TEXT("Depth"), TEXT("Depth")));
		D.Outputs.Add(ScalarPort(TEXT("Reconnection"), TEXT("Reconnection")));
		D.Parameters.Add(Num(TEXT("NetworkThreshold"), TEXT("Network Threshold"), 0.28, 0.0, 1.0, TEXT("Hydrology")));
		D.Parameters.Add(Num(TEXT("FloodplainSlopeDegrees"), TEXT("Floodplain Slope (deg)"), 8.0, 0.1, 45.0, TEXT("Hydrology")));
		D.Parameters.Add(Num(TEXT("Braiding"), TEXT("Braiding"), 0.62, 0.0, 1.0, TEXT("Pattern")));
		D.Parameters.Add(Num(TEXT("Reconnection"), TEXT("Reconnection"), 0.55, 0.0, 1.0, TEXT("Pattern")));
		D.Parameters.Add(Num(TEXT("ChannelDepthMeters"), TEXT("Channel Depth (m)"), 3.0, 0.0, 100.0, TEXT("Carving")));
		D.Parameters.Add(Int(TEXT("WidthCells"), TEXT("Width (cells)"), 2, 1, 12, TEXT("Pattern")));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 7331, 0, 2147483647, TEXT("Pattern")));
		D.Parameters.Add(Bool(TEXT("AffectHeight"), TEXT("Affect Height"), true, TEXT("Output")));
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(D.Type, EvaluateAnastomosis);
	}

	{
		FEonformTerrainNodeDescriptor D;
		D.Type = EonformTerrainNodeTypes::Lichtenberg;
		D.DisplayName = TEXT("Lichtenberg");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Grows a recursive branching discharge/fracture network whose propagation is biased by terrain gradients and concavity, with optional physical carving into the terrain surface.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Pattern"), TEXT("Pattern")));
		D.Outputs.Add(ScalarPort(TEXT("BranchOrder"), TEXT("Branch Order")));
		D.Outputs.Add(ScalarPort(TEXT("Depth"), TEXT("Depth")));
		D.Parameters.Add(Int(TEXT("Seeds"), TEXT("Seeds"), 7, 1, 128, TEXT("Growth")));
		D.Parameters.Add(Int(TEXT("Steps"), TEXT("Steps"), 96, 4, 4096, TEXT("Growth")));
		D.Parameters.Add(Num(TEXT("BranchChance"), TEXT("Branch Chance"), 0.18, 0.0, 1.0, TEXT("Growth")));
		D.Parameters.Add(Num(TEXT("TerrainGuidance"), TEXT("Terrain Guidance"), 0.65, 0.0, 1.0, TEXT("Growth")));
		D.Parameters.Add(Num(TEXT("Persistence"), TEXT("Persistence"), 0.86, 0.1, 1.0, TEXT("Growth")));
		D.Parameters.Add(Num(TEXT("CarveDepthMeters"), TEXT("Carve Depth (m)"), 2.0, 0.0, 100.0, TEXT("Carving")));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 9119, 0, 2147483647, TEXT("Growth")));
		D.Parameters.Add(Bool(TEXT("AffectHeight"), TEXT("Affect Height"), false, TEXT("Output")));
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		FEonformTerrainNodeRegistry::Register(D.Type, EvaluateLichtenberg);
	}
}
