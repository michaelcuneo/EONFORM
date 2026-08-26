#include "EonformSurfaceAnalysisNodes.h"

#include "EonformColorField.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainPhysicalMetrics.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = DisplayName;
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FEonformTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = DisplayName;
		Port.DataType = TEXT("ScalarField");
		return Port;
	}

	FEonformTerrainPortDescriptor ColorPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = DisplayName;
		Port.DataType = TEXT("Color");
		return Port;
	}

	FEonformTerrainParameterDescriptor NumberParameter(FName Name, const TCHAR* DisplayName, double Default, double Min, double Max, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EEonformTerrainParameterType::Number;
		Parameter.DefaultNumber = Default;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Min;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Max;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor IntegerParameter(FName Name, const TCHAR* DisplayName, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EEonformTerrainParameterType::Integer;
		Parameter.DefaultInteger = Default;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Min);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Max);
		return Parameter;
	}

	FEonformTerrainParameterDescriptor NameParameter(FName Name, const TCHAR* DisplayName, FName Default, std::initializer_list<FName> Options, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = Default;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}

	FEonformScalarField MakeNormalizedField(const FEonformGridDomain& Domain, FName Name)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	float EncodeSigned(double Value)
	{
		return static_cast<float>(FMath::Clamp(Value * 0.5 + 0.5, 0.0, 1.0));
	}

	FVector3d DecodeExportNormal(const FVector3d& Canonical, FName Handedness, FName UpAxis)
	{
		FVector3d Result = Canonical;
		if (Handedness == TEXT("Right")) Result.Y = -Result.Y;

		if (UpAxis == TEXT("YUp"))
		{
			// Convert EONFORM/Unreal Z-up coordinates to a Y-up normal-map convention.
			Result = FVector3d(Result.X, Result.Z, Result.Y);
		}
		return Result.GetSafeNormal();
	}

	bool EvaluateNormals(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Normals requires a valid terrain input 'Terrain'.");
			return false;
		}

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		const FEonformScalarField* Height = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Normals requires a valid Height field.");
			return false;
		}

		const FIntPoint Dimensions = Height->Domain.Dimensions;
		const FVector2d CellSize = Height->Domain.GetCellSize();
		const FVector2d SpacingMeters = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Dimensions, CellSize);
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		if (SpacingMeters.X <= UE_DOUBLE_SMALL_NUMBER || SpacingMeters.Y <= UE_DOUBLE_SMALL_NUMBER || ElevationScaleMeters <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Normals could not resolve physical terrain metrics.");
			return false;
		}

		const FName Type = Node.GetName(TEXT("Type"), TEXT("Standard"));
		const int32 Width = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Width"), Dimensions.X)), 1, 16384);
		const int32 OutputHeight = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Height"), Dimensions.Y)), 1, 16384);
		const float DetailSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("DetailSize"), 1.0)), 0.25f, 32.0f);
		const FName Handedness = Node.GetName(TEXT("Handedness"), TEXT("Left"));
		const FName UpAxis = Node.GetName(TEXT("UpAxis"), TEXT("ZUp"));

		const double ResolutionScaleX = static_cast<double>(Dimensions.X) / static_cast<double>(FMath::Max(Width, 1));
		const double ResolutionScaleY = static_cast<double>(Dimensions.Y) / static_cast<double>(FMath::Max(OutputHeight, 1));
		double RadiusScale = FMath::Max(FMath::Max(ResolutionScaleX, ResolutionScaleY), static_cast<double>(DetailSize));
		if (Type == TEXT("Detail")) RadiusScale = FMath::Max(0.5, RadiusScale * 0.5);
		const int32 Radius = FMath::Clamp(FMath::RoundToInt(RadiusScale), 1, 32);

		FEonformScalarField NormalX = MakeNormalizedField(Height->Domain, EonformTerrainFieldNames::NormalX);
		FEonformScalarField NormalY = MakeNormalizedField(Height->Domain, EonformTerrainFieldNames::NormalY);
		FEonformScalarField NormalZ = MakeNormalizedField(Height->Domain, EonformTerrainFieldNames::NormalZ);
		FEonformColorField NormalColor;
		NormalColor.Initialize(Height->Domain, FLinearColor(0.5f, 0.5f, 1.0f, 1.0f));

		for (int32 Y = 0; Y < Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Dimensions.X; ++X)
			{
				const int32 XL = FMath::Max(0, X - Radius);
				const int32 XR = FMath::Min(Dimensions.X - 1, X + Radius);
				const int32 YD = FMath::Max(0, Y - Radius);
				const int32 YU = FMath::Min(Dimensions.Y - 1, Y + Radius);
				const double SpanX = FMath::Max(static_cast<double>(XR - XL) * SpacingMeters.X, UE_DOUBLE_SMALL_NUMBER);
				const double SpanY = FMath::Max(static_cast<double>(YU - YD) * SpacingMeters.Y, UE_DOUBLE_SMALL_NUMBER);
				const double DzDx = static_cast<double>(Height->AtInterior(XR, Y) - Height->AtInterior(XL, Y)) * ElevationScaleMeters / SpanX;
				const double DzDy = static_cast<double>(Height->AtInterior(X, YU) - Height->AtInterior(X, YD)) * ElevationScaleMeters / SpanY;
				const FVector3d Canonical = FVector3d(-DzDx, -DzDy, 1.0).GetSafeNormal();
				NormalX.AtInterior(X, Y) = EncodeSigned(Canonical.X);
				NormalY.AtInterior(X, Y) = EncodeSigned(Canonical.Y);
				NormalZ.AtInterior(X, Y) = EncodeSigned(Canonical.Z);

				const FVector3d Export = DecodeExportNormal(Canonical, Handedness, UpAxis);
				NormalColor.AtInterior(X, Y) = FLinearColor(
					EncodeSigned(Export.X),
					EncodeSigned(Export.Y),
					EncodeSigned(Export.Z),
					1.0f);
			}
		}

		FEonformScalarField OutputX = NormalX;
		FEonformScalarField OutputY = NormalY;
		FEonformScalarField OutputZ = NormalZ;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(NormalX))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(NormalY))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(NormalZ)))
		{
			Error = TEXT("Normals could not publish its derived normal channels.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Normal"), FEonformTerrainValue::MakeColor(MoveTemp(NormalColor)));
		Out.Outputs.Add(TEXT("X"), FEonformTerrainValue::MakeScalarField(MoveTemp(OutputX)));
		Out.Outputs.Add(TEXT("Y"), FEonformTerrainValue::MakeScalarField(MoveTemp(OutputY)));
		Out.Outputs.Add(TEXT("Z"), FEonformTerrainValue::MakeScalarField(MoveTemp(OutputZ)));
		Error.Reset();
		return true;
	}

	bool EvaluateOcclusion(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Occlusion requires a valid terrain input 'Terrain'.");
			return false;
		}

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		const FEonformScalarField* Height = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Occlusion requires a valid Height field.");
			return false;
		}

		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 1.0)), 0.0f, 8.0f);
		const int32 Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 8);
		const FIntPoint Dimensions = Height->Domain.Dimensions;
		const FVector2d CellSize = Height->Domain.GetCellSize();
		const FVector2d SpacingMeters = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Dimensions, CellSize);
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		const double RepresentativeSpacing = FMath::Max(FMath::Min(SpacingMeters.X, SpacingMeters.Y), UE_DOUBLE_SMALL_NUMBER);

		static const FIntPoint Directions[] =
		{
			FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
			FIntPoint(1, 1), FIntPoint(-1, 1), FIntPoint(1, -1), FIntPoint(-1, -1)
		};

		FEonformScalarField Occlusion = MakeNormalizedField(Height->Domain, EonformTerrainFieldNames::Occlusion);
		for (int32 Y = 0; Y < Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Dimensions.X; ++X)
			{
				const double CenterMeters = static_cast<double>(Height->AtInterior(X, Y)) * ElevationScaleMeters;
				double WeightedOcclusion = 0.0;
				double WeightSum = 0.0;

				for (int32 Octave = 0; Octave < Octaves; ++Octave)
				{
					const int32 Radius = 1 << Octave;
					const double OctaveWeight = 1.0 / FMath::Pow(2.0, static_cast<double>(Octave) * 0.35);
					double DirectionSum = 0.0;
					int32 DirectionCount = 0;

					for (const FIntPoint& Direction : Directions)
					{
						const int32 NX = X + Direction.X * Radius;
						const int32 NY = Y + Direction.Y * Radius;
						if (NX < 0 || NX >= Dimensions.X || NY < 0 || NY >= Dimensions.Y) continue;

						const double NeighborMeters = static_cast<double>(Height->AtInterior(NX, NY)) * ElevationScaleMeters;
						const double DirectionLength = (Direction.X != 0 && Direction.Y != 0) ? UE_SQRT_2 : 1.0;
						const double DistanceMeters = FMath::Max(RepresentativeSpacing * static_cast<double>(Radius) * DirectionLength, UE_DOUBLE_SMALL_NUMBER);
						const double PositiveHorizonSlope = FMath::Max((NeighborMeters - CenterMeters) / DistanceMeters, 0.0);
						const double HorizonAngle = FMath::Atan(PositiveHorizonSlope);
						DirectionSum += FMath::Clamp(HorizonAngle / (UE_PI * 0.5), 0.0, 1.0);
						++DirectionCount;
					}

					if (DirectionCount > 0)
					{
						WeightedOcclusion += (DirectionSum / static_cast<double>(DirectionCount)) * OctaveWeight;
						WeightSum += OctaveWeight;
					}
				}

				const double Enclosure = WeightSum > UE_DOUBLE_SMALL_NUMBER ? WeightedOcclusion / WeightSum : 0.0;
				// Strength is deliberately nonlinear: low values isolate crevices; higher values bring in larger gullies/valleys.
				const double Gain = FMath::Max(static_cast<double>(Strength), 0.0);
				const double Shaped = 1.0 - FMath::Exp(-Enclosure * Gain * 4.0);
				Occlusion.AtInterior(X, Y) = static_cast<float>(FMath::Clamp(Shaped, 0.0, 1.0));
			}
		}

		FEonformScalarField Output = Occlusion;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Occlusion)))
		{
			Error = TEXT("Occlusion could not publish its derived field.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Mask"), FEonformTerrainValue::MakeScalarField(MoveTemp(Output)));
		Error.Reset();
		return true;
	}
}

void RegisterEonformSurfaceAnalysisNodes()
{
	{
		FEonformTerrainNodeDescriptor Descriptor;
		Descriptor.Type = EonformTerrainNodeTypes::Normals;
		Descriptor.DisplayName = TEXT("Normals");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Generates physically scaled terrain normals, a material-ready normal map, and persistent encoded normal channels.");
		Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		Descriptor.Outputs.Add(ColorPort(TEXT("Normal"), TEXT("Normal")));
		Descriptor.Outputs.Add(ScalarPort(TEXT("X"), TEXT("X")));
		Descriptor.Outputs.Add(ScalarPort(TEXT("Y"), TEXT("Y")));
		Descriptor.Outputs.Add(ScalarPort(TEXT("Z"), TEXT("Z")));
		Descriptor.Parameters.Add(NameParameter(TEXT("Type"), TEXT("Type"), TEXT("Standard"), { TEXT("Standard"), TEXT("Detail") }, TEXT("Normals")));
		Descriptor.Parameters.Add(IntegerParameter(TEXT("Width"), TEXT("Width"), 2048, 1, 16384, TEXT("Normals")));
		Descriptor.Parameters.Add(IntegerParameter(TEXT("Height"), TEXT("Height"), 2048, 1, 16384, TEXT("Normals")));
		Descriptor.Parameters.Add(NumberParameter(TEXT("DetailSize"), TEXT("Detail Size"), 1.0, 0.25, 32.0, TEXT("Normals")));
		Descriptor.Parameters.Add(NameParameter(TEXT("Handedness"), TEXT("Handedness"), TEXT("Left"), { TEXT("Left"), TEXT("Right") }, TEXT("Normals")));
		Descriptor.Parameters.Add(NameParameter(TEXT("UpAxis"), TEXT("Up Axis"), TEXT("ZUp"), { TEXT("YUp"), TEXT("ZUp") }, TEXT("Normals")));
		FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
		FEonformTerrainNodeRegistry::Register(Descriptor.Type, EvaluateNormals);
	}

	{
		FEonformTerrainNodeDescriptor Descriptor;
		Descriptor.Type = EonformTerrainNodeTypes::Occlusion;
		Descriptor.DisplayName = TEXT("Occlusion");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Derives a multi-scale terrain enclosure mask biased toward crevices, sediment pockets, gullies, and valleys rather than screen-space lighting.");
		Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		Descriptor.Outputs.Add(ScalarPort(TEXT("Mask"), TEXT("Occlusion")));
		Descriptor.Parameters.Add(NumberParameter(TEXT("Strength"), TEXT("Strength"), 1.0, 0.0, 8.0, TEXT("Occlusion")));
		Descriptor.Parameters.Add(IntegerParameter(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 8, TEXT("Occlusion")));
		FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
		FEonformTerrainNodeRegistry::Register(Descriptor.Type, EvaluateOcclusion);
	}
}
