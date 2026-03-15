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
        SHADER_PARAMETER(FMatrix44f, WorldToClip)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};

IMPLEMENT_GLOBAL_SHADER(FMGVertexComputeVS, "/MGVertexComputeDemo/MGVertexComputeGraphicsShader.usf", "MainVS", SF_Vertex);

class FMGVertexComputePS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMGVertexComputePS);
    SHADER_USE_PARAMETER_STRUCT(FMGVertexComputePS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InVertices)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, InUVs)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, InSampler)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()

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
            // Problem 6: Frustum culling / View filtering
            // Only render in Game views or Editor views that have show flags for Game
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

    // 1. Create RDG Buffers
    FRDGBufferRef VertexBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), NumVertices), TEXT("MGComputeVertexBuffer"));
    
    FRDGBufferRef UVBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), NumVertices), TEXT("MGComputeUVBuffer"));

    // 2. Compute Pass
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VertexBuffer), 0);
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(UVBuffer), 0);

    FMGVertexComputeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMGVertexComputeCS::FParameters>();
    PassParameters->OutVertices = GraphBuilder.CreateUAV(VertexBuffer);
    PassParameters->OutUVs = GraphBuilder.CreateUAV(UVBuffer);

    TShaderMapRef<FMGVertexComputeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    // Problem 4: Thread count should match shader [numthreads(6,1,1)]
    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("MGVertexComputePass"), ComputeShader, PassParameters, FIntVector(1, 1, 1));
    
    // 3. Draw Pass Setup
    FMGVertexComputeVS::FParameters* VSParameters = GraphBuilder.AllocParameters<FMGVertexComputeVS::FParameters>();
    VSParameters->InVertices = GraphBuilder.CreateSRV(VertexBuffer);
    VSParameters->InUVs = GraphBuilder.CreateSRV(UVBuffer);
    
    // Use TranslatedWorldToClip for better precision in UE5
    FMatrix44f TranslatedWorldToClip = FMatrix44f(View.ViewMatrices.GetTranslatedViewProjectionMatrix());
    FVector3f PreViewTranslation = FVector3f(View.ViewMatrices.GetPreViewTranslation());
    
    // Model space -> TranslatedWorldSpace (Absolute World + PreViewTranslation)
    // LocalToWorld already contains the actor's absolute world position.
    FMatrix44f TranslatedLocalToWorld = LocalToWorld;
    FVector3f WorldOrigin = FVector3f(LocalToWorld.GetOrigin());
    TranslatedLocalToWorld.SetOrigin(WorldOrigin + PreViewTranslation);

    // Matrix multiplication order for FMatrix (row-major) and VS (mul(LocalPosition, WorldToClip)):
    // LocalPosition * (TranslatedLocalToWorld * TranslatedWorldToClip)
    VSParameters->WorldToClip = TranslatedLocalToWorld * TranslatedWorldToClip;
    
    // CPU-side check for NDC coordinates of 4 corners (10m x 10m wall in YZ plane)
    FVector4f Corners[4] = {
        FVector4f(0, -500, -500, 1),
        FVector4f(0,  500, -500, 1),
        FVector4f(0, -500,  500, 1),
        FVector4f(0,  500,  500, 1)
    };

    for (int i = 0; i < 4; ++i)
    {
        FVector4f ClipPos = VSParameters->WorldToClip.TransformFVector4(Corners[i]);
        FVector3f NDC = FVector3f(ClipPos.X / ClipPos.W, ClipPos.Y / ClipPos.W, ClipPos.Z / ClipPos.W);
        UE_LOG(LogMGVertexCompute, Log, TEXT("Corner %d NDC: %s (W: %f)"), i, *NDC.ToString(), ClipPos.W);
    }
    
    UE_LOG(LogMGVertexCompute, Log, TEXT("TranslatedLocalToWorld Matrix: %s"), *TranslatedLocalToWorld.ToString());
    UE_LOG(LogMGVertexCompute, Log, TEXT("TranslatedWorldToClip Matrix: %s"), *TranslatedWorldToClip.ToString());
    UE_LOG(LogMGVertexCompute, Log, TEXT("PreViewTranslation: %s"), *PreViewTranslation.ToString());

    FMGVertexComputePS::FParameters* PSParameters = GraphBuilder.AllocParameters<FMGVertexComputePS::FParameters>();
    PSParameters->InVertices = GraphBuilder.CreateSRV(VertexBuffer);
    PSParameters->InUVs = GraphBuilder.CreateSRV(UVBuffer);
    
    FRHITexture* RTTextureRHI = View.Family->RenderTarget->GetRenderTargetTexture();
    if (!RTTextureRHI)
    {
        return;
    }

    FRDGTextureRef RenderTargetTexture = RegisterExternalTexture(GraphBuilder, RTTextureRHI, TEXT("MGVertexDrawRT"));
    PSParameters->RenderTargets[0] = FRenderTargetBinding(RenderTargetTexture, ERenderTargetLoadAction::ELoad);

    if (TargetTexture != nullptr && TargetTexture->GetResource() != nullptr)
    {
        FRHITexture* TextureRHI = TargetTexture->GetResource()->GetTextureRHI();
        if (TextureRHI != nullptr)
            PSParameters->InTexture = RegisterExternalTexture(GraphBuilder, TextureRHI, TEXT("MGVertexInputTexture"));
        else
            PSParameters->InTexture = RegisterExternalTexture(GraphBuilder, GWhiteTexture->GetTextureRHI(), TEXT("MGWhiteTexture"));
    }
    else
    {
        PSParameters->InTexture = RegisterExternalTexture(GraphBuilder, GWhiteTexture->GetTextureRHI(), TEXT("MGWhiteTexture"));
    }
    
    PSParameters->InSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

    TShaderMapRef<FMGVertexComputeVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    TShaderMapRef<FMGVertexComputePS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    GraphBuilder.AddPass(
        RDG_EVENT_NAME("MGVertexDrawPass"),
        PSParameters,
        ERDGPassFlags::Raster,
        [VSParameters, PSParameters, VertexShader, PixelShader, ViewRect = View.UnscaledViewRect, RenderTargetTexture](FRHICommandList& RHICmdList)
        {
            UE_LOG(LogMGVertexCompute, Log, TEXT("MGVertexDrawPass executing on RenderThread. ViewRect: (%d, %d) to (%d, %d). RT: %s"), 
                ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y, RenderTargetTexture->Name);
            
            RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

            FGraphicsPipelineStateInitializer GraphicsPSOInit;
            RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

            if (RenderTargetTexture && RenderTargetTexture->GetRHI())
            {
                GraphicsPSOInit.RenderTargetFormats[0] = (uint8)RenderTargetTexture->GetRHI()->GetFormat();
                GraphicsPSOInit.RenderTargetFlags[0] = RenderTargetTexture->GetRHI()->GetFlags();
                GraphicsPSOInit.NumSamples = RenderTargetTexture->GetRHI()->GetNumSamples();
                UE_LOG(LogMGVertexCompute, Log, TEXT("RT Format: %d, Samples: %d"), (int)GraphicsPSOInit.RenderTargetFormats[0], (int)GraphicsPSOInit.NumSamples);
            }
            else
            {
                UE_LOG(LogMGVertexCompute, Error, TEXT("RT RHI is null!"));
            }

            GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero>::GetRHI(); // Opaque-ish Additive for visibility
            GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
            GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

            GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
            GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
            GraphicsPSOInit.PrimitiveType = PT_TriangleList;

            SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

            SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *VSParameters);
            SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PSParameters);

            RHICmdList.DrawPrimitive(0, 2, 1);
        });
}
