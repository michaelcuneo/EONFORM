#include "GaeaFlowMapNodes.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor FlowMapTerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = DisplayName;
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FGaeaTerrainPortDescriptor FlowMapScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = DisplayName;
		Port.DataType = TEXT("ScalarField");
		return Port;
	}

	FGaeaTerrainParameterDescriptor FlowMapNumber(FName Name, const TCHAR* DisplayName, double Default, double Minimum, double Maximum, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = Default;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor FlowMapInteger(FName Name, const TCHAR* DisplayName, int64 Default, int64 Minimum, int64 Maximum, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EGaeaTerrainParameterType::Integer;
		Parameter.DefaultInteger = Default;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor FlowMapBoolean(FName Name, const TCHAR* DisplayName, bool Default, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = Default;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor FlowMapName(FName Name, const TCHAR* DisplayName, FName Default, std::initializer_list<FName> Options, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = Default;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}

	FGaeaScalarField MakeFlowField(const FGaeaGridDomain& Domain, FName Name, EGaeaInterpolation Interpolation = EGaeaInterpolation::Bilinear)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = Interpolation;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	float Smooth01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float SeedNoise(int32 X, int32 Y, int32 Seed)
	{
		uint32 Hash = static_cast<uint32>(X) * 1597334677u;
		Hash ^= static_cast<uint32>(Y) * 3812015801u;
		Hash ^= static_cast<uint32>(Seed) * 95868953u;
		Hash ^= Hash >> 16u;
		Hash *= 2246822519u;
		Hash ^= Hash >> 13u;
		return static_cast<float>(Hash & 0x00ffffffu) / 16777215.0f;
	}

	bool PrepareHydrology(
		const FGaeaTerrainValue& Input,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainDataset& OutDataset,
		const FGaeaScalarField*& OutDirection,
		const FGaeaScalarField*& OutAccumulation,
		const FGaeaScalarField*& OutCatchment,
		const FGaeaScalarField*& OutDistance,
		const FGaeaScalarField*& OutStreamOrder,
		FString& Error)
	{
		OutDataset = Input.TerrainDataset;
		if (!FGaeaTerrainDerivedData::EnsureHydrology(OutDataset, Input.HeightScale, Context.PhysicalMetrics, &Error)) return false;
		OutDirection = OutDataset.FindScalarField(GaeaTerrainFieldNames::FlowDirection);
		OutAccumulation = OutDataset.FindScalarField(GaeaTerrainFieldNames::FlowAccumulation);
		OutCatchment = OutDataset.FindScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2);
		OutDistance = OutDataset.FindScalarField(GaeaTerrainFieldNames::DistanceToOutletKm);
		OutStreamOrder = OutDataset.FindScalarField(GaeaTerrainFieldNames::StreamOrder);
		if (!OutDirection || !OutAccumulation || !OutCatchment || !OutDistance || !OutStreamOrder)
		{
			Error = TEXT("FlowMap could not resolve the terrain hydrology fields.");
			return false;
		}
		return true;
	}

	bool EvaluateFlowMap(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("FlowMap requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset;
		const FGaeaScalarField* Direction = nullptr;
		const FGaeaScalarField* Accumulation = nullptr;
		const FGaeaScalarField* Catchment = nullptr;
		const FGaeaScalarField* Distance = nullptr;
		const FGaeaScalarField* StreamOrder = nullptr;
		if (!PrepareHydrology(*Input, Context, Dataset, Direction, Accumulation, Catchment, Distance, StreamOrder, Error)) return false;

		const float FlowLength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FlowLength"), 0.65)), 0.0f, 1.0f);
		const float FlowVolume = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FlowVolume"), 0.55)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		double MaximumCatchment = 0.0;
		double MaximumDistance = 0.0;
		for (int32 Y = 0; Y < Catchment->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Catchment->Domain.Dimensions.X; ++X)
			{
				MaximumCatchment = FMath::Max(MaximumCatchment, static_cast<double>(Catchment->AtInterior(X, Y)));
				MaximumDistance = FMath::Max(MaximumDistance, static_cast<double>(Distance->AtInterior(X, Y)));
			}
		}
		MaximumCatchment = FMath::Max(MaximumCatchment, UE_DOUBLE_SMALL_NUMBER);
		MaximumDistance = FMath::Max(MaximumDistance, UE_DOUBLE_SMALL_NUMBER);

		FGaeaScalarField Flow = MakeFlowField(Catchment->Domain, TEXT("FlowMap"));
		FGaeaScalarField DirectionOut = MakeFlowField(Catchment->Domain, TEXT("FlowMapDirection"), EGaeaInterpolation::Nearest);
		FGaeaScalarField AccumulationOut = MakeFlowField(Catchment->Domain, TEXT("FlowMapAccumulation"));

		const double LogMaximumCatchment = FMath::Loge(1.0 + MaximumCatchment);
		const float LengthThreshold = FMath::Lerp(0.42f, 0.01f, FlowLength);
		const float LengthSoftness = FMath::Lerp(0.16f, 0.045f, FlowLength);
		const float VolumeExponent = FMath::Lerp(0.72f, 2.65f, FlowVolume);
		const float VolumeGain = FMath::Lerp(0.8f, 1.45f, FlowVolume);

		for (int32 Y = 0; Y < Catchment->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Catchment->Domain.Dimensions.X; ++X)
			{
				const double Area = FMath::Max(static_cast<double>(Catchment->AtInterior(X, Y)), 0.0);
				const float Catchment01 = static_cast<float>(FMath::Clamp(FMath::Loge(1.0 + Area) / LogMaximumCatchment, 0.0, 1.0));
				const float Distance01 = static_cast<float>(FMath::Clamp(static_cast<double>(Distance->AtInterior(X, Y)) / MaximumDistance, 0.0, 1.0));
				const float Noise = FMath::Lerp(0.94f, 1.06f, SeedNoise(X, Y, Seed));
				const float EffectiveLength = FMath::Clamp(Catchment01 + Distance01 * FlowLength * 0.08f, 0.0f, 1.0f);
				const float Gate = Smooth01((EffectiveLength - LengthThreshold + LengthSoftness) / FMath::Max(LengthSoftness, UE_SMALL_NUMBER));
				const float Accumulated = FMath::Pow(FMath::Max(Catchment01, 0.0f), VolumeExponent) * VolumeGain;
				Flow.AtInterior(X, Y) = FMath::Clamp(Accumulated * Gate * Noise, 0.0f, 1.0f);
				AccumulationOut.AtInterior(X, Y) = Catchment01;
				const float D8 = Direction->AtInterior(X, Y);
				DirectionOut.AtInterior(X, Y) = D8 >= 0.0f ? FMath::Clamp(D8 / 7.0f, 0.0f, 1.0f) : 0.0f;
			}
		}

		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Flow)));
		Out.Outputs.Add(TEXT("Direction"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DirectionOut)));
		Out.Outputs.Add(TEXT("Accumulation"), FGaeaTerrainValue::MakeScalarField(MoveTemp(AccumulationOut)));
		Error.Reset();
		return true;
	}

	float ClassicHierarchyWeight(int32 Order, int32 MaximumOrder, float Primary, float Secondary, float Tertiary, float Quaternary)
	{
		const int32 RankFromTop = MaximumOrder - Order;
		if (RankFromTop <= 0) return Primary;
		if (RankFromTop == 1) return Secondary;
		if (RankFromTop == 2) return Tertiary;
		return Quaternary;
	}

	void RefineClassicMap(FGaeaScalarField& Field, int32 Passes)
	{
		const int32 W = Field.Domain.Dimensions.X;
		const int32 H = Field.Domain.Dimensions.Y;
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			FGaeaScalarField Previous = Field;
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					float Maximum = Previous.AtInterior(X, Y);
					float Sum = Maximum;
					int32 Count = 1;
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							if (DX == 0 && DY == 0) continue;
							const int32 NX = X + DX;
							const int32 NY = Y + DY;
							if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
							const float Value = Previous.AtInterior(NX, NY);
							Maximum = FMath::Max(Maximum, Value);
							Sum += Value;
							++Count;
						}
					}
					Field.AtInterior(X, Y) = FMath::Clamp(FMath::Lerp(Sum / static_cast<float>(Count), Maximum, 0.58f), 0.0f, 1.0f);
				}
			}
		}
	}

	bool EvaluateFlowMapClassic(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("FlowMapClassic requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset;
		const FGaeaScalarField* Direction = nullptr;
		const FGaeaScalarField* Accumulation = nullptr;
		const FGaeaScalarField* Catchment = nullptr;
		const FGaeaScalarField* Distance = nullptr;
		const FGaeaScalarField* StreamOrder = nullptr;
		if (!PrepareHydrology(*Input, Context, Dataset, Direction, Accumulation, Catchment, Distance, StreamOrder, Error)) return false;

		const float Rainfall = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Rainfall"), 0.62)), 0.0f, 1.0f);
		const float Primary = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Primary"), 1.0)), 0.0f, 2.0f);
		const float Secondary = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Secondary"), 0.8)), 0.0f, 2.0f);
		const float Tertiary = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Tertiary"), 0.58)), 0.0f, 2.0f);
		const float Quaternary = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Quaternary"), 0.38)), 0.0f, 2.0f);
		const bool bSimulate2X = Node.GetBool(TEXT("Simulate2X"), false);
		const float Enhance = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Enhance"), 0.35)), 0.0f, 1.0f);
		const FName Quality = Node.GetName(TEXT("Quality"), TEXT("Full"));

		double MaximumCatchment = 0.0;
		int32 MaximumOrder = 1;
		for (int32 Y = 0; Y < Catchment->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Catchment->Domain.Dimensions.X; ++X)
			{
				MaximumCatchment = FMath::Max(MaximumCatchment, static_cast<double>(Catchment->AtInterior(X, Y)));
				MaximumOrder = FMath::Max(MaximumOrder, FMath::RoundToInt(StreamOrder->AtInterior(X, Y)));
			}
		}
		MaximumCatchment = FMath::Max(MaximumCatchment, UE_DOUBLE_SMALL_NUMBER);
		const double LogMaximumCatchment = FMath::Loge(1.0 + MaximumCatchment);

		FGaeaScalarField Flow = MakeFlowField(Catchment->Domain, TEXT("FlowMapClassic"));
		FGaeaScalarField Hierarchy = MakeFlowField(Catchment->Domain, TEXT("FlowMapClassicHierarchy"), EGaeaInterpolation::Nearest);
		const float RainThreshold = FMath::Lerp(0.34f, 0.015f, Rainfall);
		const float RainSoftness = FMath::Lerp(0.12f, 0.045f, Rainfall);

		for (int32 Y = 0; Y < Catchment->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Catchment->Domain.Dimensions.X; ++X)
			{
				const double Area = FMath::Max(static_cast<double>(Catchment->AtInterior(X, Y)), 0.0);
				const float Catchment01 = static_cast<float>(FMath::Clamp(FMath::Loge(1.0 + Area) / LogMaximumCatchment, 0.0, 1.0));
				const int32 Order = FMath::Max(1, FMath::RoundToInt(StreamOrder->AtInterior(X, Y)));
				const float HierarchyWeight = ClassicHierarchyWeight(Order, MaximumOrder, Primary, Secondary, Tertiary, Quaternary);
				const float RainGate = Smooth01((Catchment01 - RainThreshold + RainSoftness) / FMath::Max(RainSoftness, UE_SMALL_NUMBER));
				float Value = Catchment01 * HierarchyWeight * RainGate;
				Value = FMath::Lerp(Value, Smooth01(FMath::Clamp(Value, 0.0f, 1.0f)), Enhance);
				Flow.AtInterior(X, Y) = FMath::Clamp(Value, 0.0f, 1.0f);
				Hierarchy.AtInterior(X, Y) = MaximumOrder > 1 ? FMath::Clamp(static_cast<float>(Order - 1) / static_cast<float>(MaximumOrder - 1), 0.0f, 1.0f) : 1.0f;
			}
		}

		int32 QualityPasses = 3;
		if (Quality == TEXT("Quarter")) QualityPasses = 0;
		else if (Quality == TEXT("Third")) QualityPasses = 1;
		else if (Quality == TEXT("Half")) QualityPasses = 2;
		else if (Quality != TEXT("Full"))
		{
			Error = TEXT("FlowMapClassic Quality must be Quarter, Third, Half, or Full.");
			return false;
		}
		if (bSimulate2X) QualityPasses *= 2;
		RefineClassicMap(Flow, QualityPasses);

		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Flow)));
		Out.Outputs.Add(TEXT("Hierarchy"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Hierarchy)));
		Error.Reset();
		return true;
	}
}

void RegisterGaeaFlowMapNodes()
{
	{
		FGaeaTerrainNodeDescriptor Descriptor;
		Descriptor.Type = GaeaTerrainNodeTypes::FlowMap;
		Descriptor.DisplayName = TEXT("FlowMap");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Builds a modern flow map from EONFORM's physically routed drainage network, exposing normalized D8 direction and physical catchment accumulation alongside the final map.");
		Descriptor.Inputs.Add(FlowMapTerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(FlowMapScalarPort(TEXT("Out"), TEXT("Flow Map")));
		Descriptor.Outputs.Add(FlowMapScalarPort(TEXT("Direction"), TEXT("Direction")));
		Descriptor.Outputs.Add(FlowMapScalarPort(TEXT("Accumulation"), TEXT("Accumulation")));
		Descriptor.Parameters.Add(FlowMapNumber(TEXT("FlowLength"), TEXT("Flow Length"), 0.65, 0.0, 1.0, TEXT("Flow")));
		Descriptor.Parameters.Add(FlowMapNumber(TEXT("FlowVolume"), TEXT("Flow Volume"), 0.55, 0.0, 1.0, TEXT("Flow")));
		Descriptor.Parameters.Add(FlowMapInteger(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Flow")));
		FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
		FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateFlowMap);
	}

	{
		FGaeaTerrainNodeDescriptor Descriptor;
		Descriptor.Type = GaeaTerrainNodeTypes::FlowMapClassic;
		Descriptor.DisplayName = TEXT("FlowMapClassic");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Builds a classic-style flow map from EONFORM hydrology, with rainfall coverage and explicit primary through quaternary Strahler-stream hierarchy controls.");
		Descriptor.Inputs.Add(FlowMapTerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(FlowMapScalarPort(TEXT("Out"), TEXT("Flow Map")));
		Descriptor.Outputs.Add(FlowMapScalarPort(TEXT("Hierarchy"), TEXT("Hierarchy")));
		Descriptor.Parameters.Add(FlowMapNumber(TEXT("Rainfall"), TEXT("Rainfall"), 0.62, 0.0, 1.0, TEXT("Flow")));
		Descriptor.Parameters.Add(FlowMapNumber(TEXT("Primary"), TEXT("Primary"), 1.0, 0.0, 2.0, TEXT("Hierarchy")));
		Descriptor.Parameters.Add(FlowMapNumber(TEXT("Secondary"), TEXT("Secondary"), 0.8, 0.0, 2.0, TEXT("Hierarchy")));
		Descriptor.Parameters.Add(FlowMapNumber(TEXT("Tertiary"), TEXT("Tertiary"), 0.58, 0.0, 2.0, TEXT("Hierarchy")));
		Descriptor.Parameters.Add(FlowMapNumber(TEXT("Quaternary"), TEXT("Quaternary"), 0.38, 0.0, 2.0, TEXT("Hierarchy")));
		Descriptor.Parameters.Add(FlowMapBoolean(TEXT("Simulate2X"), TEXT("Simulate 2 X"), false, TEXT("Quality")));
		Descriptor.Parameters.Add(FlowMapNumber(TEXT("Enhance"), TEXT("Enhance"), 0.35, 0.0, 1.0, TEXT("Quality")));
		Descriptor.Parameters.Add(FlowMapName(TEXT("Quality"), TEXT("Quality"), TEXT("Full"), { TEXT("Quarter"), TEXT("Third"), TEXT("Half"), TEXT("Full") }, TEXT("Quality")));
		FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
		FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateFlowMapClassic);
	}
}
