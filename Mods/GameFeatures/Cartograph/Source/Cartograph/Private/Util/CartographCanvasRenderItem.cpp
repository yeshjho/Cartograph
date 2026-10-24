#include "Util/CartographCanvasRenderItem.h"

#include "CanvasRender.h"
#include "CartographGameInstanceModule.h"
#include "MeshPassProcessor.h"
#include "RenderGraphEvent.h"


bool FCartographCanvasRenderItem::Render_RenderThread(FCanvasRenderContext& RenderContext, FMeshPassProcessorRenderState& DrawRenderState, const FCanvas* Canvas)
{
	checkSlow(Data);
	bool bDirty = false;

#if !UE_SERVER
	if (Data->BatchedElements.HasPrimsToDraw())
	{
		bDirty = true;

		RenderContext.AddPass(
			RDG_EVENT_NAME("CanvasBatchedElements"),
			[LocalData = Data, DrawRenderState, Canvas](FRHICommandList& RHICmdList)
			{
				// current render target set for the canvas
				const FRenderTarget* CanvasRenderTarget = Canvas->GetRenderTarget();
				float Gamma = 1.0f / CanvasRenderTarget->GetDisplayGamma();

				// draw batched items
				LocalData->BatchedElements.Draw(
					RHICmdList,
					DrawRenderState,
					Canvas->GetFeatureLevel(),
					FBatchedElements::CreateProxySceneView(LocalData->Transform.GetMatrix(), FIntRect(0, 0, CanvasRenderTarget->GetSizeXY().X, CanvasRenderTarget->GetSizeXY().Y)),
					Canvas->IsHitTesting(),
					Gamma
				);
			});
	}

	if (Canvas->GetAllowedModes() & FCanvas::Allow_DeleteOnRender)
	{
		// delete data since we're done rendering it
		RenderContext.DeferredDelete(Data);
		Data = nullptr;
	}
#endif

	return bDirty;
}


bool FCartographCanvasRenderItem::Render_GameThread(const FCanvas* Canvas, FCanvasRenderThreadScope& RenderScope)
{
	checkSlow(Data);
	bool bDirty = false;

#if !UE_SERVER
	if (Data->BatchedElements.HasPrimsToDraw())
	{
		bDirty = true;

		// current render target set for the canvas  (eg. an FSlateTextureRenderTarget2DResource)
		const FRenderTarget* CanvasRenderTarget = Canvas->GetRenderTarget();
		float Gamma = 1.0f / CanvasRenderTarget->GetDisplayGamma(); // GetDisplayGamma typically == 2.2

		// Render the batched elements.
		struct FBatchedDrawParameters
		{
			FRenderData* RenderData;
			uint32 bHitTesting : 1;
			uint32 ViewportSizeX;
			uint32 ViewportSizeY;
			float DisplayGamma;
			uint32 AllowedCanvasModes;
			ERHIFeatureLevel::Type FeatureLevel;
			EShaderPlatform ShaderPlatform;
		};
		// all the parameters needed for rendering
		FBatchedDrawParameters DrawParameters =
		{
			Data,
			static_cast<uint32>(Canvas->IsHitTesting() ? 1 : 0),
			static_cast<uint32>(CanvasRenderTarget->GetSizeXY().X),
			static_cast<uint32>(CanvasRenderTarget->GetSizeXY().Y),
			Gamma,
			Canvas->GetAllowedModes(),
			Canvas->GetFeatureLevel(),
			Canvas->GetShaderPlatform()
		};
		bool IsCartograph = false;
		std::array<uint32, 4> Area = {};
		if (UCartographGameInstanceModule::Instance != nullptr && Canvas == UCartographGameInstanceModule::Instance->CurrentCanvas)
		{
			IsCartograph = true;
			Area = UCartographGameInstanceModule::Instance->ScissorArea;
		}

		RenderScope.AddPass(
			TEXT("CanvasBatchedElements"),
			[DrawParameters, IsCartograph, Area](FRHICommandList& RHICmdList)
			{
				/// The ViewRect doesn't seem to affect rendering in any way, so will use scissor.
				FSceneView SceneView = FBatchedElements::CreateProxySceneView(DrawParameters.RenderData->Transform.GetMatrix(), FIntRect(0, 0, DrawParameters.ViewportSizeX, DrawParameters.ViewportSizeY));

				FMeshPassProcessorRenderState DrawRenderState;

				// disable depth test & writes
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
				DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());

				if (IsCartograph)
				{
					RHICmdList.SetScissorRect(true, Area[0], Area[1], Area[2], Area[3]);
				}
				// draw batched items
				DrawParameters.RenderData->BatchedElements.Draw(
					RHICmdList,
					DrawRenderState,
					DrawParameters.FeatureLevel,
					SceneView,
					DrawParameters.bHitTesting,
					DrawParameters.DisplayGamma);
				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

				if (DrawParameters.AllowedCanvasModes & FCanvas::Allow_DeleteOnRender)
				{
					delete DrawParameters.RenderData;
				}
			});
	}
	if (Canvas->GetAllowedModes() & FCanvas::Allow_DeleteOnRender)
	{
		Data = nullptr;
	}
#endif

	return bDirty;
}
