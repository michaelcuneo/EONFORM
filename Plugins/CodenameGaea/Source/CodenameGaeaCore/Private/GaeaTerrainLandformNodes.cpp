#include "GaeaTerrainLandformNodes.h"
#include "GaeaModifySpatialNodes.h"
#include "GaeaShaperNode.h"
#include "GaeaSimulateEvolutionNodes.h"
#include "GaeaSurfaceNodes.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainLandformOps.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor TerrainOut(){ FGaeaTerrainPortDescriptor P; P.Name=TEXT("Out"); P.DisplayName=TEXT("Out"); P.DataType=TEXT("Terrain"); return P; }
	FGaeaTerrainPortDescriptor ScalarOut(FName N,const TCHAR* L){ FGaeaTerrainPortDescriptor P; P.Name=N; P.DisplayName=L; P.DataType=TEXT("ScalarField"); return P; }
	FGaeaTerrainParameterDescriptor Num(FName N,const TCHAR* L,double D,double Min,double Max,const TCHAR* G){ FGaeaTerrainParameterDescriptor P; P.Name=N; P.DisplayName=L; P.Group=G; P.Type=EGaeaTerrainParameterType::Number; P.DefaultNumber=D; P.bHasMinimum=true; P.Minimum=Min; P.bHasMaximum=true; P.Maximum=Max; return P; }
	FGaeaTerrainParameterDescriptor Int(FName N,const TCHAR* L,int64 D,int64 Min,int64 Max,const TCHAR* G){ FGaeaTerrainParameterDescriptor P; P.Name=N; P.DisplayName=L; P.Group=G; P.Type=EGaeaTerrainParameterType::Integer; P.DefaultInteger=D; P.bHasMinimum=true; P.Minimum=static_cast<double>(Min); P.bHasMaximum=true; P.Maximum=static_cast<double>(Max); return P; }
	FGaeaTerrainParameterDescriptor Bool(FName N,const TCHAR* L,bool D,const TCHAR* G){ FGaeaTerrainParameterDescriptor P; P.Name=N; P.DisplayName=L; P.Group=G; P.Type=EGaeaTerrainParameterType::Boolean; P.DefaultBoolean=D; return P; }
	FGaeaTerrainParameterDescriptor Choice(FName N,const TCHAR* L,FName D,std::initializer_list<FName> O,const TCHAR* G){ FGaeaTerrainParameterDescriptor P; P.Name=N; P.DisplayName=L; P.Group=G; P.Type=EGaeaTerrainParameterType::Name; P.DefaultName=D; for(FName V:O) P.NameOptions.Add(V); return P; }
	float Smooth01(float V){ V=FMath::Clamp(V,0.0f,1.0f); return V*V*(3.0f-2.0f*V); }
	FGuid Cid(int32 Seed,uint32 O){ return FGuid(0x4D544E00u+O,0x454F4E46u,static_cast<uint32>(Seed),0x434F4D50u); }
	FGaeaTerrainNode& Add(FGaeaTerrainRecipe& R,FName T,int32 S,uint32 O){ FGaeaTerrainNode N; N.Id=Cid(S,O); N.Type=T; return R.Nodes.Add_GetRef(MoveTemp(N)); }
	void Link(FGaeaTerrainRecipe& R,const FGaeaTerrainNode& A,FName AO,const FGaeaTerrainNode& B,FName BI){ FGaeaTerrainConnection C; C.FromNode=A.Id; C.FromOutput=AO; C.ToNode=B.Id; C.ToInput=BI; R.Connections.Add(C); }

	void EnsureCompositeNodes()
	{
		if(!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Shaper)) RegisterGaeaShaperNode();
		if(!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Warp)) RegisterGaeaWarpNode();
		if(!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::Erosion2)) RegisterGaeaSimulateEvolutionNodes();
		if(!FGaeaTerrainNodeRegistry::IsRegistered(GaeaTerrainNodeTypes::RockNoise)) RegisterGaeaSurfaceNodes();
	}

	bool RebuildSemantics(FGaeaTerrainDataset& D,const FGaeaScalarField& RawMass,float RequestedHeight,float HeightScale,const FGaeaTerrainPhysicalMetrics& Metrics,FString& Error)
	{
		if(!FGaeaTerrainDerivedData::EnsureContext(D,HeightScale,Metrics,&Error)) return false;
		const FGaeaScalarField* H=D.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* S=D.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* C=D.FindScalarField(GaeaTerrainFieldNames::Concavity);
		const FGaeaScalarField* V=D.FindScalarField(GaeaTerrainFieldNames::Convexity);
		if(!H||!S||!C||!V||RawMass.Domain!=H->Domain){ Error=TEXT("Mountain could not rebuild final semantic fields."); return false; }
		auto F=[H](FName N){ FGaeaScalarField O=*H; O.Descriptor.Name=N; O.Descriptor.Unit=EGaeaFieldUnit::Normalized; return O; };
		FGaeaScalarField Mass=F(GaeaTerrainFieldNames::MountainMass), Uplift=F(GaeaTerrainFieldNames::Uplift), Ridge=F(GaeaTerrainFieldNames::RidgeNetwork), Drain=F(GaeaTerrainFieldNames::DrainageReadiness), Erode=F(GaeaTerrainFieldNames::ErosionEligibility), Rock=F(GaeaTerrainFieldNames::RockExposure), Cryo=F(GaeaTerrainFieldNames::CryosphereEligibility);
		const float Den=FMath::Max(RequestedHeight,UE_SMALL_NUMBER);
		for(int32 Y=0;Y<H->Domain.Dimensions.Y;++Y) for(int32 X=0;X<H->Domain.Dimensions.X;++X)
		{
			const float M=Smooth01(RawMass.AtInterior(X,Y));
			const float HH=FMath::Clamp(H->AtInterior(X,Y)/Den,0.0f,1.0f);
			const float SS=FMath::Clamp(S->AtInterior(X,Y)/70.0f,0.0f,1.0f);
			const float CC=FMath::Clamp(C->AtInterior(X,Y)*0.5f+0.5f,0.0f,1.0f);
			const float VV=FMath::Clamp(V->AtInterior(X,Y)*0.5f+0.5f,0.0f,1.0f);
			const float RR=M*FMath::Clamp(VV*0.58f+SS*0.42f,0.0f,1.0f);
			Mass.AtInterior(X,Y)=M; Uplift.AtInterior(X,Y)=HH*M; Ridge.AtInterior(X,Y)=RR;
			Drain.AtInterior(X,Y)=M*FMath::Clamp(SS*0.52f+CC*0.48f,0.0f,1.0f);
			Erode.AtInterior(X,Y)=M*FMath::Clamp(SS*0.72f+HH*0.28f,0.0f,1.0f);
			Rock.AtInterior(X,Y)=M*FMath::Clamp(SS*0.48f+VV*0.30f+HH*0.22f,0.0f,1.0f);
			Cryo.AtInterior(X,Y)=M*Smooth01((HH-0.58f)/0.32f)*FMath::Lerp(0.72f,1.0f,RR);
		}
		return D.SetHeightDerivedScalarField(MoveTemp(Mass)) && D.SetHeightDerivedScalarField(MoveTemp(Uplift)) && D.SetHeightDerivedScalarField(MoveTemp(Ridge)) && D.SetHeightDerivedScalarField(MoveTemp(Drain)) && D.SetHeightDerivedScalarField(MoveTemp(Erode)) && D.SetHeightDerivedScalarField(MoveTemp(Rock)) && D.SetHeightDerivedScalarField(MoveTemp(Cryo));
	}

	bool RunComposite(const FGaeaMountainLandformSettings& S,const FGaeaTerrainEvaluationContext& Outer,FGaeaMountainLandformResult& R,FString& Error)
	{
		const FGaeaScalarField* RawMassPtr=R.Dataset.FindScalarField(GaeaTerrainFieldNames::MountainMass);
		if(!RawMassPtr){ Error=TEXT("Mountain base has no MountainMass field."); return false; }
		const FGaeaScalarField RawMass=*RawMassPtr;
		EnsureCompositeNodes();
		FGaeaTerrainRecipe G; G.Nodes.Reserve(9); G.Connections.Reserve(8); uint32 O=1;
		FGaeaTerrainNode& Source=Add(G,GaeaTerrainNodeTypes::SourceDataset,S.Seed,O++);
		FGaeaTerrainNode& Shape=Add(G,GaeaTerrainNodeTypes::Shaper,S.Seed,O++); Shape.NumericParameters.Add(TEXT("Shape"),S.Style==TEXT("Old")?0.025:0.065); Shape.NumericParameters.Add(TEXT("LocalEffect"),0.12); Shape.NumericParameters.Add(TEXT("LocalArea"),0.50); Shape.BoolParameters.Add(TEXT("MaintainFineDetails"),true); Shape.NumericParameters.Add(TEXT("DetailSize"),S.bReduceDetails?0.50:0.28); Link(G,Source,TEXT("Terrain"),Shape,TEXT("Terrain"));
		FGaeaTerrainNode& Warp=Add(G,GaeaTerrainNodeTypes::Warp,S.Seed,O++); Warp.NumericParameters.Add(TEXT("Size"),S.Style==TEXT("Alpine")?0.52:0.62); Warp.NumericParameters.Add(TEXT("Strength"),S.Style==TEXT("Old")?0.014:0.026); Warp.IntegerParameters.Add(TEXT("Seed"),static_cast<int64>(S.Seed)+101); Link(G,Shape,TEXT("Out"),Warp,TEXT("Input"));
		FGaeaTerrainNode& Macro=Add(G,GaeaTerrainNodeTypes::Erosion2,S.Seed,O++); Macro.IntegerParameters.Add(TEXT("Duration"),S.bReduceDetails?10:(S.Style==TEXT("Eroded")?36:28)); Macro.NumericParameters.Add(TEXT("Downcutting"),S.Style==TEXT("Alpine")?0.78:0.62); Macro.NumericParameters.Add(TEXT("ErosionScale"),S.Style==TEXT("Alpine")?1.45:1.75); Macro.IntegerParameters.Add(TEXT("Seed"),static_cast<int64>(S.Seed)+307); Macro.NumericParameters.Add(TEXT("SuspendedLoad"),0.52); Macro.NumericParameters.Add(TEXT("BedLoad"),0.46); Macro.NumericParameters.Add(TEXT("CoarseSediments"),0.28); Macro.NumericParameters.Add(TEXT("DepositionBoost"),0.12); Macro.NumericParameters.Add(TEXT("Shape"),0.48); Macro.NumericParameters.Add(TEXT("ShapeSharpness"),S.Style==TEXT("Alpine")?0.66:0.50); Macro.NumericParameters.Add(TEXT("ShapeDetailScale"),1.0); Macro.BoolParameters.Add(TEXT("EnableOrographic"),S.Style==TEXT("Alpine")); Macro.NumericParameters.Add(TEXT("Direction"),25.0); Macro.NumericParameters.Add(TEXT("DirectionalPrecipitation"),0.38); Macro.NumericParameters.Add(TEXT("RainShadow"),0.12); Link(G,Warp,TEXT("Out"),Macro,TEXT("Terrain"));
		FGaeaTerrainNode* Last=&Macro;
		if(!S.bReduceDetails)
		{
			FGaeaTerrainNode& Rock=Add(G,GaeaTerrainNodeTypes::RockNoise,S.Seed,O++); Rock.NumericParameters.Add(TEXT("Strength"),S.Style==TEXT("Alpine")?0.11:0.085); Rock.NumericParameters.Add(TEXT("Scale"),S.Style==TEXT("Alpine")?0.68:0.82); Rock.IntegerParameters.Add(TEXT("Seed"),static_cast<int64>(S.Seed)+401); Link(G,Macro,TEXT("Out"),Rock,TEXT("Terrain"));
			FGaeaTerrainNode& Fine=Add(G,GaeaTerrainNodeTypes::Erosion2,S.Seed,O++); Fine.IntegerParameters.Add(TEXT("Duration"),S.Style==TEXT("Eroded")?20:14); Fine.NumericParameters.Add(TEXT("Downcutting"),S.Style==TEXT("Alpine")?0.68:0.50); Fine.NumericParameters.Add(TEXT("ErosionScale"),S.Style==TEXT("Alpine")?0.36:0.48); Fine.IntegerParameters.Add(TEXT("Seed"),static_cast<int64>(S.Seed)+503); Fine.NumericParameters.Add(TEXT("SuspendedLoad"),0.34); Fine.NumericParameters.Add(TEXT("BedLoad"),0.26); Fine.NumericParameters.Add(TEXT("CoarseSediments"),0.15); Fine.NumericParameters.Add(TEXT("DepositionBoost"),0.06); Fine.NumericParameters.Add(TEXT("Shape"),0.24); Fine.NumericParameters.Add(TEXT("ShapeSharpness"),0.42); Fine.NumericParameters.Add(TEXT("ShapeDetailScale"),0.62); Link(G,Rock,TEXT("Out"),Fine,TEXT("Terrain")); Last=&Fine;
		}
		FGaeaTerrainNode& Thermal=Add(G,GaeaTerrainNodeTypes::Thermal2,S.Seed,O++); Thermal.IntegerParameters.Add(TEXT("Duration"),S.bReduceDetails?5:10); Thermal.NumericParameters.Add(TEXT("Strength"),S.Style==TEXT("Old")?0.34:0.22); Thermal.NumericParameters.Add(TEXT("Anisotropy"),S.Style==TEXT("Alpine")?0.10:0.03); Thermal.NumericParameters.Add(TEXT("Angle"),S.Style==TEXT("Alpine")?39.0:35.0); Thermal.NumericParameters.Add(TEXT("SedimentRemoval"),S.Style==TEXT("Alpine")?0.10:0.03); Thermal.NumericParameters.Add(TEXT("FeatureScale"),S.Style==TEXT("Alpine")?22.0:34.0); Link(G,*Last,TEXT("Out"),Thermal,TEXT("Terrain")); Last=&Thermal;
		if(!S.bReduceDetails){ FGaeaTerrainNode& Ground=Add(G,GaeaTerrainNodeTypes::GroundTexture,S.Seed,O++); Ground.NumericParameters.Add(TEXT("Strength"),S.Style==TEXT("Alpine")?0.085:0.065); Ground.NumericParameters.Add(TEXT("Scale"),S.Style==TEXT("Alpine")?0.62:0.78); Ground.IntegerParameters.Add(TEXT("Seed"),static_cast<int64>(S.Seed)+701); Link(G,Thermal,TEXT("Out"),Ground,TEXT("Terrain")); Last=&Ground; }
		G.OutputNode=Last->Id;
		FGaeaTerrainEvaluationContext Inner=Outer; Inner.SourceDataset=R.Dataset; Inner.HeightScale=R.HeightScale;
		const FGaeaTerrainEvaluationResult Eval=FGaeaTerrainEvaluator::Evaluate(G,Inner);
		if(!Eval.bSuccess){ Error=FString::Printf(TEXT("Mountain internal composite failed: %s"),*Eval.Error); return false; }
		R.Dataset=Eval.Dataset; R.HeightScale=Eval.HeightScale;
		return RebuildSemantics(R.Dataset,RawMass,S.Height,R.HeightScale,Outer.PhysicalMetrics,Error);
	}

	bool EvaluateMountain(const FGaeaTerrainNode& N,const FGaeaTerrainNodeInputs&,const FGaeaTerrainEvaluationContext& C,FGaeaTerrainNodeEvaluation& Out,FString& Error)
	{
		FGaeaMountainLandformSettings S; S.bReduceDetails=N.GetBool(TEXT("ReduceDetails"),false);
		const int32 Native=S.bReduceDetails?513:1009; const int32 Requested=C.TargetResolution.X>0?FMath::Max(C.TargetResolution.X,C.TargetResolution.Y):Native; S.Resolution=FMath::Clamp(Requested,257,S.bReduceDetails?1009:2017);
		S.WorldSize=100000.0f; S.HeightScale=C.PhysicalMetrics.HasElevationScale()?static_cast<float>(C.PhysicalMetrics.ElevationScaleMeters*100.0):FMath::Max(C.HeightScale,300000.0f); S.Scale=static_cast<float>(N.GetNumber(TEXT("Scale"),1.0)); S.Height=static_cast<float>(N.GetNumber(TEXT("Height"),0.92)); S.Style=N.GetName(TEXT("Style"),TEXT("Basic")); S.Bulk=N.GetName(TEXT("Bulk"),TEXT("Medium")); S.Seed=static_cast<int32>(N.GetInteger(TEXT("Seed"),1337)); S.OffsetX=static_cast<float>(N.GetNumber(TEXT("X"),0.0)); S.OffsetY=static_cast<float>(N.GetNumber(TEXT("Y"),0.0));
		FGaeaMountainLandformResult R; if(!FGaeaTerrainLandformOps::BuildMountain(S,C.PhysicalMetrics,R,&Error)||!RunComposite(S,C,R,Error)) return false;
		auto Pub=[&](FName O,FName F){ if(const FGaeaScalarField* P=R.Dataset.FindScalarField(F)) Out.Outputs.Add(O,FGaeaTerrainValue::MakeScalarField(*P)); };
		Pub(TEXT("Mass"),GaeaTerrainFieldNames::MountainMass); Pub(TEXT("Uplift"),GaeaTerrainFieldNames::Uplift); Pub(TEXT("Ridges"),GaeaTerrainFieldNames::RidgeNetwork); Pub(TEXT("DrainageReadiness"),GaeaTerrainFieldNames::DrainageReadiness); Pub(TEXT("ErosionEligibility"),GaeaTerrainFieldNames::ErosionEligibility); Pub(TEXT("RockExposure"),GaeaTerrainFieldNames::RockExposure); Pub(TEXT("CryosphereEligibility"),GaeaTerrainFieldNames::CryosphereEligibility);
		FGaeaTerrainValue T=FGaeaTerrainValue::MakeTerrain(MoveTemp(R.Dataset),R.HeightScale); if(!T.IsValid()){ Error=TEXT("Mountain produced invalid terrain."); return false; } Out.Outputs.Add(TEXT("Out"),MoveTemp(T)); Error.Reset(); return true;
	}
}

void RegisterGaeaTerrainLandformNodes()
{
	EnsureCompositeNodes(); FGaeaTerrainNodeDescriptor D; D.Type=GaeaTerrainNodeTypes::Mountain; D.DisplayName=TEXT("Mountain"); D.Category=TEXT("Terrain"); D.Description=TEXT("Multi-scale mountain landform with hydraulic incision, thermal weathering, rock breakup and fine surface detail.");
	D.Outputs={TerrainOut(),ScalarOut(TEXT("Mass"),TEXT("Mass")),ScalarOut(TEXT("Uplift"),TEXT("Uplift")),ScalarOut(TEXT("Ridges"),TEXT("Ridges")),ScalarOut(TEXT("DrainageReadiness"),TEXT("Drainage Readiness")),ScalarOut(TEXT("ErosionEligibility"),TEXT("Erosion Eligibility")),ScalarOut(TEXT("RockExposure"),TEXT("Rock Exposure")),ScalarOut(TEXT("CryosphereEligibility"),TEXT("Cryosphere Eligibility"))};
	D.Parameters.Add(Num(TEXT("Scale"),TEXT("Scale"),1.0,0.1,2.0,TEXT("Mountain"))); D.Parameters.Add(Num(TEXT("Height"),TEXT("Height"),0.92,0.0,1.0,TEXT("Mountain"))); D.Parameters.Add(Choice(TEXT("Style"),TEXT("Style"),TEXT("Basic"),{TEXT("Basic"),TEXT("Eroded"),TEXT("Old"),TEXT("Alpine"),TEXT("Strata")},TEXT("Mountain"))); D.Parameters.Add(Choice(TEXT("Bulk"),TEXT("Bulk"),TEXT("Medium"),{TEXT("Low"),TEXT("Medium"),TEXT("High")},TEXT("Mountain"))); D.Parameters.Add(Bool(TEXT("ReduceDetails"),TEXT("Reduce Details"),false,TEXT("Mountain"))); D.Parameters.Add(Int(TEXT("Seed"),TEXT("Seed"),1337,-2147483647,2147483647,TEXT("Mountain"))); D.Parameters.Add(Num(TEXT("X"),TEXT("X"),0.0,-1.5,1.5,TEXT("Position"))); D.Parameters.Add(Num(TEXT("Y"),TEXT("Y"),0.0,-1.5,1.5,TEXT("Position")));
	FGaeaTerrainNodeDescriptorRegistry::Register(D); FGaeaTerrainNodeRegistry::Register(D.Type,EvaluateMountain);
}
