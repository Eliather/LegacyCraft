#include "stdafx.h"
#include "4JLibs/inc/4J_Render.h"
#include "Render/IRenderBackend.h"
#include <wincodec.h>
#include <vector>
#include <string>
#include <stdio.h>
#include <string.h>

// Singleton instance
C4JRender RenderManager;

namespace
{
	static HMODULE s_backendModule = NULL;
	static const C4JRenderBackendAPI *s_api = NULL;
	static int s_requestedBackend = C4J_BACKEND_DX11;
	static bool s_requestedVsync = false;

	static std::wstring GetExeDirectory()
	{
		wchar_t path[MAX_PATH] = {0};
		DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
		if (len == 0 || len >= MAX_PATH) return L"";
		wchar_t *slash = wcsrchr(path, L'\\');
		if (!slash) slash = wcsrchr(path, L'/');
		if (!slash) return L"";
		*(slash + 1) = L'\0';
		return std::wstring(path);
	}

	static const wchar_t *GetBackendDllName(int backendType)
	{
		switch (backendType)
		{
		case C4J_BACKEND_OPENGL: return L"C4J_Render_GL.dll";
		case C4J_BACKEND_VULKAN: return L"C4J_Render_Vulkan.dll";
		case C4J_BACKEND_DX12:   return L"C4J_Render_DX12.dll";
		case C4J_BACKEND_DX11:
		default:                 return L"C4J_Render_DX11.dll";
		}
	}

	static bool TryLoadAndInitBackend(int backendType, HWND hwnd, int width, int height, bool fullscreen, bool vsync)
	{
		const wchar_t *dllName = GetBackendDllName(backendType);
		std::wstring fullPath = GetExeDirectory() + dllName;

		HMODULE mod = LoadLibraryW(fullPath.c_str());
		if (!mod)
		{
			mod = LoadLibraryW(dllName);
		}
		if (!mod)
		{
			fprintf(stderr, "[C4JRender] Could not load %ls (err=%lu)\n", dllName, GetLastError());
			return false;
		}

		PFN_C4JBackend_GetAPI pGetAPI = (PFN_C4JBackend_GetAPI)GetProcAddress(mod, "C4JBackend_GetAPI");
		if (!pGetAPI)
		{
			fprintf(stderr, "[C4JRender] Missing C4JBackend_GetAPI export in %ls\n", dllName);
			FreeLibrary(mod);
			return false;
		}

		const C4JRenderBackendAPI *api = pGetAPI();
		if (!api || api->abiVersion != C4J_RENDER_BACKEND_ABI_VERSION || api->abiSize < sizeof(C4JRenderBackendAPI))
		{
			fprintf(stderr, "[C4JRender] ABI mismatch in %ls\n", dllName);
			FreeLibrary(mod);
			return false;
		}

		if (!api->InitialiseWindow || !api->InitialiseWindow(hwnd, width, height, fullscreen ? 1 : 0, vsync ? 1 : 0))
		{
			fprintf(stderr, "[C4JRender] InitialiseWindow failed for %s\n", api->backendName ? api->backendName : "unknown");
			if (api->Shutdown) api->Shutdown();
			FreeLibrary(mod);
			return false;
		}

		s_backendModule = mod;
		s_api = api;

		C4JBackendInfo info = {};
		if (s_api->GetBackendInfo)
		{
			s_api->GetBackendInfo(&info);
		}
		printf("[C4JRender] Active backend: %s | GPU: %s | %dx%d | VSync=%d\n",
			info.backendName[0] ? info.backendName : (api->backendName ? api->backendName : "Unknown"),
			info.adapterName[0] ? info.adapterName : "Default Adapter",
			width, height, vsync ? 1 : 0);
		fflush(stdout);
		return true;
	}

	static IWICImagingFactory *GetWICFactory()
	{
		static IWICImagingFactory *s_pFactory = NULL;
		if (!s_pFactory)
		{
			CoInitialize(NULL);
			CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_pFactory));
		}
		return s_pFactory;
	}

	static bool WICDecodeFromFile(IWICImagingFactory *pFactory, const wchar_t *wFilename, D3DXIMAGE_INFO *pSrcInfo, int **ppDataOut)
	{
		IWICBitmapDecoder *pDecoder = NULL;
		if (FAILED(pFactory->CreateDecoderFromFilename(wFilename, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder)))
		{
			return false;
		}

		IWICBitmapFrameDecode *pFrame = NULL;
		if (FAILED(pDecoder->GetFrame(0, &pFrame)))
		{
			pDecoder->Release();
			return false;
		}

		UINT width = 0, height = 0;
		pFrame->GetSize(&width, &height);
		if (pSrcInfo)
		{
			pSrcInfo->Width = (int)width;
			pSrcInfo->Height = (int)height;
		}

		IWICFormatConverter *pConverter = NULL;
		if (FAILED(pFactory->CreateFormatConverter(&pConverter)))
		{
			pFrame->Release();
			pDecoder->Release();
			return false;
		}

		HRESULT hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0f, WICBitmapPaletteTypeCustom);
		if (SUCCEEDED(hr) && width > 0 && height > 0)
		{
			UINT stride = width * 4;
			UINT bufferSize = stride * height;
			int *pPixels = new int[width * height];
			hr = pConverter->CopyPixels(NULL, stride, bufferSize, (BYTE *)pPixels);
			if (SUCCEEDED(hr))
			{
				*ppDataOut = pPixels;
			}
			else
			{
				delete[] pPixels;
			}
		}

		pConverter->Release();
		pFrame->Release();
		pDecoder->Release();
		return SUCCEEDED(hr);
	}
}

bool C4JRender::InitialiseWindow(HWND hwnd, int width, int height, bool fullscreen, int preferredBackend, bool vsync)
{
	Shutdown();

	s_requestedBackend = preferredBackend;
	s_requestedVsync = vsync;

	int order[4] = { C4J_BACKEND_DX11, C4J_BACKEND_DX12, C4J_BACKEND_VULKAN, C4J_BACKEND_OPENGL };
	if (preferredBackend == C4J_BACKEND_DX12)
	{
		order[0] = C4J_BACKEND_DX12;
		order[1] = C4J_BACKEND_DX11;
		order[2] = C4J_BACKEND_VULKAN;
		order[3] = C4J_BACKEND_OPENGL;
	}
	else if (preferredBackend == C4J_BACKEND_VULKAN)
	{
		order[0] = C4J_BACKEND_VULKAN;
		order[1] = C4J_BACKEND_DX11;
		order[2] = C4J_BACKEND_DX12;
		order[3] = C4J_BACKEND_OPENGL;
	}
	else if (preferredBackend == C4J_BACKEND_OPENGL)
	{
		order[0] = C4J_BACKEND_OPENGL;
		order[1] = C4J_BACKEND_DX11;
		order[2] = C4J_BACKEND_DX12;
		order[3] = C4J_BACKEND_VULKAN;
	}

	for (int i = 0; i < 4; ++i)
	{
		if (TryLoadAndInitBackend(order[i], hwnd, width, height, fullscreen, vsync))
		{
			return true;
		}
	}
	return false;
}

bool C4JRender::ResizeWindow(int width, int height, bool fullscreen)
{
	if (!s_api || !s_api->ResizeWindow) return false;
	return s_api->ResizeWindow(width, height, fullscreen ? 1 : 0) != 0;
}

void C4JRender::Shutdown()
{
	if (s_api && s_api->Shutdown)
	{
		s_api->Shutdown();
	}
	s_api = NULL;
	if (s_backendModule)
	{
		FreeLibrary(s_backendModule);
		s_backendModule = NULL;
	}
}

int C4JRender::GetBackendType() const
{
	return s_api ? s_api->backendType : s_requestedBackend;
}

const char *C4JRender::GetBackendName() const
{
	if (s_api && s_api->backendName) return s_api->backendName;
	return "Uninitialized";
}

const char *C4JRender::GetAdapterName() const
{
	static char s_adapterBuf[128] = "Unknown";
	if (s_api && s_api->GetBackendInfo)
	{
		C4JBackendInfo info = {};
		s_api->GetBackendInfo(&info);
		if (info.adapterName[0])
		{
			strncpy_s(s_adapterBuf, sizeof(s_adapterBuf), info.adapterName, _TRUNCATE);
		}
	}
	return s_adapterBuf;
}

void C4JRender::SetVsyncEnabled(bool enabled)
{
	s_requestedVsync = enabled;
	if (s_api && s_api->SetVsync) s_api->SetVsync(enabled ? 1 : 0);
}

bool C4JRender::IsVsyncEnabled() const
{
	if (s_api && s_api->GetVsync) return s_api->GetVsync() != 0;
	return s_requestedVsync;
}

void C4JRender::Tick()
{
	if (s_api && s_api->Tick) s_api->Tick();
}

void C4JRender::UpdateGamma(unsigned short usGamma)
{
	if (s_api && s_api->UpdateGamma) s_api->UpdateGamma(usGamma);
}

void C4JRender::MatrixMode(int type)
{
	if (s_api && s_api->MatrixMode) s_api->MatrixMode(type);
}

void C4JRender::MatrixSetIdentity()
{
	if (s_api && s_api->MatrixSetIdentity) s_api->MatrixSetIdentity();
}

void C4JRender::MatrixTranslate(float x, float y, float z)
{
	if (s_api && s_api->MatrixTranslate) s_api->MatrixTranslate(x, y, z);
}

void C4JRender::MatrixRotate(float angle, float x, float y, float z)
{
	if (s_api && s_api->MatrixRotate) s_api->MatrixRotate(angle, x, y, z);
}

void C4JRender::MatrixScale(float x, float y, float z)
{
	if (s_api && s_api->MatrixScale) s_api->MatrixScale(x, y, z);
}

void C4JRender::MatrixPerspective(float fovy, float aspect, float zNear, float zFar)
{
	if (s_api && s_api->MatrixPerspective) s_api->MatrixPerspective(fovy, aspect, zNear, zFar);
}

void C4JRender::MatrixOrthogonal(float left, float right, float bottom, float top, float zNear, float zFar)
{
	if (s_api && s_api->MatrixOrthogonal) s_api->MatrixOrthogonal(left, right, bottom, top, zNear, zFar);
}

void C4JRender::MatrixPop()
{
	if (s_api && s_api->MatrixPop) s_api->MatrixPop();
}

void C4JRender::MatrixPush()
{
	if (s_api && s_api->MatrixPush) s_api->MatrixPush();
}

void C4JRender::MatrixMult(float *mat)
{
	if (s_api && s_api->MatrixMult) s_api->MatrixMult(mat);
}

const float *C4JRender::MatrixGet(int type)
{
	static const float identity[16] = {
		1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
	};
	if (s_api && s_api->MatrixGet) return s_api->MatrixGet(type);
	return identity;
}

void C4JRender::Set_matrixDirty()
{
	if (s_api && s_api->Set_matrixDirty) s_api->Set_matrixDirty();
}

void C4JRender::InitialiseContext() {}

void C4JRender::StartFrame()
{
	if (s_api && s_api->StartFrame) s_api->StartFrame();
}

void C4JRender::DoScreenGrabOnNextPresent()
{
	if (s_api && s_api->DoScreenGrabOnNextPresent) s_api->DoScreenGrabOnNextPresent();
}

void C4JRender::Present()
{
	if (s_api && s_api->Present) s_api->Present();
}

void C4JRender::Clear(int flags, const RECT *pRect)
{
	if (!s_api || !s_api->Clear) return;
	if (pRect)
	{
		C4JClearRect r = { pRect->left, pRect->top, pRect->right, pRect->bottom };
		s_api->Clear(flags, &r);
	}
	else
	{
		s_api->Clear(flags, NULL);
	}
}

void C4JRender::SetClearColour(const float colourRGBA[4])
{
	if (s_api && s_api->SetClearColour) s_api->SetClearColour(colourRGBA);
}

bool C4JRender::IsWidescreen()
{
	if (s_api && s_api->IsWidescreen) return s_api->IsWidescreen() != 0;
	return true;
}

bool C4JRender::IsHiDef()
{
	if (s_api && s_api->IsHiDef) return s_api->IsHiDef() != 0;
	return true;
}

void C4JRender::CaptureThumbnail(ImageFileBuffer *pngOut)
{
	(void)pngOut;
}

static int AllocHeapBufferRGBA(int width, int height, uint8_t **outPixels, void *user)
{
	(void)user;
	if (width <= 0 || height <= 0 || !outPixels) return 0;
	SIZE_T bytes = (SIZE_T)width * (SIZE_T)height * 4u;
	uint8_t *buf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, bytes);
	if (!buf) return 0;
	*outPixels = buf;
	return 1;
}

bool C4JRender::CaptureBackBufferRGBA(BYTE **outRgbaPixels, int *outWidth, int *outHeight)
{
	if (outRgbaPixels) *outRgbaPixels = NULL;
	if (outWidth) *outWidth = 0;
	if (outHeight) *outHeight = 0;
	if (!outRgbaPixels || !outWidth || !outHeight) return false;
	if (!s_api || !s_api->CaptureBackBufferRGBA) return false;

	uint8_t *captured = NULL;
	struct CaptureCtx { uint8_t **ptr; } ctx = { &captured };
	auto allocCb = [](int w, int h, uint8_t **outPtr, void *u) -> int {
		if (!AllocHeapBufferRGBA(w, h, outPtr, u)) return 0;
		CaptureCtx *c = (CaptureCtx *)u;
		if (c && c->ptr) *c->ptr = *outPtr;
		return 1;
	};

	int w = 0, h = 0;
	if (!s_api->CaptureBackBufferRGBA(allocCb, &ctx, &w, &h) || !captured || w <= 0 || h <= 0)
	{
		if (captured) HeapFree(GetProcessHeap(), 0, captured);
		return false;
	}

	*outRgbaPixels = captured;
	*outWidth = w;
	*outHeight = h;
	return true;
}

void C4JRender::ReleaseCapturedBackBuffer(BYTE *rgbaPixels)
{
	if (rgbaPixels)
	{
		HeapFree(GetProcessHeap(), 0, rgbaPixels);
	}
}

void C4JRender::CaptureScreen(ImageFileBuffer *jpgOut, XSOCIAL_PREVIEWIMAGE *previewOut)
{
	(void)jpgOut;
	if (!previewOut) return;
	ZeroMemory(previewOut, sizeof(*previewOut));

	BYTE *rgba = NULL;
	int w = 0, h = 0;
	if (!CaptureBackBufferRGBA(&rgba, &w, &h) || !rgba || w <= 0 || h <= 0)
	{
		return;
	}

	const DWORD pitch = (DWORD)w * 4u;
	BYTE *bgra = (BYTE *)malloc((size_t)pitch * (size_t)h);
	if (!bgra)
	{
		ReleaseCapturedBackBuffer(rgba);
		return;
	}

	const size_t pixelCount = (size_t)w * (size_t)h;
	for (size_t i = 0; i < pixelCount; ++i)
	{
		bgra[(i * 4) + 0] = rgba[(i * 4) + 2];
		bgra[(i * 4) + 1] = rgba[(i * 4) + 1];
		bgra[(i * 4) + 2] = rgba[(i * 4) + 0];
		bgra[(i * 4) + 3] = 0xFF;
	}
	ReleaseCapturedBackBuffer(rgba);

	previewOut->pBytes = bgra;
	previewOut->Pitch = pitch;
	previewOut->Width = (DWORD)w;
	previewOut->Height = (DWORD)h;
}

void C4JRender::BeginConditionalSurvey(int identifier)
{
	if (s_api && s_api->BeginConditionalSurvey) s_api->BeginConditionalSurvey(identifier);
}

void C4JRender::EndConditionalSurvey()
{
	if (s_api && s_api->EndConditionalSurvey) s_api->EndConditionalSurvey();
}

void C4JRender::BeginConditionalRendering(int identifier)
{
	if (s_api && s_api->BeginConditionalRendering) s_api->BeginConditionalRendering(identifier);
}

void C4JRender::EndConditionalRendering()
{
	if (s_api && s_api->EndConditionalRendering) s_api->EndConditionalRendering();
}

void C4JRender::DrawVertices(ePrimitiveType PrimitiveType, int count, void *dataIn, eVertexType vType, C4JRender::ePixelShaderType psType)
{
	if (s_api && s_api->DrawVertices) s_api->DrawVertices((int)PrimitiveType, count, dataIn, (int)vType, (int)psType);
}

void C4JRender::DrawVertexBuffer(ePrimitiveType PrimitiveType, int count, void *buffer, C4JRender::eVertexType vType, C4JRender::ePixelShaderType psType)
{
	if (s_api && s_api->DrawVertexBuffer) s_api->DrawVertexBuffer((int)PrimitiveType, count, buffer, (int)vType, (int)psType);
}

void C4JRender::CBuffLockStaticCreations()
{
	if (s_api && s_api->CBuffLockStaticCreations) s_api->CBuffLockStaticCreations();
}

int C4JRender::CBuffCreate(int count)
{
	return (s_api && s_api->CBuffCreate) ? s_api->CBuffCreate(count) : 0;
}

void C4JRender::CBuffDelete(int first, int count)
{
	if (s_api && s_api->CBuffDelete) s_api->CBuffDelete(first, count);
}

void C4JRender::CBuffStart(int index, bool full)
{
	if (s_api && s_api->CBuffStart) s_api->CBuffStart(index, full ? 1 : 0);
}

void C4JRender::CBuffClear(int index)
{
	if (s_api && s_api->CBuffClear) s_api->CBuffClear(index);
}

int C4JRender::CBuffSize(int index)
{
	return (s_api && s_api->CBuffSize) ? s_api->CBuffSize(index) : 0;
}

void C4JRender::CBuffEnd()
{
	if (s_api && s_api->CBuffEnd) s_api->CBuffEnd();
}

bool C4JRender::CBuffCall(int index, bool full)
{
	return (s_api && s_api->CBuffCall) ? (s_api->CBuffCall(index, full ? 1 : 0) != 0) : false;
}

void C4JRender::CBuffTick()
{
	if (s_api && s_api->CBuffTick) s_api->CBuffTick();
}

void C4JRender::CBuffDeferredModeStart()
{
	if (s_api && s_api->CBuffDeferredModeStart) s_api->CBuffDeferredModeStart();
}

void C4JRender::CBuffDeferredModeEnd()
{
	if (s_api && s_api->CBuffDeferredModeEnd) s_api->CBuffDeferredModeEnd();
}

int C4JRender::TextureCreate()
{
	return (s_api && s_api->TextureCreate) ? s_api->TextureCreate() : 0;
}

void C4JRender::TextureFree(int idx)
{
	if (s_api && s_api->TextureFree) s_api->TextureFree(idx);
}

void C4JRender::TextureBind(int idx)
{
	if (s_api && s_api->TextureBind) s_api->TextureBind(idx);
}

void C4JRender::TextureBindVertex(int idx)
{
	if (s_api && s_api->TextureBindVertex) s_api->TextureBindVertex(idx);
}

void C4JRender::TextureSetTextureLevels(int levels)
{
	if (s_api && s_api->TextureSetTextureLevels) s_api->TextureSetTextureLevels(levels);
}

int C4JRender::TextureGetTextureLevels()
{
	return (s_api && s_api->TextureGetTextureLevels) ? s_api->TextureGetTextureLevels() : 1;
}

void C4JRender::TextureData(int width, int height, void *data, int level, eTextureFormat format)
{
	if (s_api && s_api->TextureData) s_api->TextureData(width, height, data, level, (int)format);
}

void C4JRender::TextureDataUpdate(int xoffset, int yoffset, int width, int height, void *data, int level)
{
	if (s_api && s_api->TextureDataUpdate) s_api->TextureDataUpdate(xoffset, yoffset, width, height, data, level);
}

void C4JRender::TextureSetParam(int param, int value)
{
	if (s_api && s_api->TextureSetParam) s_api->TextureSetParam(param, value);
}

void C4JRender::TextureDynamicUpdateStart()
{
	if (s_api && s_api->TextureDynamicUpdateStart) s_api->TextureDynamicUpdateStart();
}

void C4JRender::TextureDynamicUpdateEnd()
{
	if (s_api && s_api->TextureDynamicUpdateEnd) s_api->TextureDynamicUpdateEnd();
}

HRESULT C4JRender::LoadTextureData(const char *szFilename, D3DXIMAGE_INFO *pSrcInfo, int **ppDataOut)
{
	if (!szFilename || !ppDataOut) return E_POINTER;
	*ppDataOut = NULL;

	IWICImagingFactory *pFactory = GetWICFactory();
	if (!pFactory) return E_FAIL;

	int wlen = MultiByteToWideChar(CP_UTF8, 0, szFilename, -1, NULL, 0);
	if (wlen <= 0) return E_FAIL;
	std::vector<wchar_t> wFilename(wlen);
	MultiByteToWideChar(CP_UTF8, 0, szFilename, -1, wFilename.data(), wlen);

	if (WICDecodeFromFile(pFactory, wFilename.data(), pSrcInfo, ppDataOut))
	{
		return ERROR_SUCCESS;
	}

	std::wstring sPath(wFilename.data());
	std::wstring sAlt;
	size_t pos = sPath.find(L"\\res\\");
	if (pos != std::wstring::npos)
	{
		sAlt = sPath.substr(0, pos) + L"\\Media\\" + sPath.substr(pos + 5);
	}
	else if ((pos = sPath.find(L"/res/")) != std::wstring::npos)
	{
		sAlt = sPath.substr(0, pos) + L"/Media/" + sPath.substr(pos + 5);
	}

	if (!sAlt.empty() && WICDecodeFromFile(pFactory, sAlt.c_str(), pSrcInfo, ppDataOut))
	{
		return ERROR_SUCCESS;
	}

	pos = sPath.find(L"Graphics");
	if (pos != std::wstring::npos)
	{
		sAlt = L"Common\\Media\\" + sPath.substr(pos);
		if (WICDecodeFromFile(pFactory, sAlt.c_str(), pSrcInfo, ppDataOut))
		{
			return ERROR_SUCCESS;
		}
	}

	return E_FAIL;
}

HRESULT C4JRender::LoadTextureData(BYTE *pbData, DWORD dwBytes, D3DXIMAGE_INFO *pSrcInfo, int **ppDataOut)
{
	if (!pbData || dwBytes == 0 || !ppDataOut) return E_POINTER;
	*ppDataOut = NULL;

	IWICImagingFactory *pFactory = GetWICFactory();
	if (!pFactory) return E_FAIL;

	IWICStream *pStream = NULL;
	if (FAILED(pFactory->CreateStream(&pStream))) return E_FAIL;

	if (FAILED(pStream->InitializeFromMemory(pbData, dwBytes)))
	{
		pStream->Release();
		return E_FAIL;
	}

	IWICBitmapDecoder *pDecoder = NULL;
	if (FAILED(pFactory->CreateDecoderFromStream(pStream, NULL, WICDecodeMetadataCacheOnDemand, &pDecoder)))
	{
		pStream->Release();
		return E_FAIL;
	}

	IWICBitmapFrameDecode *pFrame = NULL;
	if (FAILED(pDecoder->GetFrame(0, &pFrame)))
	{
		pDecoder->Release();
		pStream->Release();
		return E_FAIL;
	}

	UINT width = 0, height = 0;
	pFrame->GetSize(&width, &height);
	if (pSrcInfo)
	{
		pSrcInfo->Width = (int)width;
		pSrcInfo->Height = (int)height;
	}

	IWICFormatConverter *pConverter = NULL;
	if (FAILED(pFactory->CreateFormatConverter(&pConverter)))
	{
		pFrame->Release();
		pDecoder->Release();
		pStream->Release();
		return E_FAIL;
	}

	HRESULT hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0f, WICBitmapPaletteTypeCustom);
	if (SUCCEEDED(hr) && width > 0 && height > 0)
	{
		UINT stride = width * 4;
		UINT bufferSize = stride * height;
		int *pPixels = new int[width * height];
		hr = pConverter->CopyPixels(NULL, stride, bufferSize, (BYTE *)pPixels);
		if (SUCCEEDED(hr))
		{
			*ppDataOut = pPixels;
		}
		else
		{
			delete[] pPixels;
		}
	}

	pConverter->Release();
	pFrame->Release();
	pDecoder->Release();
	pStream->Release();
	return SUCCEEDED(hr) ? ERROR_SUCCESS : E_FAIL;
}

HRESULT C4JRender::SaveTextureData(const char *szFilename, D3DXIMAGE_INFO *pSrcInfo, int *ppDataOut)
{
	(void)szFilename; (void)pSrcInfo; (void)ppDataOut;
	return S_OK;
}

HRESULT C4JRender::SaveTextureDataToMemory(void *pOutput, int outputCapacity, int *outputLength, int width, int height, int *ppDataIn)
{
	(void)pOutput; (void)outputCapacity; (void)outputLength; (void)width; (void)height; (void)ppDataIn;
	return S_OK;
}

void C4JRender::TextureGetStats()
{
}

void *C4JRender::TextureGetTexture(int idx)
{
	return (s_api && s_api->TextureGetNativeHandle) ? s_api->TextureGetNativeHandle(idx) : NULL;
}

void C4JRender::StateSetColour(float r, float g, float b, float a) { if (s_api && s_api->StateSetColour) s_api->StateSetColour(r, g, b, a); }
void C4JRender::StateSetDepthMask(bool enable) { if (s_api && s_api->StateSetDepthMask) s_api->StateSetDepthMask(enable ? 1 : 0); }
void C4JRender::StateSetBlendEnable(bool enable) { if (s_api && s_api->StateSetBlendEnable) s_api->StateSetBlendEnable(enable ? 1 : 0); }
void C4JRender::StateSetBlendFunc(int src, int dst) { if (s_api && s_api->StateSetBlendFunc) s_api->StateSetBlendFunc(src, dst); }
void C4JRender::StateSetBlendFactor(unsigned int colour) { if (s_api && s_api->StateSetBlendFactor) s_api->StateSetBlendFactor(colour); }
void C4JRender::StateSetAlphaFunc(int func, float param) { if (s_api && s_api->StateSetAlphaFunc) s_api->StateSetAlphaFunc(func, param); }
void C4JRender::StateSetDepthFunc(int func) { if (s_api && s_api->StateSetDepthFunc) s_api->StateSetDepthFunc(func); }
void C4JRender::StateSetFaceCull(bool enable) { if (s_api && s_api->StateSetFaceCull) s_api->StateSetFaceCull(enable ? 1 : 0); }
void C4JRender::StateSetFaceCullCW(bool enable) { if (s_api && s_api->StateSetFaceCullCW) s_api->StateSetFaceCullCW(enable ? 1 : 0); }
void C4JRender::StateSetLineWidth(float width) { if (s_api && s_api->StateSetLineWidth) s_api->StateSetLineWidth(width); }
void C4JRender::StateSetWriteEnable(bool red, bool green, bool blue, bool alpha) { if (s_api && s_api->StateSetWriteEnable) s_api->StateSetWriteEnable(red ? 1 : 0, green ? 1 : 0, blue ? 1 : 0, alpha ? 1 : 0); }
void C4JRender::StateSetDepthTestEnable(bool enable) { if (s_api && s_api->StateSetDepthTestEnable) s_api->StateSetDepthTestEnable(enable ? 1 : 0); }
void C4JRender::StateSetAlphaTestEnable(bool enable) { if (s_api && s_api->StateSetAlphaTestEnable) s_api->StateSetAlphaTestEnable(enable ? 1 : 0); }
void C4JRender::StateSetDepthSlopeAndBias(float slope, float bias) { if (s_api && s_api->StateSetDepthSlopeAndBias) s_api->StateSetDepthSlopeAndBias(slope, bias); }
void C4JRender::StateSetFogEnable(bool enable) { if (s_api && s_api->StateSetFogEnable) s_api->StateSetFogEnable(enable ? 1 : 0); }
void C4JRender::StateSetFogMode(int mode) { if (s_api && s_api->StateSetFogMode) s_api->StateSetFogMode(mode); }
void C4JRender::StateSetFogNearDistance(float dist) { if (s_api && s_api->StateSetFogNearDistance) s_api->StateSetFogNearDistance(dist); }
void C4JRender::StateSetFogFarDistance(float dist) { if (s_api && s_api->StateSetFogFarDistance) s_api->StateSetFogFarDistance(dist); }
void C4JRender::StateSetFogDensity(float density) { if (s_api && s_api->StateSetFogDensity) s_api->StateSetFogDensity(density); }
void C4JRender::StateSetFogColour(float red, float green, float blue) { if (s_api && s_api->StateSetFogColour) s_api->StateSetFogColour(red, green, blue); }
void C4JRender::StateSetLightingEnable(bool enable) { if (s_api && s_api->StateSetLightingEnable) s_api->StateSetLightingEnable(enable ? 1 : 0); }
void C4JRender::StateSetVertexTextureUV(float u, float v) { if (s_api && s_api->StateSetVertexTextureUV) s_api->StateSetVertexTextureUV(u, v); }
void C4JRender::StateSetLightColour(int light, float red, float green, float blue) { if (s_api && s_api->StateSetLightColour) s_api->StateSetLightColour(light, red, green, blue); }
void C4JRender::StateSetLightAmbientColour(float red, float green, float blue) { if (s_api && s_api->StateSetLightAmbientColour) s_api->StateSetLightAmbientColour(red, green, blue); }
void C4JRender::StateSetLightDirection(int light, float x, float y, float z) { if (s_api && s_api->StateSetLightDirection) s_api->StateSetLightDirection(light, x, y, z); }
void C4JRender::StateSetLightEnable(int light, bool enable) { if (s_api && s_api->StateSetLightEnable) s_api->StateSetLightEnable(light, enable ? 1 : 0); }
void C4JRender::StateSetViewport(eViewportType viewportType) { if (s_api && s_api->StateSetViewport) s_api->StateSetViewport((int)viewportType); }
void C4JRender::StateSetEnableViewportClipPlanes(bool enable) { if (s_api && s_api->StateSetEnableViewportClipPlanes) s_api->StateSetEnableViewportClipPlanes(enable ? 1 : 0); }
void C4JRender::StateSetTexGenCol(int col, float x, float y, float z, float w, bool eyeSpace) { if (s_api && s_api->StateSetTexGenCol) s_api->StateSetTexGenCol(col, x, y, z, w, eyeSpace ? 1 : 0); }
void C4JRender::StateSetStencil(int Function, uint8_t stencil_ref, uint8_t stencil_func_mask, uint8_t stencil_write_mask) { if (s_api && s_api->StateSetStencil) s_api->StateSetStencil(Function, stencil_ref, stencil_func_mask, stencil_write_mask); }
void C4JRender::StateSetForceLOD(int LOD) { if (s_api && s_api->StateSetForceLOD) s_api->StateSetForceLOD(LOD); }
void C4JRender::BeginEvent(LPCWSTR eventName) { if (s_api && s_api->BeginEvent) s_api->BeginEvent(eventName); }
void C4JRender::EndEvent() { if (s_api && s_api->EndEvent) s_api->EndEvent(); }
void C4JRender::Suspend() { if (s_api && s_api->Suspend) s_api->Suspend(); }
bool C4JRender::Suspended() { return (s_api && s_api->Suspended) ? (s_api->Suspended() != 0) : false; }
void C4JRender::Resume() { if (s_api && s_api->Resume) s_api->Resume(); }

void *C4JRender::GDrawCreateContext(int w, int h) { return (s_api && s_api->GDraw_CreateContext) ? s_api->GDraw_CreateContext(w, h) : NULL; }
void  C4JRender::GDrawDestroyContext() { if (s_api && s_api->GDraw_DestroyContext) s_api->GDraw_DestroyContext(); }
void  C4JRender::GDrawSetRendertargetSize(int w, int h) { if (s_api && s_api->GDraw_SetRendertargetSize) s_api->GDraw_SetRendertargetSize(w, h); }
void  C4JRender::GDrawSetTileOrigin(int xPos, int yPos) { if (s_api && s_api->GDraw_SetTileOrigin) s_api->GDraw_SetTileOrigin(xPos, yPos); }
void  C4JRender::GDrawNoMoreGDrawThisFrame() { if (s_api && s_api->GDraw_NoMoreGDrawThisFrame) s_api->GDraw_NoMoreGDrawThisFrame(); }
void  C4JRender::GDrawBeginCustomDraw4J(void *region, float *mat16) { if (s_api && s_api->GDraw_BeginCustomDraw_4J) s_api->GDraw_BeginCustomDraw_4J(region, mat16); }
void  C4JRender::GDrawCalculateCustomDraw4J(void *region, float *mat16) { if (s_api && s_api->GDraw_CalculateCustomDraw_4J) s_api->GDraw_CalculateCustomDraw_4J(region, mat16); }
void  C4JRender::GDrawEndCustomDraw(void *region) { if (s_api && s_api->GDraw_EndCustomDraw) s_api->GDraw_EndCustomDraw(region); }
void  C4JRender::GDrawSetViewport4J() { if (s_api && s_api->GDraw_SetViewport_4J) s_api->GDraw_SetViewport_4J(); }
void *C4JRender::GDrawWrappedTextureCreate(int textureId) { return (s_api && s_api->GDraw_WrappedTextureCreate) ? s_api->GDraw_WrappedTextureCreate(textureId) : NULL; }
void  C4JRender::GDrawWrappedTextureDestroy(void *handle) { if (s_api && s_api->GDraw_WrappedTextureDestroy) s_api->GDraw_WrappedTextureDestroy(handle); }
