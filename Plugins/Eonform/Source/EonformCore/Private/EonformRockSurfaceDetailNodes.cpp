#include "EonformRockSurfaceDetailNodes.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* Label)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = TEXT("Terrain"); return P;
	}
	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}
	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Integer; P.DefaultInteger = Default; P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); return P;
	}
	uint32 Hash(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16; return X;
	}
	float Hash01(int32 X, int32 Y, int32 Seed, uint32 Salt = 0)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U; H ^= static_cast<uint32>(Y) * 0x85ebca6bU; H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U; H ^= Salt;
		return static_cast<float>(Hash(H) & 0x00ffffffU) / 16777215.0f;
	}
	float SmoothNoise(float X, float Y, int32 Seed, uint32 Salt = 0)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y); const float FX = X - X0, FY = Y - Y0; const float SX = FX * FX * (3.0f - 2.0f * FX), SY = FY * FY * (3.0f - 2.0f * FY);
		const float A = FMath::Lerp(Hash01(X0,Y0,Seed,Salt), Hash01(X0+1,Y0,Seed,Salt), SX); const float B = FMath::Lerp(Hash01(X0,Y0+1,Seed,Salt), Hash01(X0+1,Y0+1,Seed,Salt), SX);
		return FMath::Lerp(A,B,SY) * 2.0f - 1.0f;
	}
	float Fbm(float X, float Y, float Frequency, int32 Octaves, float Roughness, int32 Seed, uint32 Salt = 0)
	{
		float Sum=0.0f, Weight=0.0f, Amp=1.0f; for (int32 I=0; I<Octaves; ++I) { Sum += SmoothNoise(X*Frequency,Y*Frequency,Seed+I*193,Salt+I*7919u)*Amp; Weight += Amp; Frequency *= 2.03f; Amp *= Roughness; } return Weight > UE_SMALL_NUMBER ? Sum/Weight : 0.0f;
	}
	float Smooth01(float V) { V = FMath::Clamp(V,0.0f,1.0f); return V*V*(3.0f-2.0f*V); }
	const FEonformTerrainValue* RequireTerrain(const FEonformTerrainNodeInputs& Inputs, FString& Error)
	{
		const FEonformTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain")); const FEonformTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid()) { Error = TEXT("Node requires a valid Terrain input."); return nullptr; }
		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height); if (!Height || !Height->IsValid()) { Error = TEXT("Terrain input has no valid Height field."); return nullptr; }
		return Input;
	}
	bool EvaluateOutcrops(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* InputTerrain = RequireTerrain(Inputs, Error); if (!InputTerrain) return false;
		const FEonformScalarField& Source = *InputTerrain->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height); FEonformScalarField Height = Source;
		const int32 Variations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Variations"), 6)),1,32); const float Strata=FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strata"),0.25)),0.0f,1.0f); const float Density=FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"),0.45)),0.0f,1.0f); const float Shape=FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"),0.55)),0.0f,1.0f); const float Chipped=FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Chipped"),0.35)),0.0f,1.0f); const int32 Seed=static_cast<int32>(Node.GetInteger(TEXT("Seed"),1337)); const float Size=FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"),0.35)),0.02f,1.0f); const float OutcropHeight=FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"),0.45)),0.0f,1.0f); const float Rotation=FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Rotation"),0.0)));
		const float MinDim=static_cast<float>(FMath::Min(Source.Domain.Dimensions.X,Source.Domain.Dimensions.Y)); const float CellSize=FMath::Max(5.0f,Size*MinDim*0.12f);
		for(int32 Y=0;Y<Source.Domain.Dimensions.Y;++Y) for(int32 X=0;X<Source.Domain.Dimensions.X;++X)
		{
			const int32 CX=FMath::FloorToInt(X/CellSize), CY=FMath::FloorToInt(Y/CellSize); float Best=0.0f;
			for(int32 OY=-1;OY<=1;++OY) for(int32 OX=-1;OX<=1;++OX)
			{
				const int32 GX=CX+OX,GY=CY+OY; if(Hash01(GX,GY,Seed,0x11u)>Density) continue; const int32 Variation=FMath::FloorToInt(Hash01(GX,GY,Seed,0x19u)*Variations)%Variations;
				const float CenterX=(GX+0.18f+Hash01(GX,GY,Seed,0x21u)*0.64f)*CellSize, CenterY=(GY+0.18f+Hash01(GY,GX,Seed,0x31u)*0.64f)*CellSize; const float Angle=Rotation+Hash01(GX,GY,Seed,0x41u)*2.0f*PI;
				const float AxisA=CellSize*FMath::Lerp(0.20f,0.46f,Hash01(Variation,GX,Seed,0x51u)), AxisB=AxisA*FMath::Lerp(0.38f,0.82f,Hash01(Variation,GY,Seed,0x61u)); const float DX=X-CenterX,DY=Y-CenterY; const float RX=DX*FMath::Cos(Angle)+DY*FMath::Sin(Angle), RY=-DX*FMath::Sin(Angle)+DY*FMath::Cos(Angle); const float R=FMath::Sqrt(FMath::Square(RX/FMath::Max(AxisA,1.0f))+FMath::Square(RY/FMath::Max(AxisB,1.0f))); if(R>=1.0f) continue;
				float Bump=FMath::Pow(1.0f-R,FMath::Lerp(0.55f,2.8f,Shape)); const float ChipNoise=0.5f+0.5f*Fbm(X/MinDim,Y/MinDim,45.0f/FMath::Max(Size,0.02f),3,0.55f,Seed+Variation*101,0x71u); Bump*=FMath::Lerp(1.0f,Smooth01(ChipNoise),Chipped*Smooth01(R*1.4f)); if(Strata>UE_SMALL_NUMBER){const float Bands=0.72f+0.28f*FMath::Abs(FMath::Sin(Bump*PI*FMath::Lerp(18.0f,4.0f,Strata))); Bump*=FMath::Lerp(1.0f,Bands,Strata);} Best=FMath::Max(Best,Bump);
			}
			Height.AtInterior(X,Y)=FMath::Clamp(Source.AtInterior(X,Y)+Best*OutcropHeight*0.16f,-1.0f,1.0f);
		}
		Height.Descriptor.Name=EonformTerrainFieldNames::Height; FEonformTerrainDataset Dataset=InputTerrain->TerrainDataset; if(!Dataset.SetScalarField(MoveTemp(Height))){Error=TEXT("Outcrops could not publish Height.");return false;} Out.Outputs.Add(TEXT("Out"),FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset),InputTerrain->HeightScale)); return true;
	}
	bool EvaluateCraggy(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* InputTerrain=RequireTerrain(Inputs,Error); if(!InputTerrain)return false; const FEonformScalarField& Source=*InputTerrain->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height); FEonformScalarField Height=Source;
		const float Size=FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"),0.35)),0.01f,1.0f), Depth=FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Depth"),0.45)),0.0f,1.0f), Shape=FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"),0.55)),0.0f,1.0f); const int32 Seed=static_cast<int32>(Node.GetInteger(TEXT("Seed"),1337)); const int32 W=Source.Domain.Dimensions.X,H=Source.Domain.Dimensions.Y;
		for(int32 Y=0;Y<H;++Y){const float V=H>1?static_cast<float>(Y)/(H-1):0.0f;for(int32 X=0;X<W;++X){const float U=W>1?static_cast<float>(X)/(W-1):0.0f;const float Frequency=FMath::Lerp(52.0f,7.0f,Size);const float N0=Fbm(U,V,Frequency,5,0.53f,Seed,0x17u),N1=Fbm(U,V,Frequency*1.83f,4,0.57f,Seed+811,0x31u);const float Ridge=FMath::Pow(FMath::Clamp(1.0f-FMath::Abs(N0),0.0f,1.0f),FMath::Lerp(0.8f,3.2f,Shape));const float Crack=FMath::Pow(FMath::Clamp(FMath::Abs(N1),0.0f,1.0f),FMath::Lerp(2.8f,0.9f,Shape));const float Broken=(Ridge-0.48f)*0.70f-Crack*0.30f;Height.AtInterior(X,Y)=FMath::Clamp(Source.AtInterior(X,Y)+Broken*Depth*0.15f,-1.0f,1.0f);}}
		Height.Descriptor.Name=EonformTerrainFieldNames::Height;FEonformTerrainDataset Dataset=InputTerrain->TerrainDataset;if(!Dataset.SetScalarField(MoveTemp(Height))){Error=TEXT("Craggy could not publish Height.");return false;}Out.Outputs.Add(TEXT("Out"),FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset),InputTerrain->HeightScale));return true;
	}
}

void RegisterEonformOutcropsNode()
{
	FEonformTerrainNodeDescriptor D;D.Type=EonformTerrainNodeTypes::Outcrops;D.DisplayName=TEXT("Outcrops");D.Category=TEXT("Surface");D.Description=TEXT("Generates sparse oriented rock outcrops with strata and chipped-edge controls.");D.Inputs.Add(TerrainPort(TEXT("Terrain"),TEXT("Input")));D.Outputs.Add(TerrainPort(TEXT("Out"),TEXT("Out")));D.Parameters={Int(TEXT("Variations"),TEXT("Variations"),6,1,32),Num(TEXT("Strata"),TEXT("Strata"),0.25,0.0,1.0),Num(TEXT("Density"),TEXT("Density"),0.45,0.0,1.0),Num(TEXT("Shape"),TEXT("Shape"),0.55,0.0,1.0),Num(TEXT("Chipped"),TEXT("Chipped"),0.35,0.0,1.0),Int(TEXT("Seed"),TEXT("Seed"),1337,-2147483647,2147483647),Num(TEXT("Size"),TEXT("Size"),0.35,0.02,1.0),Num(TEXT("Height"),TEXT("Height"),0.45,0.0,1.0),Num(TEXT("Rotation"),TEXT("Rotation"),0.0,-360.0,360.0)};FEonformTerrainNodeDescriptorRegistry::Register(D);FEonformTerrainNodeRegistry::Register(D.Type,EvaluateOutcrops);
}
void RegisterEonformCraggyNode()
{
	FEonformTerrainNodeDescriptor D;D.Type=EonformTerrainNodeTypes::Craggy;D.DisplayName=TEXT("Craggy");D.Category=TEXT("Surface");D.Description=TEXT("Adds broken rocky character with controllable feature size, depth, and shape.");D.Inputs.Add(TerrainPort(TEXT("Terrain"),TEXT("Input")));D.Outputs.Add(TerrainPort(TEXT("Out"),TEXT("Out")));D.Parameters={Num(TEXT("Size"),TEXT("Size"),0.35,0.01,1.0),Num(TEXT("Depth"),TEXT("Depth"),0.45,0.0,1.0),Num(TEXT("Shape"),TEXT("Shape"),0.55,0.0,1.0),Int(TEXT("Seed"),TEXT("Seed"),1337,-2147483647,2147483647)};FEonformTerrainNodeDescriptorRegistry::Register(D);FEonformTerrainNodeRegistry::Register(D.Type,EvaluateCraggy);
}
