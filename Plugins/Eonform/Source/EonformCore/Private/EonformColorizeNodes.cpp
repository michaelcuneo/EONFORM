#include "EonformColorizeNodes.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "Misc/DelayedAutoRegister.h"

namespace EonformTerrainNodeTypes
{
	const FName SatMap(TEXT("SatMap"));
}

namespace EonformColorize
{
	struct FPaletteStop
	{
		float Position = 0.0f;
		FLinearColor Color = FLinearColor::Black;
	};

	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FEonformTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = Type;
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
		for (const FName Value : Values) P.NameOptions.Add(Value);
		if (Group) P.Group = Group;
		return P;
	}

	FEonformTerrainParameterDescriptor Range(FName Name, const TCHAR* Label, double MinDefault, double MaxDefault, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Range;
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

	TArray<FPaletteStop> BuildSample01Palette()
	{
		// Exact 200-color sample supplied for EONFORM SatMap development.
		// Do not collapse this into sparse control points: the local variations in
		// the measured sequence are part of the palette and must survive sampling.
		static const uint8 Samples[][3] = {
			{26, 34, 53}, {27, 35, 54}, {26, 34, 52}, {26, 34, 52}, {26, 34, 53}, {27, 35, 54}, {27, 35, 54}, {27, 35, 54},
			{27, 35, 54}, {27, 35, 54}, {27, 35, 54}, {28, 35, 53}, {28, 35, 53}, {28, 35, 53}, {28, 35, 53}, {28, 35, 53},
			{28, 35, 53}, {29, 36, 55}, {29, 36, 55}, {29, 36, 55}, {29, 36, 55}, {29, 36, 55}, {29, 36, 55}, {29, 36, 55},
			{29, 37, 56}, {29, 36, 55}, {30, 37, 56}, {30, 37, 56}, {30, 37, 56}, {30, 37, 56}, {31, 38, 57}, {31, 38, 57},
			{33, 39, 58}, {33, 39, 56}, {34, 39, 59}, {34, 39, 59}, {34, 41, 57}, {34, 40, 56}, {34, 40, 56}, {34, 40, 56},
			{35, 42, 58}, {35, 42, 58}, {35, 43, 56}, {36, 44, 57}, {36, 43, 59}, {36, 43, 60}, {36, 43, 60}, {37, 44, 59},
			{37, 45, 57}, {37, 45, 56}, {37, 45, 57}, {38, 46, 58}, {39, 46, 61}, {39, 47, 66}, {39, 47, 66}, {39, 47, 66},
			{41, 50, 67}, {41, 50, 67}, {42, 51, 64}, {44, 50, 63}, {44, 50, 63}, {43, 49, 63}, {44, 50, 64}, {44, 51, 65},
			{43, 51, 66}, {44, 50, 64}, {35, 43, 59}, {29, 37, 54}, {30, 38, 56}, {33, 43, 58}, {35, 44, 59}, {35, 46, 61},
			{34, 46, 62}, {35, 49, 63}, {36, 51, 68}, {37, 55, 75}, {38, 55, 75}, {38, 58, 78}, {36, 62, 87}, {40, 65, 90},
			{41, 62, 82}, {44, 65, 83}, {45, 68, 86}, {45, 66, 87}, {45, 68, 90}, {46, 70, 92}, {45, 66, 90}, {44, 64, 90},
			{43, 66, 92}, {43, 64, 85}, {44, 67, 87}, {41, 68, 86}, {42, 74, 95}, {43, 64, 84}, {43, 61, 78}, {44, 61, 79},
			{44, 61, 79}, {46, 62, 75}, {45, 62, 74}, {45, 64, 73}, {47, 68, 72}, {48, 70, 73}, {49, 76, 72}, {54, 80, 67},
			{68, 76, 69}, {93, 105, 82}, {146, 151, 115}, {151, 92, 55}, {149, 76, 47}, {142, 69, 42}, {140, 68, 44}, {140, 65, 43},
			{139, 64, 41}, {139, 64, 41}, {141, 67, 44}, {141, 67, 43}, {142, 69, 44}, {146, 70, 44}, {147, 71, 44}, {141, 66, 42},
			{147, 66, 44}, {145, 64, 42}, {145, 65, 42}, {150, 70, 46}, {146, 65, 44}, {143, 63, 41}, {144, 64, 41}, {141, 60, 39},
			{145, 65, 43}, {148, 68, 45}, {146, 65, 40}, {149, 67, 43}, {153, 69, 45}, {147, 66, 41}, {148, 66, 42}, {147, 65, 39},
			{148, 67, 41}, {150, 69, 42}, {150, 68, 43}, {144, 63, 43}, {143, 62, 43}, {139, 62, 44}, {138, 61, 43}, {150, 74, 49},
			{143, 68, 43}, {148, 70, 47}, {155, 83, 51}, {162, 87, 52}, {154, 75, 48}, {157, 79, 49}, {155, 76, 48}, {153, 74, 47},
			{152, 72, 45}, {158, 78, 47}, {156, 76, 46}, {175, 93, 57}, {177, 110, 63}, {164, 96, 56}, {167, 93, 54}, {158, 79, 45},
			{162, 82, 46}, {163, 85, 46}, {168, 92, 52}, {180, 108, 56}, {172, 98, 55}, {174, 98, 52}, {173, 101, 53}, {170, 97, 50},
			{176, 103, 55}, {172, 98, 51}, {172, 103, 55}, {177, 114, 65}, {166, 101, 54}, {178, 118, 67}, {189, 129, 73}, {188, 122, 62},
			{180, 110, 54}, {187, 126, 69}, {169, 94, 49}, {168, 119, 63}, {184, 123, 63}, {150, 85, 48}, {145, 81, 49}, {146, 79, 47},
			{149, 85, 45}, {146, 81, 49}, {148, 85, 46}, {183, 122, 67}, {195, 127, 72}, {179, 125, 77}, {166, 116, 61}, {162, 94, 53},
			{188, 117, 62}, {194, 131, 78}, {171, 109, 60}, {175, 117, 66}, {166, 116, 69}, {163, 112, 65}, {169, 119, 72}, {173, 119, 69},
		};

		constexpr int32 SampleCount = UE_ARRAY_COUNT(Samples);
		static_assert(SampleCount == 200, "Sample01 palette must remain a 200-color LUT.");
		TArray<FPaletteStop> Stops;
		Stops.Reserve(SampleCount);
		for (int32 I = 0; I < SampleCount; ++I)
		{
			FPaletteStop Stop;
			Stop.Position = static_cast<float>(I) / static_cast<float>(SampleCount - 1);
			Stop.Color = RGB8(Samples[I][0], Samples[I][1], Samples[I][2]);
			Stops.Add(Stop);
		}
		return Stops;
	}

	TArray<FPaletteStop> BuildPalette(FName Family, int32 Item)
	{
		// Sample01 is the canonical measured 200-color LUT. It is intentionally
		// returned without family/item variation so the supplied sample remains exact.
		if (Family == TEXT("Sample01"))
		{
			return BuildSample01Palette();
		}

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
			Stops = {
				{0.00f, RGB8(31, 43, 31)}, {0.13f, RGB8(51, 70, 39)}, {0.30f, RGB8(76, 88, 49)},
				{0.48f, RGB8(104, 95, 67)}, {0.65f, RGB8(113, 105, 91)}, {0.82f, RGB8(154, 153, 145)}, {1.00f, RGB8(226, 228, 224)} };
		}

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

	const FEonformScalarField* ResolveSource(const FEonformTerrainValue& Input)
	{
		if (Input.Type == EEonformTerrainValueType::ScalarField && Input.ScalarField.IsValid()) return &Input.ScalarField;
		if (Input.Type == EEonformTerrainValueType::Terrain && Input.IsValid())
		{
			return Input.TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		}
		return nullptr;
	}

	FEonformScalarField MakeColorChannel(const FEonformColorField& Color, FName Name, int32 Channel)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;

		FEonformScalarField Field;
		Field.Initialize(Color.Domain, Descriptor, 0.0f);
		for (int32 Y = 0; Y < Color.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Color.Domain.Dimensions.X; ++X)
			{
				const FLinearColor C = Color.AtInterior(X, Y);
				Field.AtInterior(X, Y) = FMath::Clamp(Channel == 0 ? C.R : (Channel == 1 ? C.G : C.B), 0.0f, 1.0f);
			}
		}
		return Field;
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
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("SatMap requires a valid Terrain or ScalarField input.");
			return false;
		}
		const FEonformScalarField* Source = ResolveSource(*Input);
		if (!Source || !Source->IsValid())
		{
			Error = TEXT("SatMap could not resolve a valid scalar source from its input.");
			return false;
		}

		const FName Family = Node.GetName(TEXT("Palette"), TEXT("Sample01"));
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
		FEonformColorField Color;
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

		// SatMap is an authoring/inspection layer. When it receives Terrain, keep
		// the terrain intact and attach the sampled RGB as height-derived render
		// fields so preview and output materialization can display it directly.
		if (Input->Type == EEonformTerrainValueType::Terrain)
		{
			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			FEonformScalarField R = MakeColorChannel(Color, EonformTerrainFieldNames::BaseColorR, 0);
			FEonformScalarField G = MakeColorChannel(Color, EonformTerrainFieldNames::BaseColorG, 1);
			FEonformScalarField B = MakeColorChannel(Color, EonformTerrainFieldNames::BaseColorB, 2);
			if (!Dataset.SetHeightDerivedScalarField(MoveTemp(R))
				|| !Dataset.SetHeightDerivedScalarField(MoveTemp(G))
				|| !Dataset.SetHeightDerivedScalarField(MoveTemp(B)))
			{
				Error = TEXT("SatMap could not attach its preview color to Terrain.");
				return false;
			}

			FEonformTerrainValue DecoratedTerrain = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!DecoratedTerrain.IsValid())
			{
				Error = TEXT("SatMap produced invalid decorated Terrain.");
				return false;
			}
			Out.Outputs.Add(TEXT("Terrain"), MoveTemp(DecoratedTerrain));
		}

		// Retain the explicit Color output for inspection and utility workflows.
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeColor(MoveTemp(Color)));
		Error.Reset();
		return true;
	}
}

void RegisterEonformColorizeNodes()
{
	using namespace EonformColorize;
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::SatMap;
	D.DisplayName = TEXT("SatMap");
	D.Category = TEXT("Colorize");
	D.Description = TEXT("Maps terrain or scalar values through a sampled EONFORM color lookup table and decorates Terrain for visual inspection.");
	D.Inputs.Add(Port(TEXT("Input"), TEXT("Input"), TEXT("Terrain")));
	D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Color")));
	D.Outputs.Add(Port(TEXT("Terrain"), TEXT("Terrain"), TEXT("Terrain")));
	D.Parameters = {
		Choice(TEXT("Palette"), TEXT("Palette"), TEXT("Sample01"), { TEXT("Sample01"), TEXT("Alpine"), TEXT("Temperate"), TEXT("Arid"), TEXT("Volcanic"), TEXT("Coastal"), TEXT("Tundra") }, TEXT("Library")),
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
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(D.Type, EvaluateSatMap);
}

static FDelayedAutoRegisterHelper GSatMapAutoRegister(
	EDelayedRegisterRunPhase::EndOfEngineInit,
	[]()
	{
		RegisterEonformColorizeNodes();
	});
