#include "GaeaBlurNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor BlurAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor BlurNumberParameter(
		FName Name,
		const TCHAR* DisplayName,
		double DefaultValue,
		double Minimum,
		double Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor BlurIntegerParameter(
		FName Name,
		const TCHAR* DisplayName,
		int64 DefaultValue,
		int64 Minimum,
		int64 Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor BlurNameParameter(
		FName Name,
		const TCHAR* DisplayName,
		FName DefaultValue,
		std::initializer_list<FName> Options)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}

	float BlurSampleClamped(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		const int32 ClampedX = FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1);
		const int32 ClampedY = FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1);
		return Field.AtInterior(ClampedX, ClampedY);
	}

	void BlurBoxPass(const FGaeaScalarField& Source, FGaeaScalarField& Dest, int32 Radius)
	{
		const int32 R = FMath::Max(Radius, 1);
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Sum = 0.0f;
				int32 Count = 0;
				for (int32 DY = -R; DY <= R; ++DY)
				{
					for (int32 DX = -R; DX <= R; ++DX)
					{
						Sum += BlurSampleClamped(Source, X + DX, Y + DY);
						++Count;
					}
				}
				Dest.AtInterior(X, Y) = Count > 0 ? Sum / static_cast<float>(Count) : Source.AtInterior(X, Y);
			}
		}
	}

	void BlurGaussianPass(const FGaeaScalarField& Source, FGaeaScalarField& Dest, int32 Radius, float Sigma)
	{
		const int32 R = FMath::Max(Radius, 1);
		const float SafeSigma = FMath::Max(Sigma, 0.01f);
		const float InvTwoSigmaSq = 1.0f / (2.0f * SafeSigma * SafeSigma);
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Sum = 0.0f;
				float WeightSum = 0.0f;
				for (int32 DY = -R; DY <= R; ++DY)
				{
					for (int32 DX = -R; DX <= R; ++DX)
					{
						const float DistanceSq = static_cast<float>(DX * DX + DY * DY);
						const float Weight = FMath::Exp(-DistanceSq * InvTwoSigmaSq);
						Sum += BlurSampleClamped(Source, X + DX, Y + DY) * Weight;
						WeightSum += Weight;
					}
				}
				Dest.AtInterior(X, Y) = WeightSum > UE_SMALL_NUMBER ? Sum / WeightSum : Source.AtInterior(X, Y);
			}
		}
	}

	void BlurMotionPass(const FGaeaScalarField& Source, FGaeaScalarField& Dest, int32 Radius, float DirectionDegrees)
	{
		const int32 R = FMath::Max(Radius, 1);
		const float Radians = FMath::DegreesToRadians(DirectionDegrees);
		const float DirX = FMath::Cos(Radians);
		const float DirY = FMath::Sin(Radians);
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Sum = 0.0f;
				int32 Count = 0;
				for (int32 Step = -R; Step <= R; ++Step)
				{
					const int32 SX = X + FMath::RoundToInt(DirX * static_cast<float>(Step));
					const int32 SY = Y + FMath::RoundToInt(DirY * static_cast<float>(Step));
					Sum += BlurSampleClamped(Source, SX, SY);
					++Count;
				}
				Dest.AtInterior(X, Y) = Count > 0 ? Sum / static_cast<float>(Count) : Source.AtInterior(X, Y);
			}
		}
	}

	void BlurRadialPass(const FGaeaScalarField& Source, FGaeaScalarField& Dest, int32 Radius)
	{
		const float CenterX = static_cast<float>(Source.Domain.Dimensions.X - 1) * 0.5f;
		const float CenterY = static_cast<float>(Source.Domain.Dimensions.Y - 1) * 0.5f;
		const int32 Steps = FMath::Max(Radius * 2 + 1, 3);
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const FVector2D ToCenter(CenterX - static_cast<float>(X), CenterY - static_cast<float>(Y));
				const FVector2D Direction = ToCenter.GetSafeNormal();
				float Sum = 0.0f;
				for (int32 Step = -Radius; Step <= Radius; ++Step)
				{
					const int32 SX = X + FMath::RoundToInt(Direction.X * static_cast<float>(Step));
					const int32 SY = Y + FMath::RoundToInt(Direction.Y * static_cast<float>(Step));
					Sum += BlurSampleClamped(Source, SX, SY);
				}
				Dest.AtInterior(X, Y) = Sum / static_cast<float>(Steps);
			}
		}
	}

	void BlurSmoothPass(const FGaeaScalarField& Source, FGaeaScalarField& Dest, int32 Radius)
	{
		BlurBoxPass(Source, Dest, Radius);
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Center = Source.AtInterior(X, Y);
				const float Smoothed = Dest.AtInterior(X, Y);
				Dest.AtInterior(X, Y) = FMath::Lerp(Center, Smoothed, 0.5f);
			}
		}
	}

	bool BlurProcessField(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		FGaeaScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Blur received an invalid scalar field.");
			return false;
		}

		const float Power = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Power"), 0.5)), 0.0f, 1.0f);
		const int32 Iterations = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 32);
		const float Sigma = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Sigma"), 1.0)), 0.01f, 16.0f);
		const float Direction = FMath::Fmod(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)), 360.0f);
		const FName Type = Node.GetName(TEXT("Type"), TEXT("Fast"));
		const int32 Radius = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(1.0f, 8.0f, Power)), 1, 8);

		FGaeaScalarField Current = Source;
		FGaeaScalarField Working = Source;
		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			if (Type == TEXT("Gaussian"))
			{
				BlurGaussianPass(Current, Working, Radius, Sigma);
			}
			else if (Type == TEXT("Radial"))
			{
				BlurRadialPass(Current, Working, Radius);
			}
			else if (Type == TEXT("Motion"))
			{
				BlurMotionPass(Current, Working, Radius, Direction);
			}
			else if (Type == TEXT("Smooth"))
			{
				BlurSmoothPass(Current, Working, Radius);
			}
			else
			{
				BlurBoxPass(Current, Working, Radius);
			}
			Current = Working;
		}

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				OutField.AtInterior(X, Y) = FMath::Lerp(Source.AtInterior(X, Y), Current.AtInterior(X, Y), Power);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateBlurNode(
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
			Error = TEXT("Blur requires a valid Input.");
			return false;
		}

		if (Input->Type == EGaeaTerrainValueType::ScalarField)
		{
			FGaeaScalarField Result;
			if (!BlurProcessField(Node, Input->ScalarField, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EGaeaTerrainValueType::Terrain)
		{
			const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Blur terrain input has no valid Height field.");
				return false;
			}

			FGaeaScalarField ResultHeight;
			if (!BlurProcessField(Node, *Height, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;
			FGaeaTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Blur could not publish its Height field.");
				return false;
			}

			FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!Result.IsValid())
			{
				Error = TEXT("Blur produced an invalid terrain value.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
			return true;
		}

		Error = TEXT("Blur received an unsupported input type.");
		return false;
	}
}

void RegisterGaeaBlurNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Blur;
	Descriptor.DisplayName = TEXT("Blur");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Blurs terrain or mask data using Gaea-style fast, Gaussian, radial, motion, or smooth blur modes.");
	Descriptor.Inputs.Add(BlurAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(BlurAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(BlurNumberParameter(TEXT("Power"), TEXT("Power"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(BlurIntegerParameter(TEXT("Iterations"), TEXT("Iterations"), 1, 1, 32));
	Descriptor.Parameters.Add(BlurNumberParameter(TEXT("Sigma"), TEXT("Sigma"), 1.0, 0.01, 16.0));
	Descriptor.Parameters.Add(BlurNumberParameter(TEXT("Direction"), TEXT("Direction"), 0.0, 0.0, 360.0));
	Descriptor.Parameters.Add(BlurNameParameter(TEXT("Type"), TEXT("Type"), TEXT("Fast"),
		{ TEXT("Fast"), TEXT("Gaussian"), TEXT("Radial"), TEXT("Motion"), TEXT("Smooth") }));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Blur, EvaluateBlurNode);
}
