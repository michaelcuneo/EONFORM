#include "GaeaColorizeNodes.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/DelayedAutoRegister.h"

namespace GaeaTerrainNodeTypes
{
	const FName SatMap(TEXT("SatMap"));
}

namespace GaeaColorize
{
	struct FPaletteStop
	{
		float Position = 0.0f;
		FLinearColor Color = FLinearColor::Black;
	};

	FGaeaTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = Type;
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

	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
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

	FGaeaTerrainParameterDescriptor Range(FName Name, const TCHAR* Label, double MinDefault, double MaxDefault, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Range;
		P.DefaultRangeMin = MinDefault;
		P.DefaultRangeMax = MaxDefault;
		P.bHasMinimum = true;
		P.Minimum = 0.0;
		P.bHasMaximum = true;
		P.Maximum = 1.0;
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

	float HashSigned(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
		H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
		H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U;
		return static_cast<float>(Hash(H) & 0x00ffffffU) / 8388607.5f - 1.0f;
	}

	FLinearColor RGB8(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor(FColor(R, G, B, 255));
	}

	TArray<FPaletteStop> BuildPalette(FName Family, int32 Item)
	{
		const int32 Variant = FMath::Abs(Item) % 4;
		TArray<FPaletteStop> Stops;

		if (Family == TEXT("Arid"))
		{
			Stops = {
				{0.00f, RGB8(48, 39, 29)}, {0.16f, RGB8(92, 69, 43)}, {0.34f, RGB8(145, 104, 60)},
				{0.55f, RGB8(188, 148, 88)}, {0.76f, RGB8(128, 103, 72)}, {1.00f, RGB8(218, 203, 172)} };
		}
		else if (Family == TEXT("Volcanic"))
		{
			Stops = {
				{0.00f, RGB8(19, 20, 18)}, {0.18f, RGB8(38, 41, 37)}, {0.38f, RGB8(66, 61, 52)},
				{0.58f, RGB8(94, 76, 60)}, {0.78f, RGB8(126, 116, 101)}, {1.00f, RGB8(205, 203, 194)} };
		}
		else if (Family == TEXT("Coastal"))
		{
			Stops = {
				{0.00f, RGB8(34, 57, 55)}, {0.13f, RGB8(72, 91, 70)}, {0.28f, RGB8(139, 128, 82)},
				{0.45f, RGB8(86, 104, 59)}, {0.68f, RGB8(67, 80, 53)}, {0.84f, RGB8(117, 112, 91)}, {1.00f, RGB8(202, 199, 181)} };
		}
		else if (Family == TEXT("Tundra"))
		{
			Stops = {
				{0.00f, RGB8(55, 61, 48)}, {0.18f, RGB8(83, 91, 65)}, {0.40f, RGB8(112, 107, 79)},
				{0.62f, RGB8(119, 119, 105)}, {0.80f, RGB8(160, 162, 151)}, {1.00f, RGB8(225, 228, 224)} };
		}
		else if (Family == TEXT("Temperate"))
		{
			Stops = {
				{0.00f, RGB8(37, 51, 32)}, {0.15f, RGB8(57, 77, 41)}, {0.34f, RGB8(78, 96, 50)},
				{0.52f, RGB8(103, 101, 63)}, {0.71f, RGB8(105, 91, 72)}, {0.88f, RGB8(145, 140, 127)}, {1.00f, RGB8(211, 211, 202)} };
		}
		else
		{
			// Alpine is the default: dark vegetated bases, exposed rock, then pale high relief.
			Stops = {
				{0.00f, RGB8(31, 43, 31)}, {0.13f, RGB8(51, 70, 39)}, {0.30f, RGB8(76, 88, 49)},
				{0.48f, RGB8(104, 95, 67)}, {0.65f, RGB8(113, 105, 91)}, {0.82f, RGB8(154, 153, 145)}, {1.00f, RGB8(226, 228, 224)} };
		}

		// Each item is a deterministic variation of the same natural family, not a
		// copied satellite palette. Keep changes restrained enough to preserve the family identity.
		const float HueShift = (static_cast<float>(Variant) - 1.5f) * 4.0f;
		const float SatScale = 0.93f + static_cast<float>(Variant) * 0.045f;
		const float ValueScale = 0.96f + static_cast<float>((Variant + 1) % 4) * 0.025f;
		for (FPaletteStop& Stop : Stops)
		{
			FLinearColor HSV = Stop.Color.LinearRGBToHSV();
			HSV.R = FMath::Fmod(HSV.R + HueShift + 360.0f, 360.0f);
			HSV.G = FMath::Clamp(HSV.G * SatScale, 0.0f, 1.0f);
			HSV.B = FMath::Clamp(HSV.B * ValueScale, 0.0f, 1.0f);
			Stop.Color = HSV.HSVToLinearRGB();
		}
		return Stops;
	}

	FLinearColor SamplePalette(const TArray<FPaletteStop>& Stops, float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		if (Stops.IsEmpty()) return FLinearColor::Black;
		if (T <= Stops[0].Position) return Stops[0].Color;
		for (int32 I = 1; I < Stops.Num(); ++I)
		{
			if (T <= Stops[I].Position)
			{
				const float Span = FMath::Max(Stops[I].Position - Stops[I - 1].Position, UE_SMALL_NUMBER);
				const float A = (T - Stops[I - 1].Position) / Span;
				return FMath::Lerp(Stops[I - 1].Color, Stops[I].Color, A);
			}
		}
		return Stops.Last().Color;
	}

	const FGaeaScalarField* ResolveSource(const FGaeaTerrainValue& Input)
	{
		if (Input.Type == EGaeaTerrainValueType::ScalarField && Input.ScalarField.IsValid()) return &Input.ScalarField;
		if (Input.Type == EGaeaTerrainValueType::Terrain && Input.IsValid())
		{
			return Input.TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		}
		return nullptr;
	}

	float ApplyBias(float T, float Bias)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		Bias = FMath::Clamp(Bias, -1.0f, 1.0f);
		if (FMath::Abs(Bias) <= UE_SMALL_NUMBER) return T;
		const float Exponent = FMath::Pow(4.0f, -Bias);
		return FMath::Pow(T, Exponent);
	}

	bool EvaluateSatMap(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("SatMap requires a valid Terrain or ScalarField input.");
			return false;
		}
		const FGaeaScalarField* Source = ResolveSource(*Input);
		if (!Source || !Source->IsValid())
		{
			Error = TEXT("SatMap could not resolve a valid scalar source from its input.");
			return false;
		}

		const FName Family = Node.GetName(TEXT("Palette"), TEXT("Alpine"));
		const int32 Item = static_cast<int32>(Node.GetInteger(TEXT("Item"), 0));
		const FName Enhance = Node.GetName(TEXT("Enhance"), TEXT("None"));
		const bool bReverse = Node.GetBool(TEXT("Reverse"), false);
		const float RangeMin = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMin"), 0.0)), 0.0f, 1.0f);
		const float RangeMax = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMax"), 1.0)), RangeMin + UE_SMALL_NUMBER, 1.0f);
		const float Bias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Bias"), 0.0)), -1.0f, 1.0f);
		const float Roughness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Roughness"), 0.0)), 0.0f, 1.0f);
		const float Hue = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Hue"), 0.0)), -180.0f, 180.0f);
		const float Saturation = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Saturation"), 1.0)), 0.0f, 2.0f);
		const float Lightness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Lightness"), 1.0)), 0.0f, 2.0f);

		float SourceMin = TNumericLimits<float>::Max();
		float SourceMax = TNumericLimits<float>::Lowest();
		for (const float Value : Source->Values)
		{
			SourceMin = FMath::Min(SourceMin, Value);
			SourceMax = FMath::Max(SourceMax, Value);
		}
		const float SourceSpan = FMath::Max(SourceMax - SourceMin, UE_SMALL_NUMBER);

		TArray<float> Cdf;
		if (Enhance == TEXT("Equalize"))
		{
			constexpr int32 Bins = 256;
			TArray<int32> Histogram;
			Histogram.Init(0, Bins);
			for (const float Value : Source->Values)
			{
				const float N = FMath::Clamp((Value - SourceMin) / SourceSpan, 0.0f, 1.0f);
				++Histogram[FMath::Clamp(FMath::FloorToInt(N * static_cast<float>(Bins - 1)), 0, Bins - 1)];
			}
			Cdf.Init(0.0f, Bins);
			int32 Running = 0;
			const int32 Count = FMath::Max(1, Source->Values.Num());
			for (int32 I = 0; I < Bins; ++I)
			{
				Running += Histogram[I];
				Cdf[I] = static_cast<float>(Running) / static_cast<float>(Count);
			}
		}

		const TArray<FPaletteStop> Palette = BuildPalette(Family, Item);
		FGaeaColorField Color;
		Color.Initialize(Source->Domain, FLinearColor::Black);
		const int32 W = Source->Domain.Dimensions.X;
		const int32 H = Source->Domain.Dimensions.Y;
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				float T = Source->AtInterior(X, Y);
				if (Enhance == TEXT("Autolevel") || Enhance == TEXT("Equalize"))
				{
					T = FMath::Clamp((T - SourceMin) / SourceSpan, 0.0f, 1.0f);
				}
				else
				{
					// Terrain sources commonly use signed normalized heights. Scalar masks are
					// normally 0..1; only remap when a signed value is actually present.
					if (SourceMin < -UE_SMALL_NUMBER) T = T * 0.5f + 0.5f;
					T = FMath::Clamp(T, 0.0f, 1.0f);
				}

				if (Enhance == TEXT("Equalize") && !Cdf.IsEmpty())
				{
					const int32 Bin = FMath::Clamp(FMath::FloorToInt(T * static_cast<float>(Cdf.Num() - 1)), 0, Cdf.Num() - 1);
					T = Cdf[Bin];
				}

				T = FMath::Clamp((T - RangeMin) / (RangeMax - RangeMin), 0.0f, 1.0f);
				T = ApplyBias(T, Bias);
				if (Roughness > UE_SMALL_NUMBER)
				{
					T = FMath::Clamp(T + HashSigned(X, Y, Item + 1973) * Roughness * 0.035f, 0.0f, 1.0f);
				}
				if (bReverse) T = 1.0f - T;

				FLinearColor Result = SamplePalette(Palette, T);
				FLinearColor HSV = Result.LinearRGBToHSV();
				HSV.R = FMath::Fmod(HSV.R + Hue + 360.0f, 360.0f);
				HSV.G = FMath::Clamp(HSV.G * Saturation, 0.0f, 1.0f);
				HSV.B = FMath::Clamp(HSV.B * Lightness, 0.0f, 1.0f);
				Result = HSV.HSVToLinearRGB();
				Result.A = 1.0f;
				Color.AtInterior(X, Y) = Result;
			}
		}

		if (!Color.IsValid())
		{
			Error = TEXT("SatMap produced an invalid Color field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeColor(MoveTemp(Color)));

		// Preserve compatibility with the terrain-only top-level evaluator. SatMap
		// remains a genuine Color producer, while graphs ending on a Terrain-driven
		// SatMap can still be evaluated/materialized by current terrain consumers.
		if (Input->Type == EGaeaTerrainValueType::Terrain)
		{
			Out.Outputs.Add(TEXT("Terrain"), *Input);
		}

		Error.Reset();
		return true;
	}
}

void RegisterGaeaColorizeNodes()
{
	using namespace GaeaColorize;
	FGaeaTerrainNodeDescriptor D;
	D.Type = GaeaTerrainNodeTypes::SatMap;
	D.DisplayName = TEXT("SatMap");
	D.Category = TEXT("Colorize");
	D.Description = TEXT("Maps terrain or scalar values through an EONFORM satellite-inspired natural color palette.");
	D.Inputs.Add(Port(TEXT("Input"), TEXT("Input"), TEXT("Terrain")));
	D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Color")));
	D.Outputs.Add(Port(TEXT("Terrain"), TEXT("Terrain"), TEXT("Terrain")));
	D.Parameters = {
		Choice(TEXT("Palette"), TEXT("Palette"), TEXT("Alpine"), { TEXT("Alpine"), TEXT("Temperate"), TEXT("Arid"), TEXT("Volcanic"), TEXT("Coastal"), TEXT("Tundra") }, TEXT("Library")),
		Int(TEXT("Item"), TEXT("Item"), 0, 0, 255, TEXT("Library")),
		Range(TEXT("Range"), TEXT("Range"), 0.0, 1.0, TEXT("Mapping")),
		Num(TEXT("Bias"), TEXT("Bias"), 0.0, -1.0, 1.0, TEXT("Mapping")),
		Choice(TEXT("Enhance"), TEXT("Enhance"), TEXT("None"), { TEXT("None"), TEXT("Autolevel"), TEXT("Equalize") }, TEXT("Mapping")),
		Bool(TEXT("Reverse"), TEXT("Reverse"), false, TEXT("Mapping")),
		Num(TEXT("Roughness"), TEXT("Roughness"), 0.0, 0.0, 1.0, TEXT("Mapping")),
		Num(TEXT("Hue"), TEXT("Hue"), 0.0, -180.0, 180.0, TEXT("Color")),
		Num(TEXT("Saturation"), TEXT("Saturation"), 1.0, 0.0, 2.0, TEXT("Color")),
		Num(TEXT("Lightness"), TEXT("Lightness"), 1.0, 0.0, 2.0, TEXT("Color"))
	};
	FGaeaTerrainNodeDescriptorRegistry::Register(D);
	FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateSatMap);
}

// GaeaColorizeNodes.cpp is compiled as part of CodenameGaeaCore. Delay automatic
// registration until engine initialization is complete so registry globals in
// other translation units are guaranteed to exist regardless of static-init order.
static FDelayedAutoRegisterHelper GSatMapAutoRegister(
	EDelayedRegisterRunPhase::EndOfEngineInit,
	[]()
	{
		RegisterGaeaColorizeNodes();
	});
