#include "MGVertexComputeComponent.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneViewExtension.h"
#include "SceneView.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"

DEFINE_LOG_CATEGORY_STATIC(LogMGVertexCompute, Log, All);

class FMGVertexComputeCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMGVertexComputeCS);
    SHADER_USE_PARAMETER_STRUCT(FMGVertexComputeCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutVertices)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, OutUVs)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};

IMPLEMENT_GLOBAL_SHADER(FMGVertexComputeCS, "/MGVertexComputeDemo/MGVertexComputeShader.usf", "MainCS", SF_Compute);

class FMGVertexComputeVS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMGVertexComputeVS);
    SHADER_USE_PARAMETER_STRUCT(FMGVertexComputeVS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InVertices)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, InUVs)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, InSampler)
        SHADER_PARAMETER(FMatrix44f, WorldToClip)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMGVertexComputeVS, "/MGVertexComputeDemo/MGVertexComputeGraphicsShader.usf", "MainVS", SF_Vertex);

class FMGVertexComputePS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMGVertexComputePS);
    SHADER_USE_PARAMETER_STRUCT(FMGVertexComputePS, FGlobalShader);

    using FParameters = FMGVertexComputeVS::FParameters;

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};

IMPLEMENT_GLOBAL_SHADER(FMGVertexComputePS, "/MGVertexComputeDemo/MGVertexComputeGraphicsShader.usf", "MainPS", SF_Pixel);

class FMGVertexComputeViewExtension : public FSceneViewExtensionBase
{
public:
    FMGVertexComputeViewExtension(const FAutoRegister& AutoRegister, class UMGVertexComputeComponent* InComponent)
        : FSceneViewExtensionBase(AutoRegister), Component(InComponent)
    {
        LocalToWorldCaptured = FMatrix44f::Identity;
    }

    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override 
    {
        if (Component.IsValid() && Component->GetOwner())
        {
            LocalToWorldCaptured = FMatrix44f(Component->GetOwner()->GetActorTransform().ToMatrixWithScale());
            UE_LOG(LogMGVertexCompute, Log, TEXT("SetupView: Captured Actor Location: %s"), *Component->GetOwner()->GetActorLocation().ToString());
        }
    }
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override { return true; }

    virtual void PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override
    {
        UE_LOG(LogMGVertexCompute, Log, TEXT("PostRenderView_RenderThread called. State: %d"), Component.IsValid() ? (int)Component->State : -1);
        if (Component.IsValid() && Component->State == EMGVertexState::Executing)
        {
            // Ensure we're rendering in a context that supports debug or game flags, filtering out irrelevant viewports.
            if (InView.Family->EngineShowFlags.Game || InView.Family->EngineShowFlags.Editor)
            {
                if (InView.Family->RenderTarget && InView.Family->RenderTarget->GetRenderTargetTexture())
                {
                    Component->Render_RenderThread(GraphBuilder, InView, LocalToWorldCaptured);
                }
                else
                {
                    UE_LOG(LogMGVertexCompute, Warning, TEXT("PostRenderView_RenderThread: RenderTarget or RHI texture is null."));
                }
            }
        }
        else if (Component.IsValid())
        {
             UE_LOG(LogMGVertexCompute, Warning, TEXT("PostRenderView_RenderThread: Component state is not Executing (State: %d)"), (int)Component->State);
        }
    }

private:
    TWeakObjectPtr<class UMGVertexComputeComponent> Component;
    FMatrix44f LocalToWorldCaptured;
};

UMGVertexComputeComponent::UMGVertexComputeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    CachedLocalToWorld = FMatrix44f::Identity;
}

void UMGVertexComputeComponent::BeginPlay()
{
    Super::BeginPlay();
    State = EMGVertexState::Executing;
    ViewExtension = FSceneViewExtensions::NewExtension<FMGVertexComputeViewExtension>(this);
}

void UMGVertexComputeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    CachedLocalToWorld = FMatrix44f(GetOwner()->GetActorTransform().ToMatrixWithScale());
}

void UMGVertexComputeComponent::Render_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FMatrix44f& LocalToWorld)
{
    UE_LOG(LogMGVertexCompute, Log, TEXT("Render_RenderThread started."));

    const int32 NumVertices = 6;

    // Initialize the Render Graph buffers for vertex and UV data.
    FRDGBufferRef VertexBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), NumVertices), TEXT("MGComputeVertexBuffer"));
    
    FRDGBufferRef UVBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), NumVertices), TEXT("MGComputeUVBuffer"));

    // Clear the buffers before the compute pass starts.
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VertexBuffer), 0);
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(UVBuffer), 0);

    FMGVertexComputeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMGVertexComputeCS::FParameters>();
    PassParameters->OutVertices = GraphBuilder.CreateUAV(VertexBuffer);
    PassParameters->OutUVs = GraphBuilder.CreateUAV(UVBuffer);

    TShaderMapRef<FMGVertexComputeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    // Dispatch the compute shader; thread count should be kept in sync with [numthreads(6,1,1)] in the shader file.
    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("MGVertexComputePass"), ComputeShader, PassParameters, FIntVector(1, 1, 1));
    
    // Set up the unified draw pass parameters.
    FMGVertexComputeVS::FParameters* DrawParameters = GraphBuilder.AllocParameters<FMGVertexComputeVS::FParameters>();
    DrawParameters->InVertices = GraphBuilder.CreateSRV(VertexBuffer);
    DrawParameters->InUVs = GraphBuilder.CreateSRV(UVBuffer);
    
    // Account for TranslatedWorldToClip to maintain high precision in large UE5 scenes.
    FMatrix44f TranslatedWorldToClip = FMatrix44f(View.ViewMatrices.GetTranslatedViewProjectionMatrix());
    FVector3f PreViewTranslation = FVector3f(View.ViewMatrices.GetPreViewTranslation());
    
    // Convert from Model space to TranslatedWorldSpace (Absolute World + PreViewTranslation).
    FMatrix44f TranslatedLocalToWorld = LocalToWorld;
    FVector3f WorldOrigin = FVector3f(LocalToWorld.GetOrigin());
    TranslatedLocalToWorld.SetOrigin(WorldOrigin + PreViewTranslation);

    // Matrix multiplication order for row-major FMatrix corresponds to the VS mul(LocalPosition, WorldToClip).
    DrawParameters->WorldToClip = TranslatedLocalToWorld * TranslatedWorldToClip;
    
    FRHITexture* RTTextureRHI = View.Family->RenderTarget->GetRenderTargetTexture();
    if (!RTTextureRHI)
    {
        return;
    }

    FRDGTextureRef RenderTargetTexture = RegisterExternalTexture(GraphBuilder, RTTextureRHI, TEXT("MGVertexDrawRT"));
    DrawParameters->RenderTargets[0] = FRenderTargetBinding(RenderTargetTexture, ERenderTargetLoadAction::ELoad);

    if (TargetTexture != nullptr && TargetTexture->GetResource() != nullptr)
    {
        FRHITexture* TextureRHI = TargetTexture->GetResource()->GetTextureRHI();
        if (TextureRHI != nullptr)
            DrawParameters->InTexture = RegisterExternalTexture(GraphBuilder, TextureRHI, TEXT("MGVertexInputTexture"));
        else
            DrawParameters->InTexture = RegisterExternalTexture(GraphBuilder, GWhiteTexture->GetTextureRHI(), TEXT("MGWhiteTexture"));
    }
    else
    {
        DrawParameters->InTexture = RegisterExternalTexture(GraphBuilder, GWhiteTexture->GetTextureRHI(), TEXT("MGWhiteTexture"));
    }
    
    DrawParameters->InSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

    TShaderMapRef<FMGVertexComputeVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    TShaderMapRef<FMGVertexComputePS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    GraphBuilder.AddPass(
        RDG_EVENT_NAME("MGVertexDrawPass"),
        DrawParameters,
        ERDGPassFlags::Raster,
        [DrawParameters, VertexShader, PixelShader, ViewRect = View.UnscaledViewRect, RenderTargetTexture](FRHICommandList& RHICmdList)
        {
            RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

            FGraphicsPipelineStateInitializer GraphicsPSOInit;
            RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

            if (RenderTargetTexture && RenderTargetTexture->GetRHI())
            {
                GraphicsPSOInit.RenderTargetFormats[0] = (uint8)RenderTargetTexture->GetRHI()->GetFormat();
                GraphicsPSOInit.RenderTargetFlags[0] = RenderTargetTexture->GetRHI()->GetFlags();
                GraphicsPSOInit.NumSamples = RenderTargetTexture->GetRHI()->GetNumSamples();
            }

            GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero>::GetRHI();
            GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
            GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

            GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
            GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
            GraphicsPSOInit.PrimitiveType = PT_TriangleList;

            SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

            SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *DrawParameters);
            SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *DrawParameters);

            RHICmdList.DrawPrimitive(0, 2, 1);
        });
}
