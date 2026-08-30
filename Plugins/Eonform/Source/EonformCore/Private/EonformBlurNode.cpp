#include "EonformBlurNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformBlurNode
{
	int32 ResolveRadiusSamples(
		const FEonformTerrainNode& Node,
		const FIntPoint& ReferenceDimensions)
	{
		const float RadiusControl = FMath::Clamp(
			static_cast<float>(Node.GetNumber(TEXT("Radius"), 0.1)),
			0.0f,
			1.0f);
		if (RadiusControl <= UE_SMALL_NUMBER) return 0;
		if (ReferenceDimensions.X < 2 || ReferenceDimensions.Y < 2) return INDEX_NONE;

		const int32 MinDimension = FMath::Min(ReferenceDimensions.X, ReferenceDimensions.Y);
		return FMath::Clamp(
			FMath::RoundToInt(RadiusControl * static_cast<float>(MinDimension) * 0.02f),
			1,
			16);
	}
}

namespace
{
	FEonformTerrainPortDescriptor BlurAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor BlurNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	float BlurSampleClamped(
		const FEonformScalarField& Field,
		int32 X,
		int32 Y,
		const FEonformGridDomain* ReferenceDomain)
	{
		const FIntPoint StorageDimensions = Field.Domain.GetStorageDimensions();
		int32 SampleX = FMath::Clamp(X, 0, StorageDimensions.X - 1);
		int32 SampleY = FMath::Clamp(Y, 0, StorageDimensions.Y - 1);

		// A regional guard band may extend beyond the actual world on regions
		// touching a world edge. The legacy full-world Blur clamps there, so map
		// those samples back onto the reference-world edge rather than consuming
		// procedural values generated outside the world.
		if (ReferenceDomain)
		{
			FVector2d World = Field.Domain.StorageSampleToWorld(SampleX, SampleY);
			World.X = FMath::Clamp(World.X, ReferenceDomain->WorldMin.X, ReferenceDomain->WorldMax.X);
			World.Y = FMath::Clamp(World.Y, ReferenceDomain->WorldMin.Y, ReferenceDomain->WorldMax.Y);
			const FVector2d StorageCoordinate = Field.Domain.WorldToStorageCoordinate(World);
			SampleX = FMath::Clamp(FMath::RoundToInt(StorageCoordinate.X), 0, StorageDimensions.X - 1);
			SampleY = FMath::Clamp(FMath::RoundToInt(StorageCoordinate.Y), 0, StorageDimensions.Y - 1);
		}

		return Field.AtStorage(SampleX, SampleY);
	}

	bool BlurProcessField(
		const FEonformTerrainNode& Node,
		const FEonformScalarField& Source,
		const FEonformTerrainEvaluationContext& Context,
		FEonformScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Blur received an invalid scalar field.");
			return false;
		}

		const FIntPoint ReferenceDimensions = Context.HasRegion()
			? Context.ReferenceResolution
			: Source.Domain.Dimensions;
		const int32 Radius = EonformBlurNode::ResolveRadiusSamples(Node, ReferenceDimensions);
		if (Radius == INDEX_NONE)
		{
			Error = TEXT("Regional Blur requires a valid full-world reference resolution.");
			return false;
		}
		if (Radius == 0)
		{
			OutField = Source;
			return true;
		}
		if (Context.HasRegion() && Source.Domain.BorderSamples < Radius)
		{
			Error = FString::Printf(
				TEXT("Regional Blur requires at least %d dependency-border samples; received %d."),
				Radius,
				Source.Domain.BorderSamples);
			return false;
		}

		FEonformGridDomain ReferenceDomain;
		const FEonformGridDomain* ReferenceDomainPtr = nullptr;
		if (Context.HasRegion())
		{
			ReferenceDomain = Context.ResolveReferenceDomain(
				ReferenceDimensions,
				FVector2d(-50000.0, -50000.0),
				FVector2d(50000.0, 50000.0));
			if (!ReferenceDomain.IsValid())
			{
				Error = TEXT("Regional Blur could not resolve the full-world reference domain.");
				return false;
			}
			ReferenceDomainPtr = &ReferenceDomain;
		}

		OutField = Source;
		const FIntPoint StorageDimensions = Source.Domain.GetStorageDimensions();
		for (int32 Y = 0; Y < StorageDimensions.Y; ++Y)
		{
			for (int32 X = 0; X < StorageDimensions.X; ++X)
			{
				float Sum = 0.0f;
				int32 Count = 0;
				for (int32 DY = -Radius; DY <= Radius; ++DY)
				{
					for (int32 DX = -Radius; DX <= Radius; ++DX)
					{
						Sum += BlurSampleClamped(Source, X + DX, Y + DY, ReferenceDomainPtr);
						++Count;
					}
				}
				OutField.AtStorage(X, Y) = Count > 0
					? Sum / static_cast<float>(Count)
					: Source.AtStorage(X, Y);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateBlurNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Blur requires a valid Input.");
			return false;
		}

		if (Input->Type == EEonformTerrainValueType::ScalarField)
		{
			FEonformScalarField Result;
			if (!BlurProcessField(Node, Input->ScalarField, Context, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EEonformTerrainValueType::Terrain)
		{
			const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Blur terrain input has no valid Height field.");
				return false;
			}

			FEonformScalarField ResultHeight;
			if (!BlurProcessField(Node, *Height, Context, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Blur could not publish its Height field.");
				return false;
			}

			FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
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

void RegisterEonformBlurNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Blur;
	Descriptor.DisplayName = TEXT("Blur");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Diffuses sharp shapes and softens terrain or mask data.");
	Descriptor.Inputs.Add(BlurAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(BlurAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(BlurNumberParameter(TEXT("Radius"), TEXT("Radius"), 0.1, 0.0, 1.0));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Blur, EvaluateBlurNode);
}
