#include "GaeaFileNodeDecoder.h"

#include "GaeaColorField.h"
#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainRecipe.h"
#include "ImageCore.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	FString ResolveFileNodePath(const FGaeaTerrainNode& Node)
	{
		FString Path = Node.GetName(TEXT("File"), NAME_None).ToString();
		if (Path.IsEmpty() || Path == TEXT("None")) Path = Node.GetName(TEXT("RelativePath"), NAME_None).ToString();
		if (Path.IsEmpty() || Path == TEXT("None")) return FString();
		if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
		FPaths::NormalizeFilename(Path);
		return Path;
	}

	bool MakeFileNodeDomain(int32 Width, int32 Height, float WorldSize, FGaeaGridDomain& OutDomain, FString& Error)
	{
		if (Width < 2 || Height < 2)
		{
			Error = TEXT("File image must be at least 2x2 pixels.");
			return false;
		}

		const double Half = static_cast<double>(WorldSize) * 0.5;
		OutDomain = FGaeaGridDomain::Make(
			FIntPoint(Width, Height),
			FVector2d(-Half, -Half),
			FVector2d(Half, Half));
		if (!OutDomain.IsValid())
		{
			Error = TEXT("File produced an invalid grid domain.");
			return false;
		}
		return true;
	}

	bool DecodeFileNodeImage(const FString& Path, FImage& OutImage, FString& Error)
	{
		TArray64<uint8> Compressed;
		if (!FFileHelper::LoadFileToArray(Compressed, *Path))
		{
			Error = FString::Printf(TEXT("File could not read '%s'."), *Path);
			return false;
		}
		if (Compressed.IsEmpty())
		{
			Error = TEXT("File is empty.");
			return false;
		}

		// UE 5.8: decode through the module-level FImage API. Do not call
		// IImageWrapper::GetRawImage(FImage&) directly; keeping the wrapper
		// implementation behind IImageWrapperModule avoids the unresolved
		// GetRawImage symbol that can otherwise appear when linking this module.
		IImageWrapperModule& ImageWrapperModule =
			FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		if (!ImageWrapperModule.DecompressImage(Compressed.GetData(), Compressed.Num(), OutImage))
		{
			Error = FString::Printf(TEXT("File could not decode '%s'. Supported raster formats include TIFF/GeoTIFF, PNG, JPEG, BMP, and EXR."), *Path);
			return false;
		}
		return true;
	}

	bool EvaluateFileNodeDecoded(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FString Path = ResolveFileNodePath(Node);
		if (Path.IsEmpty())
		{
			Error = TEXT("File node has no file selected.");
			return false;
		}
		if (!FPaths::FileExists(Path))
		{
			Error = FString::Printf(TEXT("File does not exist: %s"), *Path);
			return false;
		}

		FImage Image;
		if (!DecodeFileNodeImage(Path, Image, Error)) return false;

		Image.ChangeFormat(ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		const TArrayView64<const FLinearColor> Pixels = Image.AsRGBA32F();
		const int32 SourceWidth = Image.SizeX;
		const int32 SourceHeight = Image.SizeY;
		if (SourceWidth < 2 || SourceHeight < 2 || Pixels.Num() < static_cast<int64>(SourceWidth) * SourceHeight)
		{
			Error = TEXT("File decoded image data is invalid.");
			return false;
		}

		const bool bCropSquare = Node.GetBool(TEXT("CropToSquare"), false);
		const int32 OutputWidth = bCropSquare ? FMath::Min(SourceWidth, SourceHeight) : SourceWidth;
		const int32 OutputHeight = bCropSquare ? FMath::Min(SourceWidth, SourceHeight) : SourceHeight;
		const int32 CropX = bCropSquare ? (SourceWidth - OutputWidth) / 2 : 0;
		const int32 CropY = bCropSquare ? (SourceHeight - OutputHeight) / 2 : 0;
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);

		FGaeaGridDomain Domain;
		if (!MakeFileNodeDomain(OutputWidth, OutputHeight, WorldSize, Domain, Error)) return false;

		const bool bRGB = Node.GetBool(TEXT("IsRGB"), false);
		const bool bAllowUnclamped = Node.GetBool(TEXT("AllowUnclamped"), false);
		if (bRGB)
		{
			FGaeaColorField Color;
			Color.Initialize(Domain);
			for (int32 Y = 0; Y < OutputHeight; ++Y)
			{
				for (int32 X = 0; X < OutputWidth; ++X)
				{
					FLinearColor Value = Pixels[static_cast<int64>(Y + CropY) * SourceWidth + (X + CropX)];
					if (!bAllowUnclamped)
					{
						Value.R = FMath::Clamp(Value.R, 0.0f, 1.0f);
						Value.G = FMath::Clamp(Value.G, 0.0f, 1.0f);
						Value.B = FMath::Clamp(Value.B, 0.0f, 1.0f);
						Value.A = FMath::Clamp(Value.A, 0.0f, 1.0f);
					}
					Color.AtInterior(X, Y) = Value;
				}
			}
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeColor(MoveTemp(Color)));
			return true;
		}

		float MinElevation = TNumericLimits<float>::Max();
		float MaxElevation = TNumericLimits<float>::Lowest();
		for (int32 Y = 0; Y < OutputHeight; ++Y)
		{
			for (int32 X = 0; X < OutputWidth; ++X)
			{
				const float V = Pixels[static_cast<int64>(Y + CropY) * SourceWidth + (X + CropX)].R;
				if (!FMath::IsFinite(V)) continue;
				MinElevation = FMath::Min(MinElevation, V);
				MaxElevation = FMath::Max(MaxElevation, V);
			}
		}
		if (!FMath::IsFinite(MinElevation) || !FMath::IsFinite(MaxElevation))
		{
			Error = TEXT("File contains no finite elevation samples.");
			return false;
		}

		const FString Extension = FPaths::GetExtension(Path, false).ToLower();
		const bool bTiff = Extension == TEXT("tif") || Extension == TEXT("tiff");
		const bool bLooksLikeAbsoluteElevation = bTiff && (MinElevation < -UE_KINDA_SMALL_NUMBER || MaxElevation > 1.0001f);
		float OutputHeightScale = FMath::Clamp(
			static_cast<float>(Node.GetNumber(TEXT("HeightScale"), Context.HeightScale > UE_SMALL_NUMBER ? Context.HeightScale : 8000.0f)),
			1.0f,
			1000000.0f);

		if (bLooksLikeAbsoluteElevation)
		{
			// GeoTIFF DEMs commonly store signed/float elevations directly. Preserve
			// zero as sea level by normalizing around zero rather than min/max shifting.
			OutputHeightScale = FMath::Clamp(FMath::Max(FMath::Abs(MinElevation), FMath::Abs(MaxElevation)), 1.0f, 1000000.0f);
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Height;
		Height.Initialize(Domain, Descriptor);

		for (int32 Y = 0; Y < OutputHeight; ++Y)
		{
			for (int32 X = 0; X < OutputWidth; ++X)
			{
				const float Raw = Pixels[static_cast<int64>(Y + CropY) * SourceWidth + (X + CropX)].R;
				float Value = FMath::IsFinite(Raw) ? Raw : 0.0f;
				if (bLooksLikeAbsoluteElevation)
				{
					Value /= OutputHeightScale;
				}
				else if (!bAllowUnclamped)
				{
					Value = FMath::Clamp(Value, 0.0f, 1.0f);
				}
				Height.AtInterior(X, Y) = FMath::Clamp(Value, -1.0f, 1.0f);
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("File could not publish its Height field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), OutputHeightScale));
		return true;
	}
}

void RegisterGaeaFileNodeDecoder()
{
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::File, EvaluateFileNodeDecoded);
}
