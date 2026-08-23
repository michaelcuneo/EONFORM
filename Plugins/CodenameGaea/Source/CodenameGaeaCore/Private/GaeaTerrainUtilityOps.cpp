#include "GaeaTerrainUtilityOps.h"

namespace
{
	bool ValidateSameDomain(const FGaeaScalarField& A, const FGaeaScalarField& B, const TCHAR* Operation, FString* OutError)
	{
		if (!A.IsValid() || !B.IsValid() || A.Domain != B.Domain)
		{
			if (OutError) *OutError = FString::Printf(TEXT("%s requires valid fields with matching domains."), Operation);
			return false;
		}
		return true;
	}

	float SampleWrapped(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		const int32 W = Field.Domain.Dimensions.X;
		const int32 H = Field.Domain.Dimensions.Y;
		X = (X % W + W) % W;
		Y = (Y % H + H) % H;
		return Field.AtInterior(X, Y);
	}

	FLinearColor SampleWrapped(const FGaeaColorField& Field, int32 X, int32 Y)
	{
		const int32 W = Field.Domain.Dimensions.X;
		const int32 H = Field.Domain.Dimensions.Y;
		X = (X % W + W) % W;
		Y = (Y % H + H) % H;
		return Field.AtInterior(X, Y);
	}

	float Smooth01(float V)
	{
		V = FMath::Clamp(V, 0.0f, 1.0f);
		return V * V * (3.0f - 2.0f * V);
	}

	float EvalToken(const FString& Token, float A, float B, float C, float X, float Y)
	{
		if (Token == TEXT("a")) return A;
		if (Token == TEXT("b")) return B;
		if (Token == TEXT("c")) return C;
		if (Token == TEXT("x")) return X;
		if (Token == TEXT("y")) return Y;
		if (Token == TEXT("pi")) return PI;
		return FCString::Atof(*Token);
	}

	float EvalSimpleExpression(FString Expression, float A, float B, float C, float X, float Y)
	{
		Expression.ReplaceInline(TEXT(" "), TEXT(""));
		if (Expression.IsEmpty()) return A;

		// Safe intentionally-small expression subset sufficient for graph math and
		// composite masks: variables/constants, unary functions, and one binary op.
		struct FUnary { const TCHAR* Name; TFunction<float(float)> Fn; };
		const FUnary Unary[] = {
			{ TEXT("abs"), [](float V){ return FMath::Abs(V); } },
			{ TEXT("sqrt"), [](float V){ return FMath::Sqrt(FMath::Max(V, 0.0f)); } },
			{ TEXT("sin"), [](float V){ return FMath::Sin(V); } },
			{ TEXT("cos"), [](float V){ return FMath::Cos(V); } },
			{ TEXT("tan"), [](float V){ return FMath::Tan(V); } },
			{ TEXT("floor"), [](float V){ return FMath::FloorToFloat(V); } },
			{ TEXT("ceil"), [](float V){ return FMath::CeilToFloat(V); } },
			{ TEXT("frac"), [](float V){ return FMath::Frac(V); } },
			{ TEXT("saturate"), [](float V){ return FMath::Clamp(V, 0.0f, 1.0f); } },
			{ TEXT("smooth"), [](float V){ return Smooth01(V); } }
		};
		for (const FUnary& U : Unary)
		{
			const FString Prefix = FString(U.Name) + TEXT("(");
			if (Expression.StartsWith(Prefix) && Expression.EndsWith(TEXT(")")))
			{
				const FString Inner = Expression.Mid(Prefix.Len(), Expression.Len() - Prefix.Len() - 1);
				return U.Fn(EvalSimpleExpression(Inner, A, B, C, X, Y));
			}
		}

		for (const TCHAR Op : { TEXT('+'), TEXT('-'), TEXT('*'), TEXT('/'), TEXT('^') })
		{
			int32 Index = INDEX_NONE;
			if (Expression.FindChar(Op, Index) && Index > 0)
			{
				const float L = EvalSimpleExpression(Expression.Left(Index), A, B, C, X, Y);
				const float R = EvalSimpleExpression(Expression.Mid(Index + 1), A, B, C, X, Y);
				switch (Op)
				{
				case TEXT('+'): return L + R;
				case TEXT('-'): return L - R;
				case TEXT('*'): return L * R;
				case TEXT('/'): return FMath::Abs(R) > UE_SMALL_NUMBER ? L / R : L;
				case TEXT('^'): return FMath::Pow(FMath::Max(L, 0.0f), R);
				default: break;
				}
			}
		}
		return EvalToken(Expression, A, B, C, X, Y);
	}

	FLinearColor BlendColor(const FLinearColor& A, const FLinearColor& B, float Alpha, FName Mode)
	{
		FLinearColor Target = B;
		if (Mode == TEXT("Add")) Target = A + B;
		else if (Mode == TEXT("Multiply")) Target = A * B;
		else if (Mode == TEXT("Screen"))
		{
			Target = FLinearColor(1.0f - (1.0f - A.R) * (1.0f - B.R), 1.0f - (1.0f - A.G) * (1.0f - B.G), 1.0f - (1.0f - A.B) * (1.0f - B.B), 1.0f);
		}
		else if (Mode == TEXT("Max"))
		{
			Target = FLinearColor(FMath::Max(A.R, B.R), FMath::Max(A.G, B.G), FMath::Max(A.B, B.B), 1.0f);
		}
		else if (Mode == TEXT("Min"))
		{
			Target = FLinearColor(FMath::Min(A.R, B.R), FMath::Min(A.G, B.G), FMath::Min(A.B, B.B), 1.0f);
		}
		return FMath::Lerp(A, Target, FMath::Clamp(Alpha, 0.0f, 1.0f));
	}
}

bool FGaeaTerrainUtilityOps::EvaluateMath(const FGaeaScalarField& A, const FGaeaScalarField* B, const FGaeaScalarField* C, const FGaeaTerrainUtilityMathSettings& Settings, FGaeaScalarField& OutField, FString* OutError)
{
	if (!A.IsValid()) { if (OutError) *OutError = TEXT("Math requires a valid A field."); return false; }
	if (B && !ValidateSameDomain(A, *B, TEXT("Math"), OutError)) return false;
	if (C && !ValidateSameDomain(A, *C, TEXT("Math"), OutError)) return false;
	OutField = A;
	const int32 W = A.Domain.Dimensions.X;
	const int32 H = A.Domain.Dimensions.Y;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const float NX = Settings.bNormalizedCoordinates && W > 1 ? static_cast<float>(X) / static_cast<float>(W - 1) : static_cast<float>(X);
			const float NY = Settings.bNormalizedCoordinates && H > 1 ? static_cast<float>(Y) / static_cast<float>(H - 1) : static_cast<float>(Y);
			const float V = EvalSimpleExpression(Settings.Expression, A.AtInterior(X,Y), B ? B->AtInterior(X,Y) : 0.0f, C ? C->AtInterior(X,Y) : 0.0f, NX, NY);
			OutField.AtInterior(X,Y) = FMath::IsFinite(V) ? V : 0.0f;
		}
	}
	if (OutError) OutError->Reset();
	return true;
}

bool FGaeaTerrainUtilityOps::Compare(const FGaeaScalarField& A, const FGaeaScalarField& B, float Ratio, bool bPerpendicular, bool bSwap, FGaeaScalarField& OutField, FGaeaColorField* OutColorized, FString* OutError)
{
	if (!ValidateSameDomain(A, B, TEXT("Compare"), OutError)) return false;
	OutField = A;
	Ratio = FMath::Clamp(Ratio, 0.001f, 1.0f);
	if (OutColorized) OutColorized->Initialize(A.Domain, FLinearColor::Black);
	for (int32 Y = 0; Y < A.Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < A.Domain.Dimensions.X; ++X)
		{
			float Av = A.AtInterior(X,Y);
			float Bv = B.AtInterior(X,Y);
			if (bSwap) Swap(Av, Bv);
			float D = bPerpendicular ? FMath::Abs(Av) + FMath::Abs(Bv) : FMath::Abs(Av - Bv);
			D = FMath::Clamp(D / Ratio, 0.0f, 1.0f);
			OutField.AtInterior(X,Y) = D;
			if (OutColorized)
			{
				const float Signed = FMath::Clamp((Av - Bv) / Ratio, -1.0f, 1.0f);
				OutColorized->AtInterior(X,Y) = Signed >= 0.0f
					? FMath::Lerp(FLinearColor::Black, FLinearColor(1,0.15f,0.05f,1), Signed)
					: FMath::Lerp(FLinearColor::Black, FLinearColor(0.05f,0.3f,1,1), -Signed);
			}
		}
	}
	if (OutError) OutError->Reset();
	return true;
}

bool FGaeaTerrainUtilityOps::ApplyMask(const FGaeaScalarField& After, const FGaeaScalarField& Before, const FGaeaScalarField& Mask, FGaeaScalarField& OutField, FString* OutError)
{
	if (!ValidateSameDomain(After, Before, TEXT("Mask"), OutError) || !ValidateSameDomain(After, Mask, TEXT("Mask"), OutError)) return false;
	OutField = After;
	for (int32 Y = 0; Y < After.Domain.Dimensions.Y; ++Y)
		for (int32 X = 0; X < After.Domain.Dimensions.X; ++X)
			OutField.AtInterior(X,Y) = FMath::Lerp(Before.AtInterior(X,Y), After.AtInterior(X,Y), FMath::Clamp(Mask.AtInterior(X,Y), 0.0f, 1.0f));
	if (OutError) OutError->Reset();
	return true;
}

bool FGaeaTerrainUtilityOps::MakeSeamless(const FGaeaScalarField& Input, float Edge, float ShiftX, float ShiftY, FGaeaScalarField& OutField, FString* OutError)
{
	if (!Input.IsValid()) { if (OutError) *OutError = TEXT("Seamless requires a valid input field."); return false; }
	OutField = Input;
	const int32 W = Input.Domain.Dimensions.X, H = Input.Domain.Dimensions.Y;
	const int32 SX = FMath::RoundToInt(ShiftX * static_cast<float>(W));
	const int32 SY = FMath::RoundToInt(ShiftY * static_cast<float>(H));
	Edge = FMath::Clamp(Edge, 0.001f, 0.5f);
	for (int32 Y=0; Y<H; ++Y) for (int32 X=0; X<W; ++X)
	{
		const float NX = W > 1 ? static_cast<float>(X) / static_cast<float>(W-1) : 0.0f;
		const float NY = H > 1 ? static_cast<float>(Y) / static_cast<float>(H-1) : 0.0f;
		const float WX = FMath::Min(NX, 1.0f-NX) < Edge ? Smooth01(FMath::Min(NX,1.0f-NX)/Edge) : 1.0f;
		const float WY = FMath::Min(NY, 1.0f-NY) < Edge ? Smooth01(FMath::Min(NY,1.0f-NY)/Edge) : 1.0f;
		const float Wgt = FMath::Min(WX, WY);
		const float Shifted = SampleWrapped(Input, X + SX + W/2, Y + SY + H/2);
		OutField.AtInterior(X,Y) = FMath::Lerp(Shifted, Input.AtInterior(X,Y), Wgt);
	}
	if (OutError) OutError->Reset();
	return true;
}

bool FGaeaTerrainUtilityOps::MakeSeamless(const FGaeaColorField& Input, float Edge, float ShiftX, float ShiftY, FGaeaColorField& OutField, FString* OutError)
{
	if (!Input.IsValid()) { if (OutError) *OutError = TEXT("Seamless requires a valid color field."); return false; }
	OutField = Input;
	const int32 W = Input.Domain.Dimensions.X, H = Input.Domain.Dimensions.Y;
	const int32 SX = FMath::RoundToInt(ShiftX * static_cast<float>(W));
	const int32 SY = FMath::RoundToInt(ShiftY * static_cast<float>(H));
	Edge = FMath::Clamp(Edge, 0.001f, 0.5f);
	for (int32 Y=0; Y<H; ++Y) for (int32 X=0; X<W; ++X)
	{
		const float NX = W > 1 ? static_cast<float>(X) / static_cast<float>(W-1) : 0.0f;
		const float NY = H > 1 ? static_cast<float>(Y) / static_cast<float>(H-1) : 0.0f;
		const float WX = FMath::Min(NX, 1.0f-NX) < Edge ? Smooth01(FMath::Min(NX,1.0f-NX)/Edge) : 1.0f;
		const float WY = FMath::Min(NY, 1.0f-NY) < Edge ? Smooth01(FMath::Min(NY,1.0f-NY)/Edge) : 1.0f;
		const float Wgt = FMath::Min(WX, WY);
		OutField.AtInterior(X,Y) = FMath::Lerp(SampleWrapped(Input, X + SX + W/2, Y + SY + H/2), Input.AtInterior(X,Y), Wgt);
	}
	if (OutError) OutError->Reset();
	return true;
}

bool FGaeaTerrainUtilityOps::Repeat(const FGaeaScalarField& Input, int32 Tiles, bool bCompensateHeight, float Height, FGaeaScalarField& OutField, FString* OutError)
{
	if (!Input.IsValid()) { if (OutError) *OutError = TEXT("Repeat requires a valid input field."); return false; }
	Tiles = FMath::Clamp(Tiles, 1, 64);
	OutField = Input;
	const int32 W = Input.Domain.Dimensions.X, H = Input.Domain.Dimensions.Y;
	const float Scale = bCompensateHeight ? Height : 1.0f;
	for (int32 Y=0; Y<H; ++Y) for (int32 X=0; X<W; ++X)
	{
		const int32 SX = FMath::FloorToInt((static_cast<double>(X) / FMath::Max(W-1,1)) * Tiles * FMath::Max(W-1,1)) % W;
		const int32 SY = FMath::FloorToInt((static_cast<double>(Y) / FMath::Max(H-1,1)) * Tiles * FMath::Max(H-1,1)) % H;
		OutField.AtInterior(X,Y) = SampleWrapped(Input, SX, SY) * Scale;
	}
	if (OutError) OutError->Reset();
	return true;
}

bool FGaeaTerrainUtilityOps::MixColors(const TArray<FGaeaTerrainUtilityColorLayer>& Layers, FGaeaColorField& OutField, FString* OutError)
{
	if (Layers.IsEmpty() || !Layers[0].Color || !Layers[0].Color->IsValid()) { if (OutError) *OutError = TEXT("Mixer requires at least one valid color layer."); return false; }
	OutField = *Layers[0].Color;
	for (int32 I=1; I<Layers.Num(); ++I)
	{
		const FGaeaTerrainUtilityColorLayer& L = Layers[I];
		if (!L.Color || !L.Color->IsValid() || L.Color->Domain != OutField.Domain) { if (OutError) *OutError = TEXT("Mixer color layers must use the same domain."); return false; }
		if (L.Mask && (!L.Mask->IsValid() || L.Mask->Domain != OutField.Domain)) { if (OutError) *OutError = TEXT("Mixer masks must use the same domain."); return false; }
		for (int32 Y=0; Y<OutField.Domain.Dimensions.Y; ++Y) for (int32 X=0; X<OutField.Domain.Dimensions.X; ++X)
		{
			const float Alpha = FMath::Clamp(L.Opacity * (L.Mask ? L.Mask->AtInterior(X,Y) : 1.0f), 0.0f, 1.0f);
			OutField.AtInterior(X,Y) = BlendColor(OutField.AtInterior(X,Y), L.Color->AtInterior(X,Y), Alpha, L.BlendMode);
		}
	}
	if (OutError) OutError->Reset();
	return true;
}

int32 FGaeaTerrainUtilityOps::MutateSeed(int32 BaseSeed, int32 OverrideSeed)
{
	uint32 H = GetTypeHash(BaseSeed);
	H = HashCombineFast(H, GetTypeHash(OverrideSeed));
	H ^= H >> 16;
	H *= 0x7feb352du;
	H ^= H >> 15;
	return static_cast<int32>(H & 0x7fffffffu);
}
