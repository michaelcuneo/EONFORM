#include "EonformModifySpatialNodes.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformModifySpatialNodesPrivate
{
	FEonformTerrainPortDescriptor SpatialPort(FName Name, const TCHAR* Label)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = Label;
		Port.DataType = TEXT("Any");
		return Port;
	}

	FEonformTerrainParameterDescriptor SpatialNumber(FName Name, const TCHAR* Label, double DefaultValue, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = DefaultValue;
		P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max;
		return P;
	}

	FEonformTerrainParameterDescriptor SpatialInteger(FName Name, const TCHAR* Label, int64 DefaultValue, int64 Min, int64 Max)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Integer; P.DefaultInteger = DefaultValue;
		P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max);
		return P;
	}

	FEonformTerrainParameterDescriptor SpatialBool(FName Name, const TCHAR* Label, bool DefaultValue)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Boolean; P.DefaultBoolean = DefaultValue;
		return P;
	}

	FEonformTerrainParameterDescriptor SpatialName(FName Name, const TCHAR* Label, FName DefaultValue, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Name; P.DefaultName = DefaultValue;
		for (const FName Option : Options) P.NameOptions.Add(Option);
		return P;
	}

	const FEonformScalarField* FieldFromValue(const FEonformTerrainValue* Value)
	{
		if (!Value || !Value->IsValid()) return nullptr;
		if (Value->Type == EEonformTerrainValueType::ScalarField) return &Value->ScalarField;
		if (Value->Type == EEonformTerrainValueType::Terrain) return Value->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		return nullptr;
	}

	float SampleClamped(const FEonformScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1), FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	float Hash01(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + static_cast<uint32>(Seed) * 2246822519u;
		H = (H ^ (H >> 13u)) * 1274126177u;
		H ^= H >> 16u;
		return static_cast<float>(H & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
	}

	float SampleGridBilinear(const FEonformScalarField& Field, float X, float Y, bool bMirror)
	{
		const int32 W = Field.Domain.Dimensions.X, H = Field.Domain.Dimensions.Y;
		auto MirrorCoord = [](float V, int32 MaxIndex)
		{
			if (MaxIndex <= 0) return 0.0f;
			const float Period = static_cast<float>(MaxIndex * 2);
			float R = FMath::Fmod(V, Period);
			if (R < 0.0f) R += Period;
			return R > MaxIndex ? Period - R : R;
		};
		if (bMirror) { X = MirrorCoord(X, W - 1); Y = MirrorCoord(Y, H - 1); }
		else { X = FMath::Clamp(X, 0.0f, static_cast<float>(W - 1)); Y = FMath::Clamp(Y, 0.0f, static_cast<float>(H - 1)); }
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, W - 1), Y1 = FMath::Min(Y0 + 1, H - 1);
		const float TX = X - X0, TY = Y - Y0;
		return FMath::Lerp(FMath::Lerp(Field.AtInterior(X0, Y0), Field.AtInterior(X1, Y0), TX), FMath::Lerp(Field.AtInterior(X0, Y1), Field.AtInterior(X1, Y1), TX), TY);
	}

	void GradientAt(const FEonformScalarField& Field, int32 X, int32 Y, float& GX, float& GY)
	{
		GX = 0.5f * (SampleClamped(Field, X + 1, Y) - SampleClamped(Field, X - 1, Y));
		GY = 0.5f * (SampleClamped(Field, X, Y + 1) - SampleClamped(Field, X, Y - 1));
	}

	using FSpatialProcessor = TFunction<bool(const FEonformTerrainNode&, const FEonformScalarField&, const FEonformScalarField*, FEonformScalarField&, FString&)>;

	bool EvaluateSpatial(const TCHAR* Label, const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, FEonformTerrainNodeEvaluation& Out, FString& Error, const FSpatialProcessor& Processor)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid()) { Error = FString::Printf(TEXT("%s requires a valid Input."), Label); return false; }
		const FEonformTerrainValue* const* GuidePtr = Inputs.Find(TEXT("Guide"));
		const FEonformScalarField* Guide = FieldFromValue(GuidePtr ? *GuidePtr : nullptr);
		const FEonformScalarField* Source = FieldFromValue(Input);
		if (!Source || !Source->IsValid()) { Error = FString::Printf(TEXT("%s received no valid scalar field."), Label); return false; }
		FEonformScalarField Result;
		if (!Processor(Node, *Source, Guide, Result, Error)) return false;
		if (Input->Type == EEonformTerrainValueType::ScalarField)
		{
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}
		Result.Descriptor.Name = EonformTerrainFieldNames::Height;
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Result))) { Error = FString::Printf(TEXT("%s could not publish its Height field."), Label); return false; }
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		return true;
	}

	bool FoldField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField*, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Fold received an invalid field."); return false; }
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 1.0f);
		const float Angle = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Angle"), 35.0)));
		const float Frequency = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Frequency"), 4.0)), 1.0f, 32.0f);
		const FVector2D Axis(FMath::Cos(Angle), FMath::Sin(Angle));
		Out = Source;
		const float Span = static_cast<float>(FMath::Max(Source.Domain.Dimensions.X, Source.Domain.Dimensions.Y));
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
		{
			const float Coord = (X * Axis.X + Y * Axis.Y) / FMath::Max(Span, 1.0f);
			const float Fold = 1.0f - 2.0f * FMath::Abs(FMath::Frac(Coord * Frequency) - 0.5f);
			Out.AtInterior(X, Y) = FMath::Clamp(Source.AtInterior(X, Y) + (Fold * 2.0f - 1.0f) * Amount * 0.35f, -1.0f, 1.0f);
		}
		return true;
	}

	bool MeshifyField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField*, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Meshify received an invalid field."); return false; }
		const int32 Cell = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("CellSize"), 4)), 1, 64);
		const float Facet = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Facet"), 0.75)), 0.0f, 1.0f);
		Out = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
		{
			const int32 X0 = (X / Cell) * Cell, Y0 = (Y / Cell) * Cell;
			const float A = SampleClamped(Source, X0, Y0), B = SampleClamped(Source, X0 + Cell, Y0), C = SampleClamped(Source, X0, Y0 + Cell);
			const float U = static_cast<float>(X - X0) / Cell, V = static_cast<float>(Y - Y0) / Cell;
			const float Plane = A + (B - A) * U + (C - A) * V;
			Out.AtInterior(X, Y) = FMath::Lerp(Source.AtInterior(X, Y), FMath::Clamp(Plane, -1.0f, 1.0f), Facet);
		}
		return true;
	}

	bool OrigamiField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField*, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Origami received an invalid field."); return false; }
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Frequency = FMath::Lerp(2.0f, 18.0f, Amount);
		Out = Source;
		const float InvW = 1.0f / FMath::Max(1, Source.Domain.Dimensions.X - 1), InvH = 1.0f / FMath::Max(1, Source.Domain.Dimensions.Y - 1);
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
		{
			const float U = X * InvW, V = Y * InvH;
			const float J = (Hash01(X / 16, Y / 16, Seed) - 0.5f) * 0.25f;
			const float F1 = 1.0f - 2.0f * FMath::Abs(FMath::Frac((U + V + J) * Frequency) - 0.5f);
			const float F2 = 1.0f - 2.0f * FMath::Abs(FMath::Frac((U - V - J) * Frequency * 0.73f) - 0.5f);
			Out.AtInterior(X, Y) = FMath::Clamp(Source.AtInterior(X, Y) + (F1 + F2 - 1.0f) * Amount * 0.28f, -1.0f, 1.0f);
		}
		return true;
	}

	bool SlopeBlurField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField* Guide, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("SlopeBlur received an invalid field."); return false; }
		const FEonformScalarField& G = Guide ? *Guide : Source;
		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 0.5)), 0.0f, 1.0f);
		const int32 Samples = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Samples"), 4)), 1, 16);
		Out = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
		{
			float GX, GY; GradientAt(G, X, Y, GX, GY);
			const float L = FMath::Sqrt(GX * GX + GY * GY);
			if (L <= UE_SMALL_NUMBER) continue;
			GX /= L; GY /= L;
			float Sum = Source.AtInterior(X, Y); int32 Count = 1;
			for (int32 I = 1; I <= Samples; ++I) { const float D = I * Intensity; Sum += SampleGridBilinear(Source, X - GX * D, Y - GY * D, true); ++Count; }
			Out.AtInterior(X, Y) = Sum / Count;
		}
		return true;
	}

	bool SlopeWarpField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField* Guide, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("SlopeWarp received an invalid field."); return false; }
		const FEonformScalarField& G = Guide ? *Guide : Source;
		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 0.2)), 0.0f, 1.0f) * FMath::Min(Source.Domain.Dimensions.X, Source.Domain.Dimensions.Y) * 0.08f;
		const int32 Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 8);
		const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		const bool bNormalized = Node.GetBool(TEXT("Normalized"), true);
		FEonformScalarField Current = Source, Next = Source;
		for (int32 Iter = 0; Iter < Iterations; ++Iter)
		{
			for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float GX, GY; GradientAt(G, X, Y, GX, GY);
				const float CS = FMath::Cos(Direction), SN = FMath::Sin(Direction);
				float DX = GX * CS - GY * SN, DY = GX * SN + GY * CS;
				const float L = FMath::Sqrt(DX * DX + DY * DY);
				if (bNormalized && L > UE_SMALL_NUMBER) { DX /= L; DY /= L; }
				Next.AtInterior(X, Y) = SampleGridBilinear(Current, X - DX * Intensity, Y - DY * Intensity, true);
			}
			Swap(Current, Next);
		}
		Out = MoveTemp(Current);
		return true;
	}

	bool SwirlField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField*, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Swirl received an invalid field."); return false; }
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.05f, 1.0f);
		const float CX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("X"), 0.5)), 0.0f, 1.0f) * (Source.Domain.Dimensions.X - 1);
		const float CY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Y"), 0.5)), 0.0f, 1.0f) * (Source.Domain.Dimensions.Y - 1);
		const float Power = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Power"), 0.65)), -2.0f, 2.0f);
		const float Radius = Size * 0.5f * FMath::Min(Source.Domain.Dimensions.X, Source.Domain.Dimensions.Y);
		Out = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
		{
			const float DX = X - CX, DY = Y - CY, D = FMath::Sqrt(DX * DX + DY * DY);
			if (D >= Radius || Radius <= UE_SMALL_NUMBER) continue;
			const float T = 1.0f - D / Radius, A = -Power * T * T * PI;
			const float CS = FMath::Cos(A), SN = FMath::Sin(A);
			Out.AtInterior(X, Y) = SampleGridBilinear(Source, CX + DX * CS - DY * SN, CY + DX * SN + DY * CS, true);
		}
		return true;
	}

	bool ThermalShaperField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField*, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("ThermalShaper received an invalid field."); return false; }
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), -1.0f, 1.0f);
		const float Talus = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Talus"), 0.08)), 0.001f, 1.0f);
		const int32 Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 2)), 1, 16);
		FEonformScalarField Current = Source, Next = Source;
		for (int32 Iter = 0; Iter < Iterations; ++Iter)
		{
			Next = Current;
			for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float C = Current.AtInterior(X, Y);
				float MinN = C, MaxN = C;
				for (int32 DY = -1; DY <= 1; ++DY) for (int32 DX = -1; DX <= 1; ++DX) if (DX || DY) { const float V = SampleClamped(Current, X + DX, Y + DY); MinN = FMath::Min(MinN, V); MaxN = FMath::Max(MaxN, V); }
				const float Relief = MaxN - MinN;
				if (Relief > Talus) Next.AtInterior(X, Y) = FMath::Clamp(C + Amount * (C - 0.5f * (MinN + MaxN)) * 0.25f, -1.0f, 1.0f);
			}
			Swap(Current, Next);
		}
		Out = MoveTemp(Current);
		return true;
	}

	bool TransposeField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField* Guide, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Transpose received an invalid field."); return false; }
		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("Transpose"));
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 1.0)), 0.0f, 1.0f);
		const bool bFlatten = Node.GetBool(TEXT("Flatten"), false);
		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Threshold"), 0.0)), -1.0f, 1.0f);
		if (Mode != TEXT("Transpose") && Mode != TEXT("Embed") && Mode != TEXT("Insert")) { Error = TEXT("Transpose Mode must be Transpose, Embed, or Insert."); return false; }
		Out = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
		{
			const float Ref = Guide ? SampleGridBilinear(*Guide, static_cast<float>(X), static_cast<float>(Y), true) : SampleGridBilinear(Source, static_cast<float>(Y) * Source.Domain.Dimensions.X / FMath::Max(1, Source.Domain.Dimensions.Y), static_cast<float>(X) * Source.Domain.Dimensions.Y / FMath::Max(1, Source.Domain.Dimensions.X), true);
			const float Base = Source.AtInterior(X, Y);
			float Target = Ref;
			if (Mode == TEXT("Embed")) Target = FMath::Clamp(Base + Ref * 0.35f, -1.0f, 1.0f);
			else if (Mode == TEXT("Insert")) Target = Ref >= Threshold ? FMath::Clamp(Base + (Ref - Threshold), -1.0f, 1.0f) : Base;
			if (bFlatten) Target = FMath::Lerp(Target, Base, 0.5f);
			Out.AtInterior(X, Y) = FMath::Lerp(Base, Target, Amount);
		}
		return true;
	}

	bool VariableBlurField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField* Guide, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("VariableBlur received an invalid field."); return false; }
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 1.0)), 0.0f, 1.0f);
		const int32 MaxRadius = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Radius"), 6)), 1, 24);
		Out = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
		{
			const float Control = Guide ? FMath::Clamp(SampleGridBilinear(*Guide, static_cast<float>(X), static_cast<float>(Y), true) * 0.5f + 0.5f, 0.0f, 1.0f) : FMath::Clamp(FMath::Abs(Source.AtInterior(X, Y)), 0.0f, 1.0f);
			const int32 Radius = FMath::Clamp(FMath::RoundToInt(Control * MaxRadius), 0, MaxRadius);
			if (Radius == 0) continue;
			float Sum = 0.0f; int32 Count = 0;
			for (int32 DY = -Radius; DY <= Radius; ++DY) for (int32 DX = -Radius; DX <= Radius; ++DX) { Sum += SampleClamped(Source, X + DX, Y + DY); ++Count; }
			Out.AtInterior(X, Y) = FMath::Lerp(Source.AtInterior(X, Y), Count > 0 ? Sum / Count : Source.AtInterior(X, Y), Amount);
		}
		return true;
	}

	bool WhorlField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, const FEonformScalarField*, FEonformScalarField& Out, FString& Error)
	{
		if (!Source.IsValid()) { Error = TEXT("Whorl received an invalid field."); return false; }
		const FName Type = Node.GetName(TEXT("Type"), TEXT("Stretch"));
		const int32 Count = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Whorls"), 4)), 1, 32);
		const float Power = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Power"), 0.35)), -2.0f, 2.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		if (Type != TEXT("Stretch") && Type != TEXT("Spin")) { Error = TEXT("Whorl Type must be Stretch or Spin."); return false; }
		Out = Source;
		const float MinDim = static_cast<float>(FMath::Min(Source.Domain.Dimensions.X, Source.Domain.Dimensions.Y));
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
		{
			FVector2D P(static_cast<float>(X), static_cast<float>(Y));
			for (int32 I = 0; I < Count; ++I)
			{
				const FVector2D C(Hash01(I, 17, Seed) * (Source.Domain.Dimensions.X - 1), Hash01(31, I, Seed + 7) * (Source.Domain.Dimensions.Y - 1));
				FVector2D D = P - C;
				const float Radius = MinDim * FMath::Lerp(0.12f, 0.32f, Hash01(I, 71, Seed + 19));
				const float Dist = D.Size();
				if (Dist >= Radius || Dist <= UE_SMALL_NUMBER) continue;
				const float T = 1.0f - Dist / Radius;
				if (Type == TEXT("Spin"))
				{
					const float A = -Power * T * T * 1.6f;
					const float CS = FMath::Cos(A), SN = FMath::Sin(A);
					D = FVector2D(D.X * CS - D.Y * SN, D.X * SN + D.Y * CS);
				}
				else D *= 1.0f + Power * T * 0.35f;
				P = C + D;
			}
			Out.AtInterior(X, Y) = SampleGridBilinear(Source, P.X, P.Y, true);
		}
		return true;
	}

	void RegisterSpatialSimple(FName Type, const TCHAR* DisplayName, const TCHAR* Description, std::initializer_list<FEonformTerrainParameterDescriptor> Parameters, const FSpatialProcessor& Processor, bool bGuide = false)
	{
		FEonformTerrainNodeDescriptor D;
		D.Type = Type; D.DisplayName = DisplayName; D.Category = TEXT("Modify"); D.Description = Description;
		D.Inputs.Add(SpatialPort(TEXT("Input"), TEXT("Input")));
		if (bGuide) D.Inputs.Add(SpatialPort(TEXT("Guide"), TEXT("Guide")));
		D.Outputs.Add(SpatialPort(TEXT("Out"), TEXT("Out")));
		for (const FEonformTerrainParameterDescriptor& P : Parameters) D.Parameters.Add(P);
		FEonformTerrainNodeDescriptorRegistry::Register(D);
		const FString Label(DisplayName);
		FEonformTerrainNodeRegistry::Register(Type, [Label, Processor](const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
		{
			return EvaluateSpatial(*Label, Node, Inputs, Out, Error, Processor);
		});
	}
}

void RegisterEonformFoldNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::Fold,TEXT("Fold"),TEXT("Introduces slanted folds and breaks into terrain structure."),{SpatialNumber(TEXT("Amount"),TEXT("Amount"),0.5,0.0,1.0),SpatialNumber(TEXT("Angle"),TEXT("Angle"),35.0,-180.0,180.0),SpatialNumber(TEXT("Frequency"),TEXT("Frequency"),4.0,1.0,32.0)},FoldField); }
void RegisterEonformMeshifyNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::Meshify,TEXT("Meshify"),TEXT("Facets terrain into coarse planar cells while retaining the source silhouette."),{SpatialInteger(TEXT("CellSize"),TEXT("Cell Size"),4,1,64),SpatialNumber(TEXT("Facet"),TEXT("Facet"),0.75,0.0,1.0)},MeshifyField); }
void RegisterEonformOrigamiNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::Origami,TEXT("Origami"),TEXT("Creates diagonal multifold structure for stylized or erosion-ready terrain."),{SpatialNumber(TEXT("Amount"),TEXT("Amount"),0.5,0.0,1.0),SpatialInteger(TEXT("Seed"),TEXT("Seed"),1337,0,2147483647)},OrigamiField); }
void RegisterEonformSlopeBlurNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::SlopeBlur,TEXT("SlopeBlur"),TEXT("Blurs terrain along the local slope direction of an optional guide."),{SpatialNumber(TEXT("Intensity"),TEXT("Intensity"),0.5,0.0,1.0),SpatialInteger(TEXT("Samples"),TEXT("Samples"),4,1,16)},SlopeBlurField,true); }
void RegisterEonformSlopeWarpNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::SlopeWarp,TEXT("SlopeWarp"),TEXT("Warps terrain along the local slope vector of an optional guide."),{SpatialNumber(TEXT("Intensity"),TEXT("Intensity"),0.2,0.0,1.0),SpatialInteger(TEXT("Iterations"),TEXT("Iterations"),1,1,8),SpatialNumber(TEXT("Direction"),TEXT("Direction"),0.0,-360.0,360.0),SpatialBool(TEXT("Normalized"),TEXT("Normalized"),true)},SlopeWarpField,true); }
void RegisterEonformSwirlNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::Swirl,TEXT("Swirl"),TEXT("Creates a single vortex warp centered at a configurable location."),{SpatialNumber(TEXT("Size"),TEXT("Size"),0.5,0.05,1.0),SpatialNumber(TEXT("X"),TEXT("X"),0.5,0.0,1.0),SpatialNumber(TEXT("Y"),TEXT("Y"),0.5,0.0,1.0),SpatialNumber(TEXT("Power"),TEXT("Power"),0.65,-2.0,2.0)},SwirlField); }
void RegisterEonformThermalShaperNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::ThermalShaper,TEXT("ThermalShaper"),TEXT("Strengthens or relaxes steep thermal-like terrain structure."),{SpatialNumber(TEXT("Amount"),TEXT("Amount"),0.5,-1.0,1.0),SpatialNumber(TEXT("Talus"),TEXT("Talus"),0.08,0.001,1.0),SpatialInteger(TEXT("Iterations"),TEXT("Iterations"),2,1,16)},ThermalShaperField); }
void RegisterEonformTransposeNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::Transpose,TEXT("Transpose"),TEXT("Transfers the character of a reference field onto an input while retaining its broad form."),{SpatialName(TEXT("Mode"),TEXT("Mode"),TEXT("Transpose"),{TEXT("Transpose"),TEXT("Embed"),TEXT("Insert")}),SpatialNumber(TEXT("Amount"),TEXT("Amount"),1.0,0.0,1.0),SpatialBool(TEXT("Extend"),TEXT("Extend"),false),SpatialBool(TEXT("Flatten"),TEXT("Flatten"),false),SpatialNumber(TEXT("Threshold"),TEXT("Threshold"),0.0,-1.0,1.0),SpatialName(TEXT("Boundary"),TEXT("Boundary"),TEXT("Mirror"),{TEXT("Clamp"),TEXT("Mirror")})},TransposeField,true); }
void RegisterEonformVariableBlurNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::VariableBlur,TEXT("VariableBlur"),TEXT("Varies blur radius spatially using an optional guide field."),{SpatialNumber(TEXT("Amount"),TEXT("Amount"),1.0,0.0,1.0),SpatialInteger(TEXT("Radius"),TEXT("Radius"),6,1,24)},VariableBlurField,true); }
void RegisterEonformWhorlNode(){ using namespace EonformModifySpatialNodesPrivate; RegisterSpatialSimple(EonformTerrainNodeTypes::Whorl,TEXT("Whorl"),TEXT("Applies multiple vortex-like distortions across large terrain areas."),{SpatialName(TEXT("Type"),TEXT("Type"),TEXT("Stretch"),{TEXT("Stretch"),TEXT("Spin")}),SpatialInteger(TEXT("Whorls"),TEXT("Whorls"),4,1,32),SpatialNumber(TEXT("Power"),TEXT("Power"),0.35,-2.0,2.0),SpatialInteger(TEXT("Seed"),TEXT("Seed"),1337,0,2147483647)},WhorlField); }
