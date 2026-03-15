#include "MGVertexComputeDemo.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FMGVertexComputeDemoModule"

void FMGVertexComputeDemoModule::StartupModule()
{
	FString ShaderDir = FPaths::Combine(
		IPluginManager::Get().FindPlugin(TEXT("MGVertexComputeDemo"))->GetBaseDir(),
		TEXT("Shaders")
	);

	AddShaderSourceDirectoryMapping(TEXT("/MGVertexComputeDemo"), ShaderDir);
}

void FMGVertexComputeDemoModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMGVertexComputeDemoModule, MGVertexComputeDemo);