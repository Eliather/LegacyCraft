#pragma once

#include <windows.h>
#include <stdint.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#define C4J_DX12_FRAMES_IN_FLIGHT 2
#define C4J_DX12_FRAME_SRV_CAPACITY 32768

struct DX12BackendGDrawHost
{
	ID3D12Device *device;
	ID3D12CommandQueue *queue;
	ID3D12GraphicsCommandList *activeCmd;

	D3D12_CPU_DESCRIPTOR_HANDLE activeMainRTV;
	D3D12_CPU_DESCRIPTOR_HANDLE activeMainDSV;

	ID3D12DescriptorHeap *rtvHeap;
	UINT rtvDescSize;
	ID3D12DescriptorHeap *dsvHeap;
	UINT dsvDescSize;

	ID3D12DescriptorHeap *samplerHeap;
	UINT samplerDescSize;

	int width;
	int height;
	uint32_t frameIndex;
	uint64_t frameSeq;

	void (*EnsureFrameRecording)(void);
	void (*ExecuteOneShotUpload)(void (*recordFn)(ID3D12GraphicsCommandList *cmd, void *user), void *user);
	bool (*AllocateFrameSRV)(ID3D12Resource *res, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE *outCpu, D3D12_GPU_DESCRIPTOR_HANDLE *outGpu);
	void *(*AllocFrameUploadBytes)(uint32_t size, uint32_t align, D3D12_GPU_VIRTUAL_ADDRESS *outGpuVA);
	void (*DeferredReleaseIUnknown)(IUnknown *obj);
	ID3D12DescriptorHeap *(*GetActiveSRVHeap)(void);
	D3D12_GPU_DESCRIPTOR_HANDLE (*GetSamplerGPUHandle)(bool linearFilter, bool clampWrap);

	ID3D12Resource *(*GetMCTextureResource)(int textureId, int *outW, int *outH, bool *outLinear, bool *outClamp);
	void (*ApplyMCViewportAndTarget)(void);
	void (*InvalidateWorldPipelineCache)(void);
};

struct GDrawFunctions;

GDrawFunctions *gdraw_DX12_CreateContext(DX12BackendGDrawHost *host, int w, int h);
void            gdraw_DX12_DestroyContext(void);
void            gdraw_DX12_SetRendertargetSize(int w, int h);
void            gdraw_DX12_SetTileOrigin(int xPos, int yPos);
void            gdraw_DX12_NoMoreGDrawThisFrame(void);
void            gdraw_DX12_BeginCustomDraw_4J(void *region, float *mat16);
void            gdraw_DX12_CalculateCustomDraw_4J(void *region, float *mat16);
void            gdraw_DX12_EndCustomDraw(void *region);
void            gdraw_DX12_SetViewport_4J(void);
void           *gdraw_DX12_WrappedTextureCreate(int textureId);
void            gdraw_DX12_WrappedTextureDestroy(void *handle);
