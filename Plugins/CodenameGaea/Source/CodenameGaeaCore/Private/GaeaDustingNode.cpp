#include "GaeaCryosphereNodes.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaDustingNode
{
	FGaeaTerrainPortDescriptor TerrainPort(FName N, const TCHAR* L) { FGaeaTerrainPortDescriptor P; P.Name=N; P.DisplayName=L; P.DataType=TEXT("Terrain"); return P; }
	FGaeaTerrainPortDescriptor ScalarPort(FName N, const TCHAR* L) { FGaeaTerrainPortDescriptor P; P.Name=N; P.DisplayName=L; P.DataType=TEXT("ScalarField"); return P; }
	FGaeaTerrainParameterDescriptor Num(FName N,const TCHAR* L,double D,double Min,double Max,const TCHAR* G){ FGaeaTerrainParameterDescriptor P; P.Name=N; P.DisplayName=L; P.Type=EGaeaTerrainParameterType::Number; P.DefaultNumber=D; P.bHasMinimum=true; P.Minimum=Min; P.bHasMaximum=true; P.Maximum=Max; P.Group=G; return P; }
	FGaeaTerrainParameterDescriptor Bool(FName N,const TCHAR* L,bool D,const TCHAR* G){ FGaeaTerrainParameterDescriptor P; P.Name=N; P.DisplayName=L; P.Type=EGaeaTerrainParameterType::Boolean; P.DefaultBool=D; P.Group=G; return P; }
	FGaeaScalarField MakeScalar(const FGaeaGridDomain& D,FName N,EGaeaFieldUnit U=EGaeaFieldUnit::Normalized){ FGaeaFieldDescriptor X; X.Name=N; X.Unit=U; X.Interpolation=EGaeaInterpolation::Bilinear; FGaeaScalarField F; F.Initialize(D,X,0.0f); return F; }
	float HashNoise(int32 X,int32 Y,int32 Seed){ uint32 H=static_cast<uint32>(X)*1597334677u+static_cast<uint32>(Y)*3812015801u+static_cast<uint32>(Seed)*95868953u; H^=H>>16u; H*=2246822519u; H^=H>>13u; return static_cast<float>(H&0x00ffffffu)/16777215.0f; }
	float Smooth01(double V){ const float T=static_cast<float>(FMath::Clamp(V,0.0,1.0)); return T*T*(3.0f-2.0f*T); }

	bool EvaluateDusting(const FGaeaTerrainNode& Node,const FGaeaTerrainNodeInputs& Inputs,const FGaeaTerrainEvaluationContext& Context,FGaeaTerrainNodeEvaluation& Out,FString& Error)
	{
		const FGaeaTerrainValue* const* Ptr=Inputs.Find(TEXT("Terrain")); const FGaeaTerrainValue* Input=Ptr?*Ptr:nullptr;
		if(!Input||Input->Type!=EGaeaTerrainValueType::Terrain||!Input->IsValid()){ Error=TEXT("Dusting requires a valid terrain input 'Terrain'."); return false; }
		FGaeaTerrainDataset Dataset=Input->TerrainDataset;
		if(!FGaeaTerrainDerivedData::EnsureContext(Dataset,Input->HeightScale,Context.PhysicalMetrics,&Error)) return false;
		const FGaeaScalarField* Source=Dataset.FindScalarField(GaeaTerrainFieldNames::Height); const FGaeaScalarField* Slope=Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees); const FGaeaScalarField* Concavity=Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		if(!Source||!Slope||!Concavity){ Error=TEXT("Dusting could not resolve Height, SlopeDegrees, and Concavity."); return false; }
		const double Scale=Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale); if(Scale<=UE_DOUBLE_SMALL_NUMBER){ Error=TEXT("Dusting could not resolve physical elevation scale."); return false; }
		const double Snowline=Node.GetNumber(TEXT("SnowlineMeters"),800.0); const double Falloff=FMath::Max(Node.GetNumber(TEXT("FalloffMeters"),250.0),1.0); const double MaxDepth=FMath::Max(Node.GetNumber(TEXT("MaxDepthMeters"),0.08),0.0);
		const double Coverage=FMath::Clamp(Node.GetNumber(TEXT("Coverage"),0.7),0.0,1.0); const double SlopeCoverage=FMath::Clamp(Node.GetNumber(TEXT("SlopeCoverage"),0.55),0.0,1.0); const double Melt=FMath::Clamp(Node.GetNumber(TEXT("Melt"),0.15),0.0,1.0); const double Grit=FMath::Clamp(Node.GetNumber(TEXT("Grit"),0.3),0.0,1.0); const double Shelter=FMath::Clamp(Node.GetNumber(TEXT("Shelter"),0.35),0.0,1.0); const int32 Seed=FMath::RoundToInt(Node.GetNumber(TEXT("Seed"),7331.0)); const bool bIncludeExisting=Node.GetBool(TEXT("IncludeExistingSnow"),true); const bool bAffectHeight=Node.GetBool(TEXT("AffectHeight"),false);
		const FGaeaScalarField* Existing=Dataset.FindScalarField(TEXT("SnowDepth"));
		FGaeaScalarField Height=*Source; FGaeaScalarField Dust=MakeScalar(Source->Domain,TEXT("Dusting")); FGaeaScalarField Depth=MakeScalar(Source->Domain,TEXT("DustingDepth"),EGaeaFieldUnit::Meters);
		for(int32 Y=0;Y<Source->Domain.Dimensions.Y;++Y){ for(int32 X=0;X<Source->Domain.Dimensions.X;++X){ const double Elev=static_cast<double>(Source->AtInterior(X,Y))*Scale; const double Alt=Smooth01((Elev-(Snowline-Falloff))/(2.0*Falloff)); const double SlopeDeg=Slope->AtInterior(X,Y); const double Stable=FMath::Lerp(1.0,FMath::Clamp(1.0-SlopeDeg/70.0,0.0,1.0),SlopeCoverage); const double Concave=FMath::Clamp(static_cast<double>(Concavity->AtInterior(X,Y)),0.0,1.0); const double Retain=FMath::Lerp(1.0,0.65+0.35*Concave,Shelter); const double Noise=FMath::Lerp(1.0,0.55+0.45*HashNoise(X,Y,Seed),Grit); const double ExistingBoost=(bIncludeExisting&&Existing&&Existing->Domain==Source->Domain)?FMath::Clamp(static_cast<double>(Existing->AtInterior(X,Y))/FMath::Max(MaxDepth,0.01),0.0,1.0)*0.15:0.0; const double Amount=FMath::Clamp((Alt*Stable*Retain*Noise*Coverage+ExistingBoost)*(1.0-Melt),0.0,1.0); const double Meters=MaxDepth*Amount; Dust.AtInterior(X,Y)=static_cast<float>(Amount); Depth.AtInterior(X,Y)=static_cast<float>(Meters); if(bAffectHeight) Height.AtInterior(X,Y)=FMath::Clamp(Source->AtInterior(X,Y)+static_cast<float>(Meters/Scale),-1.0f,1.0f); }}
		if(bAffectHeight){ Height.Descriptor.Name=GaeaTerrainFieldNames::Height; if(!Dataset.SetScalarField(MoveTemp(Height))){ Error=TEXT("Dusting could not publish dusted Height."); return false; }}
		FGaeaScalarField DustOut=Dust,DepthOut=Depth; if(!Dataset.SetHeightDerivedScalarField(MoveTemp(Dust))||!Dataset.SetHeightDerivedScalarField(MoveTemp(Depth))){ Error=TEXT("Dusting could not publish dusting fields."); return false; }
		Out.Outputs.Add(TEXT("Out"),FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset),Input->HeightScale)); Out.Outputs.Add(TEXT("Dusting"),FGaeaTerrainValue::MakeScalarField(MoveTemp(DustOut))); Out.Outputs.Add(TEXT("Depth"),FGaeaTerrainValue::MakeScalarField(MoveTemp(DepthOut))); Error.Reset(); return true;
	}
}

void RegisterGaeaDustingNode()
{
	using namespace GaeaDustingNode; FGaeaTerrainNodeDescriptor D; D.Type=FName(TEXT("Dusting")); D.DisplayName=TEXT("Dusting"); D.Category=TEXT("Simulate"); D.Description=TEXT("Applies a physically thin, patchy snow dusting controlled by snowline, slope stability, terrain shelter, melt, and small-scale surface variation, optionally respecting an existing snowpack."); D.Inputs.Add(TerrainPort(TEXT("Terrain"),TEXT("Input"))); D.Outputs.Add(TerrainPort(TEXT("Out"),TEXT("Out"))); D.Outputs.Add(ScalarPort(TEXT("Dusting"),TEXT("Dusting"))); D.Outputs.Add(ScalarPort(TEXT("Depth"),TEXT("Depth")));
	D.Parameters.Add(Num(TEXT("SnowlineMeters"),TEXT("Snowline (m)"),800.0,-1000.0,10000.0,TEXT("Coverage"))); D.Parameters.Add(Num(TEXT("FalloffMeters"),TEXT("Falloff (m)"),250.0,1.0,5000.0,TEXT("Coverage"))); D.Parameters.Add(Num(TEXT("MaxDepthMeters"),TEXT("Maximum Depth (m)"),0.08,0.0,2.0,TEXT("Snow"))); D.Parameters.Add(Num(TEXT("Coverage"),TEXT("Coverage"),0.7,0.0,1.0,TEXT("Coverage"))); D.Parameters.Add(Num(TEXT("SlopeCoverage"),TEXT("Slope Coverage"),0.55,0.0,1.0,TEXT("Coverage"))); D.Parameters.Add(Num(TEXT("Melt"),TEXT("Melt"),0.15,0.0,1.0,TEXT("Snow"))); D.Parameters.Add(Num(TEXT("Grit"),TEXT("Grit"),0.3,0.0,1.0,TEXT("Variation"))); D.Parameters.Add(Num(TEXT("Shelter"),TEXT("Shelter"),0.35,0.0,1.0,TEXT("Variation"))); D.Parameters.Add(Num(TEXT("Seed"),TEXT("Seed"),7331.0,0.0,1000000.0,TEXT("Variation"))); D.Parameters.Add(Bool(TEXT("IncludeExistingSnow"),TEXT("Include Existing Snow"),true,TEXT("Snow"))); D.Parameters.Add(Bool(TEXT("AffectHeight"),TEXT("Affect Height"),false,TEXT("Output"))); FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(D.Type,EvaluateDusting);
}
