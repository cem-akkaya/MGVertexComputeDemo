#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MGVertexComputeComponent.generated.h"

UENUM(BlueprintType)
enum class EMGVertexState : uint8
{
    Initializing,
    Executing,
    Error
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MGVERTEXCOMPUTEDEMO_API UMGVertexComputeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMGVertexComputeComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MGVertexCompute")
    class UTexture2D* TargetTexture;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MGVertexCompute")
    EMGVertexState State = EMGVertexState::Initializing;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    TSharedPtr<class FMGVertexComputeViewExtension, ESPMode::ThreadSafe> ViewExtension;
    FMatrix44f CachedLocalToWorld;

public:
    void Render_RenderThread(class FRDGBuilder& GraphBuilder, const class FSceneView& View, const FMatrix44f& LocalToWorld);
    FMatrix44f GetCachedLocalToWorld() const { return CachedLocalToWorld; }
};
