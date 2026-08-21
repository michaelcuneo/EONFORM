#include "GaeaPrimitiveAssetNodes.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor PrimitiveAssetPort(FName Name, FName DataType, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = DataType;
		Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor PrimitiveAssetNumber(FName Name, const TCHAR* Label, double DefaultValue, double Minimum, double Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = Label;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor PrimitiveAssetBool(FName Name, const TCHAR* Label, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = Label;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor PrimitiveAssetName(FName Name, const TCHAR* Label, FName DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = Label;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		return Parameter;
	}

	bool EvaluateDrawNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const float Soften = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Soften"), 0.25)), 0.0f, 1.0f);
		const float Height = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), 0.0f, 4.0f);
		const FName StrokeData = Node.GetName(TEXT("StrokeData"), NAME_None);
		const double Half = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-Half, -Half), FVector2d(Half, Half));
		if (!Domain.IsValid()) { Error = TEXT("Draw produced an invalid grid domain."); return false; }
		FGaeaFieldDescriptor Descriptor; Descriptor.Name = GaeaTerrainFieldNames::Height; Descriptor.Unit = EGaeaFieldUnit::Normalized; Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field; Field.Initialize(Domain, Descriptor);
		const uint32 StrokeHash = GetTypeHash(StrokeData);
		const float Phase = static_cast<float>(StrokeHash & 0xffffU) / 65535.0f * 2.0f * PI;
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d World = Domain.InteriorSampleToWorld(X, Y);
				const float NX = static_cast<float>(World.X / Half); const float NY = static_cast<float>(World.Y / Half);
				const float Spine = 0.24f * FMath::Sin(NX * 3.2f + Phase) + 0.10f * FMath::Sin(NX * 8.0f - Phase * 0.5f);
				const float Distance = FMath::Abs(NY - Spine); const float Width = FMath::Lerp(0.12f, 0.42f, Soften);
				const float Ridge = FMath::Pow(FMath::Clamp(1.0f - Distance / FMath::Max(Width, UE_SMALL_NUMBER), 0.0f, 1.0f), FMath::Lerp(3.5f, 1.2f, Soften));
				Field.AtInterior(X, Y) = FMath::Clamp(Ridge * Height, 0.0f, 1.0f);
			}
		}
		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Field))) { Error = TEXT("Draw could not publish its Height field."); return false; }
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid()) { Error = TEXT("Draw produced an invalid terrain value."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}

	bool EvaluateObjectNode(const FGaeaTerrainNode&, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation&, FString& Error)
	{
		Error = TEXT("Object node import backend is pending. The Gaea 2 public node contract is available, but mesh rasterization to a 2.5D heightfield has not been wired yet."); return false;
	}

	bool EvaluateTileInputNode(const FGaeaTerrainNode&, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation&, FString& Error)
	{
		Error = TEXT("TileInput backend is pending. The Gaea 2 public node contract is available, but tiled file ingestion/build-context mapping has not been wired yet."); return false;
	}
}

void RegisterGaeaDrawNode()
{
	FGaeaTerrainNodeDescriptor Descriptor; Descriptor.Type = GaeaTerrainNodeTypes::Draw; Descriptor.DisplayName = TEXT("Draw"); Descriptor.Category = TEXT("Primitive"); Descriptor.Description = TEXT("Draws authored mountain-range strokes using the Painter workflow.");
	Descriptor.Outputs.Add(PrimitiveAssetPort(TEXT("Out"), TEXT("Terrain"), TEXT("Out")));
	Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("Soften"), TEXT("Soften"), 0.25, 0.0, 1.0)); Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("Height"), TEXT("Height"), 1.0, 0.0, 4.0)); Descriptor.Parameters.Add(PrimitiveAssetName(TEXT("StrokeData"), TEXT("Stroke Data"), NAME_None));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Draw, EvaluateDrawNode);
}

void RegisterGaeaFileNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::File;
	Descriptor.DisplayName = TEXT("File");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Loads an external heightfield or RGB image with explicit physical extents or ground-sample distance.");
	Descriptor.Outputs.Add(PrimitiveAssetPort(TEXT("Out"), TEXT("Any"), TEXT("Out")));

	Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("IsRGB"), TEXT("Is RGB"), false));
	Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("EnforceLinearGamma"), TEXT("Enforce Linear Gamma"), false));
	Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("AllowUnclamped"), TEXT("Allow Unclamped"), false));
	Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("CropToSquare"), TEXT("Crop to Square"), false));

	// Spatial calibration. Pixel resolution comes from the image itself; these
	// values define how large that raster is in the physical world.
	Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("ExtentXMetres"), TEXT("Extent X (m)"), 1000.0, 0.001, 100000000.0));
	Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("ExtentYMetres"), TEXT("Extent Y (m)"), 1000.0, 0.001, 100000000.0));
	Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("XYScale"), TEXT("XY Scale"), 1.0, 0.0001, 10000.0));
	Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("UseGroundSampleDistance"), TEXT("Use Metres / Pixel"), false));
	Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("GroundSampleDistanceXMetres"), TEXT("Metres / Pixel X"), 1.0, 0.0001, 1000000.0));
	Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("GroundSampleDistanceYMetres"), TEXT("Metres / Pixel Y"), 1.0, 0.0001, 1000000.0));
	Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("HeightScaleMetres"), TEXT("Height Scale (m)"), 80.0, 0.001, 1000000.0));

	Descriptor.Parameters.Add(PrimitiveAssetName(TEXT("InformationHoudini"), TEXT("Information Houdini"), NAME_None));
	Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("NeverCache"), TEXT("Never Cache"), false));

	// Descriptor only. The UE 5.8-safe evaluator is registered separately by
	// RegisterGaeaFileNodeDecoder(), which decodes through IImageWrapperModule::DecompressImage.
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
}

void RegisterGaeaObjectNode()
{
	FGaeaTerrainNodeDescriptor Descriptor; Descriptor.Type = GaeaTerrainNodeTypes::Object; Descriptor.DisplayName = TEXT("Object"); Descriptor.Category = TEXT("Primitive"); Descriptor.Description = TEXT("Imports a 3D mesh and rasterizes it to a heightfield."); Descriptor.Outputs.Add(PrimitiveAssetPort(TEXT("Out"), TEXT("Terrain"), TEXT("Out")));
	Descriptor.Parameters.Add(PrimitiveAssetName(TEXT("Mesh"), TEXT("Mesh"), NAME_None)); Descriptor.Parameters.Add(PrimitiveAssetName(TEXT("RelativePath"), TEXT("Relative Path"), NAME_None)); Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("DropToFloor"), TEXT("Drop to Floor"), true)); Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("OffsetX"), TEXT("Offset X"), 0.0, -1000000.0, 1000000.0)); Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("OffsetY"), TEXT("Offset Y"), 0.0, -1000000.0, 1000000.0)); Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("OffsetZ"), TEXT("Offset Z"), 0.0, -1000000.0, 1000000.0)); Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("Scale"), TEXT("Scale"), 1.0, 0.0001, 1000.0)); Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("Pitch"), TEXT("Pitch"), 0.0, -360.0, 360.0)); Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("Yaw"), TEXT("Yaw"), 0.0, -360.0, 360.0)); Descriptor.Parameters.Add(PrimitiveAssetNumber(TEXT("Roll"), TEXT("Roll"), 0.0, -360.0, 360.0)); Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("Antialiasing"), TEXT("Antialiasing"), true));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Object, EvaluateObjectNode);
}

void RegisterGaeaTileInputNode()
{
	FGaeaTerrainNodeDescriptor Descriptor; Descriptor.Type = GaeaTerrainNodeTypes::TileInput; Descriptor.DisplayName = TEXT("TileInput"); Descriptor.Category = TEXT("Primitive"); Descriptor.Description = TEXT("Reads source tiles one-to-one for tiled builds or combines them into a single input."); Descriptor.Outputs.Add(PrimitiveAssetPort(TEXT("Out"), TEXT("Terrain"), TEXT("Out")));
	Descriptor.Parameters.Add(PrimitiveAssetName(TEXT("File"), TEXT("File"), NAME_None)); Descriptor.Parameters.Add(PrimitiveAssetName(TEXT("RelativePath"), TEXT("Relative Path"), NAME_None)); Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("EnforceLinearGamma"), TEXT("Enforce Linear Gamma"), false)); Descriptor.Parameters.Add(PrimitiveAssetBool(TEXT("FlipY"), TEXT("Flip Y"), false)); Descriptor.Parameters.Add(PrimitiveAssetName(TEXT("TileInfo"), TEXT("Tile Info"), NAME_None));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::TileInput, EvaluateTileInputNode);
}
