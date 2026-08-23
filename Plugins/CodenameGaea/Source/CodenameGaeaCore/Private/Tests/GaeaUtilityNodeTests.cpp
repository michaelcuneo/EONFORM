#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaGridDomain.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTerrainUtilityOps.h"
#include "GaeaUtilityNodes.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaScalarField MakeUtilityField(FName Name)
	{
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(FIntPoint(17,17),FVector2d(-8000.0,-8000.0),FVector2d(8000.0,8000.0));
		FGaeaFieldDescriptor Descriptor; Descriptor.Name=Name; Descriptor.Unit=EGaeaFieldUnit::Normalized; Descriptor.Interpolation=EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field; Field.Initialize(Domain,Descriptor,0.0f);
		for(int32 Y=0;Y<17;++Y) for(int32 X=0;X<17;++X)
		{
			const float NX=static_cast<float>(X)/16.0f; const float NY=static_cast<float>(Y)/16.0f;
			Field.AtInterior(X,Y)=FMath::Clamp(0.15f+0.55f*NX+0.20f*FMath::Sin(NY*PI*2.0f),-1.0f,1.0f);
		}
		return Field;
	}

	FGaeaTerrainEvaluationContext MakeContext()
	{
		FGaeaTerrainEvaluationContext Context; Context.HeightScale=2400.0f; Context.SourceDataset.SetScalarField(MakeUtilityField(GaeaTerrainFieldNames::Height)); return Context;
	}

	FGaeaTerrainRecipe MakeMathRecipe()
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source; Source.Id=FGuid(7701,1,1,1); Source.Type=GaeaTerrainNodeTypes::SourceDataset;
		FGaeaTerrainNode Math; Math.Id=FGuid(7702,2,2,2); Math.Type=GaeaTerrainNodeTypes::Math; Math.NameParameters.Add(TEXT("Expression"),TEXT("a*0.5"));
		FGaeaTerrainConnection C; C.FromNode=Source.Id; C.FromOutput=TEXT("Terrain"); C.ToNode=Math.Id; C.ToInput=TEXT("A");
		Recipe.Nodes={Source,Math}; Recipe.Connections={C}; Recipe.OutputNode=Math.Id; return Recipe;
	}

	FGaeaTerrainRecipe MakeRoutingRecipe(FName Type)
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source; Source.Id=FGuid(7801,1,1,1); Source.Type=GaeaTerrainNodeTypes::SourceDataset;
		FGaeaTerrainNode Utility; Utility.Id=FGuid(7802,2,2,GetTypeHash(Type)); Utility.Type=Type;
		if(Type==GaeaTerrainNodeTypes::Switch) Utility.IntegerParameters.Add(TEXT("Index"),0);
		FGaeaTerrainConnection C; C.FromNode=Source.Id; C.FromOutput=TEXT("Terrain"); C.ToNode=Utility.Id; C.ToInput=Type==GaeaTerrainNodeTypes::Switch?TEXT("Input1"):TEXT("Input");
		Recipe.Nodes={Source,Utility}; Recipe.Connections={C}; Recipe.OutputNode=Utility.Id; return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaUtilityContractsTest,"CodenameGaea.Core.Graph.UtilityContracts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGaeaUtilityContractsTest::RunTest(const FString& Parameters)
{
	RegisterGaeaUtilityNodes();
	const FName Types[]={GaeaTerrainNodeTypes::Accumulator,GaeaTerrainNodeTypes::Chokepoint,GaeaTerrainNodeTypes::Compare,GaeaTerrainNodeTypes::DataExtractor,GaeaTerrainNodeTypes::Gate,GaeaTerrainNodeTypes::Layers,GaeaTerrainNodeTypes::Mask,GaeaTerrainNodeTypes::Math,GaeaTerrainNodeTypes::Mixer,GaeaTerrainNodeTypes::Repeat,GaeaTerrainNodeTypes::Reseed,GaeaTerrainNodeTypes::Route,GaeaTerrainNodeTypes::Seamless,GaeaTerrainNodeTypes::Switch,GaeaTerrainNodeTypes::Var};
	for(FName Type:Types)
	{
		FGaeaTerrainNodeDescriptor D; TestTrue(*FString::Printf(TEXT("%s descriptor exists"),*Type.ToString()),FGaeaTerrainNodeDescriptorRegistry::Get(Type,D));
		TestTrue(*FString::Printf(TEXT("%s evaluator exists"),*Type.ToString()),FGaeaTerrainNodeRegistry::IsRegistered(Type));
		TestEqual(*FString::Printf(TEXT("%s is Utility"),*Type.ToString()),D.Category,FString(TEXT("Utility")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaUtilityInternalParityTest,"CodenameGaea.Core.Graph.UtilityInternalParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGaeaUtilityInternalParityTest::RunTest(const FString& Parameters)
{
	RegisterGaeaUtilityNodes();
	const FGaeaTerrainEvaluationContext Context=MakeContext();
	const FGaeaTerrainEvaluationResult GraphResult=FGaeaTerrainEvaluator::Evaluate(MakeMathRecipe(),Context);
	TestTrue(TEXT("Math graph evaluates"),GraphResult.bSuccess); if(!GraphResult.bSuccess){AddError(GraphResult.Error);return false;}
	const FGaeaScalarField* GraphHeight=GraphResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Height); const FGaeaScalarField* Source=Context.SourceDataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Graph Height exists"),GraphHeight); TestNotNull(TEXT("Source Height exists"),Source); if(!GraphHeight||!Source)return false;
	FGaeaTerrainUtilityMathSettings Settings; Settings.Expression=TEXT("a*0.5"); FGaeaScalarField Direct; FString Error;
	TestTrue(TEXT("Internal Math evaluates"),FGaeaTerrainUtilityOps::EvaluateMath(*Source,nullptr,nullptr,Settings,Direct,&Error)); if(!Error.IsEmpty())AddError(Error);
	for(int32 Y=0;Y<Source->Domain.Dimensions.Y;++Y)for(int32 X=0;X<Source->Domain.Dimensions.X;++X)
		TestEqual(TEXT("Graph Math equals internal Math"),GraphHeight->AtInterior(X,Y),Direct.AtInterior(X,Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaUtilityRoutingTest,"CodenameGaea.Core.Graph.UtilityRouting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGaeaUtilityRoutingTest::RunTest(const FString& Parameters)
{
	RegisterGaeaUtilityNodes(); const FGaeaTerrainEvaluationContext Context=MakeContext();
	for(FName Type:{GaeaTerrainNodeTypes::Chokepoint,GaeaTerrainNodeTypes::Route,GaeaTerrainNodeTypes::Switch,GaeaTerrainNodeTypes::Reseed,GaeaTerrainNodeTypes::Var})
	{
		const FGaeaTerrainEvaluationResult Result=FGaeaTerrainEvaluator::Evaluate(MakeRoutingRecipe(Type),Context);
		TestTrue(*FString::Printf(TEXT("%s routes terrain"),*Type.ToString()),Result.bSuccess); if(!Result.bSuccess)AddError(Result.Error);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaUtilityFieldOpsTest,"CodenameGaea.Core.Graph.UtilityFieldOps",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGaeaUtilityFieldOpsTest::RunTest(const FString& Parameters)
{
	FGaeaScalarField A=MakeUtilityField(TEXT("A")); FGaeaScalarField B=A; for(float& V:B.Values)V=1.0f-V;
	FGaeaScalarField Comparison; FGaeaColorField ComparisonColor; FString Error;
	TestTrue(TEXT("Compare works internally"),FGaeaTerrainUtilityOps::Compare(A,B,0.5f,false,false,Comparison,&ComparisonColor,&Error));
	TestTrue(TEXT("Compare color is valid"),ComparisonColor.IsValid());
	FGaeaScalarField Mask=A; for(float& V:Mask.Values)V=FMath::Clamp(V,0.0f,1.0f); FGaeaScalarField Masked;
	TestTrue(TEXT("Mask works internally"),FGaeaTerrainUtilityOps::ApplyMask(B,A,Mask,Masked,&Error));
	FGaeaScalarField Seamless; TestTrue(TEXT("Seamless works internally"),FGaeaTerrainUtilityOps::MakeSeamless(A,0.2f,0.0f,0.0f,Seamless,&Error));
	FGaeaScalarField Repeated; TestTrue(TEXT("Repeat works internally"),FGaeaTerrainUtilityOps::Repeat(A,3,false,1.0f,Repeated,&Error));
	TestTrue(TEXT("Reseed mutation is deterministic"),FGaeaTerrainUtilityOps::MutateSeed(1337,42)==FGaeaTerrainUtilityOps::MutateSeed(1337,42));
	TestTrue(TEXT("Reseed mutation changes seed"),FGaeaTerrainUtilityOps::MutateSeed(1337,42)!=1337);
	return true;
}

#endif
