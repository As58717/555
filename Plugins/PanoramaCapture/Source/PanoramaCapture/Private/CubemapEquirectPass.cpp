#include "CubemapEquirectPass.h"

#include "GlobalShader.h"
#include "PanoramaCaptureModule.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterUtils.h"
#include "ShaderCompilerCore.h"
#include "ComputeShaderUtils.h"

class FCubemapToEquirectCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCubemapToEquirectCS);
    SHADER_USE_PARAMETER_STRUCT(FCubemapToEquirectCS, FGlobalShader);

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return Parameters.Platform == SP_PCD3D_SM5 || Parameters.Platform == SP_METAL_SM5 || Parameters.Platform == SP_VULKAN_SM5;
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("PANORAMA_LINEAR_GAMMA"), 1);
    }

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FVector2f, OutputResolution)
        SHADER_PARAMETER(uint32, bStereo)
        SHADER_PARAMETER(uint32, bLinear)
        SHADER_PARAMETER(uint32, bStereoOverUnder)
        SHADER_PARAMETER_RDG_TEXTURE(TextureCube, SourceTextureLeft)
        SHADER_PARAMETER_RDG_TEXTURE(TextureCube, SourceTextureRight)
        SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FCubemapToEquirectCS, "/PanoramaCapture/Private/CubemapToEquirect.usf", "MainCS", SF_Compute);

void FCubemapEquirectPass::AddComputePass(FRDGBuilder& GraphBuilder, const FCubemapEquirectDispatchParams& Params)
{
    if (!Params.SourceCubemapLeft || !Params.DestinationEquirect)
    {
        return;
    }

    FCubemapToEquirectCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCubemapToEquirectCS::FParameters>();
    PassParameters->SourceTextureLeft = Params.SourceCubemapLeft;
    PassParameters->SourceTextureRight = Params.SourceCubemapRight ? Params.SourceCubemapRight : Params.SourceCubemapLeft;
    PassParameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    PassParameters->OutputTexture = GraphBuilder.CreateUAV(Params.DestinationEquirect);
    PassParameters->OutputResolution = FVector2f(Params.OutputResolution);
    PassParameters->bStereo = Params.bStereo ? 1u : 0u;
    PassParameters->bLinear = Params.bLinearGamma ? 1u : 0u;
    PassParameters->bStereoOverUnder = Params.bStereoOverUnder ? 1u : 0u;

    TShaderMapRef<FCubemapToEquirectCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    const FIntVector GroupCount(
        FMath::DivideAndRoundUp(Params.OutputResolution.X, 8),
        FMath::DivideAndRoundUp(Params.OutputResolution.Y, 8),
        1);

    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Panorama::CubemapToEquirect"), ComputeShader, PassParameters, GroupCount);
}
