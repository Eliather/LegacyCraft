#include "stdafx.h"
#include "..\Common\Consoles_App.h"
#include "..\User.h"
#include "..\..\Minecraft.Client\Minecraft.h"
#include "..\..\Minecraft.Client\MinecraftServer.h"
#include "..\..\Minecraft.Client\PlayerList.h"
#include "..\..\Minecraft.Client\ServerPlayer.h"
#include "..\..\Minecraft.World\Level.h"
#include "..\..\Minecraft.World\LevelSettings.h"
#include "..\..\Minecraft.World\BiomeSource.h"
#include "..\..\Minecraft.World\LevelType.h"
#include "..\Common\zlib\zlib.h"

extern ID3D11Device*           g_pd3dDevice;
extern ID3D11DeviceContext*    g_pImmediateContext;
extern IDXGISwapChain*         g_pSwapChain;

namespace
{
#ifdef _WINDOWS64
	const int WINDOWS64_SAVE_THUMBNAIL_CAPTURE_MAX_RETRIES = 6;
	const DWORD WINDOWS64_SAVE_THUMBNAIL_DIMENSION = 64;
#endif

	static const BYTE WINDOWS64_PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

	void AppendBigEndianUInt32(std::vector<BYTE> &buffer, DWORD value)
	{
		buffer.push_back((BYTE)((value >> 24) & 0xFF));
		buffer.push_back((BYTE)((value >> 16) & 0xFF));
		buffer.push_back((BYTE)((value >> 8) & 0xFF));
		buffer.push_back((BYTE)(value & 0xFF));
	}

	void AppendPngChunk(std::vector<BYTE> &pngData, const char chunkType[4], const BYTE *chunkData, DWORD chunkSize)
	{
		AppendBigEndianUInt32(pngData, chunkSize);
		pngData.insert(pngData.end(), chunkType, chunkType + 4);
		if(chunkData != NULL && chunkSize > 0)
		{
			pngData.insert(pngData.end(), chunkData, chunkData + chunkSize);
		}

		uLong crcValue = crc32(0L, Z_NULL, 0);
		crcValue = crc32(crcValue, (const Bytef *)chunkType, 4);
		if(chunkData != NULL && chunkSize > 0)
		{
			crcValue = crc32(crcValue, (const Bytef *)chunkData, chunkSize);
		}
		AppendBigEndianUInt32(pngData, (DWORD)crcValue);
	}

	bool EncodeRgbaPixelsAsPng(const BYTE *rgbaPixels, DWORD width, DWORD height, DWORD rowBytes, std::vector<BYTE> &pngData)
	{
		pngData.clear();
		if(rgbaPixels == NULL || width == 0 || height == 0 || rowBytes < (width * 4))
		{
			return false;
		}

		const size_t filteredRowBytes = (size_t)(width * 4) + 1;
		std::vector<BYTE> filteredPixels(filteredRowBytes * height);
		for(DWORD y = 0; y < height; ++y)
		{
			BYTE *destRow = &filteredPixels[filteredRowBytes * y];
			destRow[0] = 0;
			memcpy(destRow + 1, rgbaPixels + (rowBytes * y), width * 4);
		}

		uLongf compressedSize = compressBound((uLong)filteredPixels.size());
		std::vector<BYTE> compressedPixels((size_t)compressedSize);
		if(compress2((Bytef *)&compressedPixels[0], &compressedSize, (const Bytef *)&filteredPixels[0], (uLong)filteredPixels.size(), Z_BEST_SPEED) != Z_OK)
		{
			return false;
		}
		compressedPixels.resize((size_t)compressedSize);

		pngData.insert(pngData.end(), WINDOWS64_PNG_SIGNATURE, WINDOWS64_PNG_SIGNATURE + 8);

		BYTE ihdr[13];
		ihdr[0] = (BYTE)((width >> 24) & 0xFF);
		ihdr[1] = (BYTE)((width >> 16) & 0xFF);
		ihdr[2] = (BYTE)((width >> 8) & 0xFF);
		ihdr[3] = (BYTE)(width & 0xFF);
		ihdr[4] = (BYTE)((height >> 24) & 0xFF);
		ihdr[5] = (BYTE)((height >> 16) & 0xFF);
		ihdr[6] = (BYTE)((height >> 8) & 0xFF);
		ihdr[7] = (BYTE)(height & 0xFF);
		ihdr[8] = 8;
		ihdr[9] = 6;
		ihdr[10] = 0;
		ihdr[11] = 0;
		ihdr[12] = 0;
		AppendPngChunk(pngData, "IHDR", ihdr, sizeof(ihdr));
		AppendPngChunk(pngData, "IDAT", compressedPixels.empty() ? NULL : &compressedPixels[0], (DWORD)compressedPixels.size());
		AppendPngChunk(pngData, "IEND", NULL, 0);
		return true;
	}

	bool BuildSquareThumbnailFromRgba(const BYTE *sourcePixels, DWORD sourceWidth, DWORD sourceHeight, DWORD sourceRowBytes, std::vector<BYTE> &thumbnailPixels)
	{
		thumbnailPixels.clear();
		if(sourcePixels == NULL || sourceWidth == 0 || sourceHeight == 0 || sourceRowBytes < (sourceWidth * 4))
		{
			return false;
		}

		const DWORD outputSize = WINDOWS64_SAVE_THUMBNAIL_DIMENSION;
		const DWORD outputRowBytes = outputSize * 4;
		const DWORD cropSize = sourceWidth < sourceHeight ? sourceWidth : sourceHeight;
		const DWORD cropX = (sourceWidth - cropSize) / 2;
		const DWORD cropY = (sourceHeight - cropSize) / 2;

		thumbnailPixels.resize((size_t)outputRowBytes * outputSize);
		for(DWORD y = 0; y < outputSize; ++y)
		{
			DWORD sourceY = cropY + ((y * cropSize) / outputSize);
			if(sourceY >= sourceHeight)
			{
				sourceY = sourceHeight - 1;
			}

			BYTE *destRow = &thumbnailPixels[(size_t)outputRowBytes * y];
			for(DWORD x = 0; x < outputSize; ++x)
			{
				DWORD sourceX = cropX + ((x * cropSize) / outputSize);
				if(sourceX >= sourceWidth)
				{
					sourceX = sourceWidth - 1;
				}

				const BYTE *sourcePixel = sourcePixels + ((size_t)sourceRowBytes * sourceY) + (sourceX * 4);
				BYTE *destPixel = destRow + (x * 4);
				destPixel[0] = sourcePixel[0];
				destPixel[1] = sourcePixel[1];
				destPixel[2] = sourcePixel[2];
				destPixel[3] = 0xFF;
			}
		}

		return !thumbnailPixels.empty();
	}

	bool EncodePreviewImageAsPng(const XSOCIAL_PREVIEWIMAGE &previewImage, std::vector<BYTE> &pngData)
	{
		pngData.clear();

		if(previewImage.pBytes == NULL || previewImage.Width == 0 || previewImage.Height == 0)
		{
			return false;
		}

		const DWORD rowBytes = previewImage.Width * 4;
		if(previewImage.Pitch < rowBytes)
		{
			return false;
		}

		std::vector<BYTE> rgbaPixels((size_t)rowBytes * previewImage.Height);
		for(DWORD y = 0; y < previewImage.Height; ++y)
		{
			const BYTE *srcRow = previewImage.pBytes + (previewImage.Pitch * y);
			BYTE *destRow = &rgbaPixels[(size_t)rowBytes * y];
			for(DWORD x = 0; x < previewImage.Width; ++x)
			{
				const BYTE blue = srcRow[(x * 4) + 0];
				const BYTE green = srcRow[(x * 4) + 1];
				const BYTE red = srcRow[(x * 4) + 2];
				destRow[(x * 4) + 0] = red;
				destRow[(x * 4) + 1] = green;
				destRow[(x * 4) + 2] = blue;
				destRow[(x * 4) + 3] = 0xFF;
			}
		}

		std::vector<BYTE> thumbnailPixels;
		const DWORD thumbnailRowBytes = WINDOWS64_SAVE_THUMBNAIL_DIMENSION * 4;
		if(!BuildSquareThumbnailFromRgba(&rgbaPixels[0], previewImage.Width, previewImage.Height, rowBytes, thumbnailPixels))
		{
			return false;
		}

		return EncodeRgbaPixelsAsPng(&thumbnailPixels[0], WINDOWS64_SAVE_THUMBNAIL_DIMENSION, WINDOWS64_SAVE_THUMBNAIL_DIMENSION, thumbnailRowBytes, pngData);
	}

	bool EncodeImageBufferAsPng(ImageFileBuffer &imageBuffer, std::vector<BYTE> &pngData)
	{
		pngData.clear();
		if(imageBuffer.GetBufferPointer() == NULL || imageBuffer.GetBufferSize() <= 0)
		{
			return false;
		}

		D3DXIMAGE_INFO imageInfo;
		ZeroMemory(&imageInfo, sizeof(imageInfo));
		int *decodedPixels = NULL;
		const HRESULT hr = RenderManager.LoadTextureData((BYTE *)imageBuffer.GetBufferPointer(), imageBuffer.GetBufferSize(), &imageInfo, &decodedPixels);
		if(hr != ERROR_SUCCESS || decodedPixels == NULL || imageInfo.Width == 0 || imageInfo.Height == 0)
		{
			delete[] decodedPixels;
			return false;
		}

		const DWORD rowBytes = imageInfo.Width * 4;
		std::vector<BYTE> rgbaPixels((size_t)rowBytes * imageInfo.Height);
		for(DWORD y = 0; y < imageInfo.Height; ++y)
		{
			for(DWORD x = 0; x < imageInfo.Width; ++x)
			{
				const unsigned int argb = (unsigned int)decodedPixels[(y * imageInfo.Width) + x];
				BYTE *destPixel = &rgbaPixels[((size_t)y * rowBytes) + (x * 4)];
				destPixel[0] = (BYTE)((argb >> 16) & 0xFF);
				destPixel[1] = (BYTE)((argb >> 8) & 0xFF);
				destPixel[2] = (BYTE)(argb & 0xFF);
				destPixel[3] = 0xFF;
			}
		}

		delete[] decodedPixels;

		std::vector<BYTE> thumbnailPixels;
		const DWORD thumbnailRowBytes = WINDOWS64_SAVE_THUMBNAIL_DIMENSION * 4;
		if(!BuildSquareThumbnailFromRgba(&rgbaPixels[0], imageInfo.Width, imageInfo.Height, rowBytes, thumbnailPixels))
		{
			return false;
		}

		return EncodeRgbaPixelsAsPng(&thumbnailPixels[0], WINDOWS64_SAVE_THUMBNAIL_DIMENSION, WINDOWS64_SAVE_THUMBNAIL_DIMENSION, thumbnailRowBytes, pngData);
	}

	bool DecodeImageBufferToArgb(BYTE *pbData, DWORD dwBytes, std::vector<int> &argbPixels, DWORD &width, DWORD &height)
	{
		argbPixels.clear();
		width = 0;
		height = 0;

		if(pbData == NULL || dwBytes == 0)
		{
			return false;
		}

		D3DXIMAGE_INFO imageInfo;
		ZeroMemory(&imageInfo, sizeof(imageInfo));
		int *decodedPixels = NULL;
		const HRESULT hr = RenderManager.LoadTextureData(pbData, dwBytes, &imageInfo, &decodedPixels);
		if(hr != ERROR_SUCCESS || decodedPixels == NULL || imageInfo.Width <= 0 || imageInfo.Height <= 0)
		{
			delete[] decodedPixels;
			return false;
		}

		width = (DWORD)imageInfo.Width;
		height = (DWORD)imageInfo.Height;
		argbPixels.assign(decodedPixels, decodedPixels + ((size_t)width * height));
		delete[] decodedPixels;
		return !argbPixels.empty();
	}

	bool IsArgbImageVisuallyUseful(const int *argbPixels, DWORD width, DWORD height)
	{
		if(argbPixels == NULL || width == 0 || height == 0)
		{
			return false;
		}

		const size_t pixelCount = (size_t)width * height;
		unsigned int minAlpha = 255;
		unsigned int maxAlpha = 0;
		unsigned int minRed = 255;
		unsigned int maxRed = 0;
		unsigned int minGreen = 255;
		unsigned int maxGreen = 0;
		unsigned int minBlue = 255;
		unsigned int maxBlue = 0;
		unsigned int nonTransparentPixels = 0;
		unsigned int nonBlackPixels = 0;

		for(size_t i = 0; i < pixelCount; ++i)
		{
			const unsigned int argb = (unsigned int)argbPixels[i];
			const unsigned int alpha = (argb >> 24) & 0xFF;
			const unsigned int red = (argb >> 16) & 0xFF;
			const unsigned int green = (argb >> 8) & 0xFF;
			const unsigned int blue = argb & 0xFF;

			if(alpha < minAlpha) minAlpha = alpha;
			if(alpha > maxAlpha) maxAlpha = alpha;
			if(red < minRed) minRed = red;
			if(red > maxRed) maxRed = red;
			if(green < minGreen) minGreen = green;
			if(green > maxGreen) maxGreen = green;
			if(blue < minBlue) minBlue = blue;
			if(blue > maxBlue) maxBlue = blue;

			if(alpha > 8 || red > 4 || green > 4 || blue > 4)
			{
				++nonTransparentPixels;
			}

			if(red > 4 || green > 4 || blue > 4)
			{
				++nonBlackPixels;
			}
		}

		const unsigned int alphaRange = maxAlpha - minAlpha;
		const unsigned int redRange = maxRed - minRed;
		const unsigned int greenRange = maxGreen - minGreen;
		const unsigned int blueRange = maxBlue - minBlue;
		const bool hasMeaningfulRange = redRange >= 6 || greenRange >= 6 || blueRange >= 6 || alphaRange >= 6;
		const bool hasVisiblePixels = nonTransparentPixels > (pixelCount / 4);
		const bool hasNonBlackPixels = nonBlackPixels > (pixelCount / 128);

		return hasMeaningfulRange && hasVisiblePixels && hasNonBlackPixels;
	}

	bool IsEncodedPngVisuallyUseful(BYTE *pbData, DWORD dwBytes)
	{
		if(pbData == NULL || dwBytes <= sizeof(WINDOWS64_PNG_SIGNATURE))
		{
			return false;
		}

		if(dwBytes < 8 || memcmp(pbData, WINDOWS64_PNG_SIGNATURE, 8) != 0)
		{
			return false;
		}

		DWORD width = 0;
		DWORD height = 0;
		std::vector<int> argbPixels;
		if(!DecodeImageBufferToArgb(pbData, dwBytes, argbPixels, width, height))
		{
			return false;
		}

		return IsArgbImageVisuallyUseful(&argbPixels[0], width, height);
	}

	bool IsRgbaImageVisuallyUseful(const BYTE *rgbaPixels, DWORD width, DWORD height, DWORD rowBytes)
	{
		if(rgbaPixels == NULL || width == 0 || height == 0 || rowBytes < (width * 4))
		{
			return false;
		}

		std::vector<int> argbPixels((size_t)width * height);
		for(DWORD y = 0; y < height; ++y)
		{
			const BYTE *srcRow = rgbaPixels + ((size_t)rowBytes * y);
			for(DWORD x = 0; x < width; ++x)
			{
				const BYTE red = srcRow[(x * 4) + 0];
				const BYTE green = srcRow[(x * 4) + 1];
				const BYTE blue = srcRow[(x * 4) + 2];
				BYTE alpha = srcRow[(x * 4) + 3];
				if(alpha <= 8 && (red > 4 || green > 4 || blue > 4))
				{
					alpha = 0xFF;
				}

				argbPixels[((size_t)y * width) + x] =
					((unsigned int)alpha << 24) |
					((unsigned int)red << 16) |
					((unsigned int)green << 8) |
					(unsigned int)blue;
			}
		}

		return IsArgbImageVisuallyUseful(&argbPixels[0], width, height);
	}

	bool IsPreviewImageVisuallyUseful(const XSOCIAL_PREVIEWIMAGE &previewImage)
	{
		if(previewImage.pBytes == NULL || previewImage.Width == 0 || previewImage.Height == 0)
		{
			return false;
		}

		const DWORD rowBytes = previewImage.Width * 4;
		if(previewImage.Pitch < rowBytes)
		{
			return false;
		}

		std::vector<int> argbPixels((size_t)previewImage.Width * previewImage.Height);
		for(DWORD y = 0; y < previewImage.Height; ++y)
		{
			const BYTE *srcRow = previewImage.pBytes + (previewImage.Pitch * y);
			for(DWORD x = 0; x < previewImage.Width; ++x)
			{
				const BYTE blue = srcRow[(x * 4) + 0];
				const BYTE green = srcRow[(x * 4) + 1];
				const BYTE red = srcRow[(x * 4) + 2];
				argbPixels[(y * previewImage.Width) + x] =
					(0xFFu << 24) |
					((unsigned int)red << 16) |
					((unsigned int)green << 8) |
					(unsigned int)blue;
			}
		}

		return IsArgbImageVisuallyUseful(&argbPixels[0], previewImage.Width, previewImage.Height);
	}

	bool TryCaptureBackBufferAsPng(std::vector<BYTE> &pngData)
	{
		pngData.clear();

		if(g_pSwapChain == NULL || g_pd3dDevice == NULL || g_pImmediateContext == NULL)
		{
			return false;
		}

		ID3D11Texture2D *backBuffer = NULL;
		HRESULT hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backBuffer);
		if(FAILED(hr) || backBuffer == NULL)
		{
			return false;
		}

		D3D11_TEXTURE2D_DESC desc;
		backBuffer->GetDesc(&desc);
		if(desc.Width == 0 || desc.Height == 0)
		{
			backBuffer->Release();
			return false;
		}

		ID3D11Texture2D *copySource = backBuffer;
		ID3D11Texture2D *resolvedTexture = NULL;
		if(desc.SampleDesc.Count > 1)
		{
			D3D11_TEXTURE2D_DESC resolveDesc = desc;
			resolveDesc.BindFlags = 0;
			resolveDesc.CPUAccessFlags = 0;
			resolveDesc.MiscFlags = 0;
			resolveDesc.Usage = D3D11_USAGE_DEFAULT;
			resolveDesc.SampleDesc.Count = 1;
			resolveDesc.SampleDesc.Quality = 0;

			hr = g_pd3dDevice->CreateTexture2D(&resolveDesc, NULL, &resolvedTexture);
			if(FAILED(hr) || resolvedTexture == NULL)
			{
				backBuffer->Release();
				return false;
			}

			g_pImmediateContext->ResolveSubresource(resolvedTexture, 0, backBuffer, 0, desc.Format);
			copySource = resolvedTexture;
		}

		D3D11_TEXTURE2D_DESC stagingDesc = desc;
		stagingDesc.BindFlags = 0;
		stagingDesc.MiscFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.ArraySize = 1;
		stagingDesc.MipLevels = 1;
		stagingDesc.SampleDesc.Count = 1;
		stagingDesc.SampleDesc.Quality = 0;

		ID3D11Texture2D *stagingTexture = NULL;
		hr = g_pd3dDevice->CreateTexture2D(&stagingDesc, NULL, &stagingTexture);
		if(FAILED(hr) || stagingTexture == NULL)
		{
			if(resolvedTexture != NULL)
			{
				resolvedTexture->Release();
			}
			backBuffer->Release();
			return false;
		}

		g_pImmediateContext->CopyResource(stagingTexture, copySource);
		g_pImmediateContext->Flush();

		D3D11_MAPPED_SUBRESOURCE mappedResource;
		ZeroMemory(&mappedResource, sizeof(mappedResource));
		hr = g_pImmediateContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
		if(FAILED(hr) || mappedResource.pData == NULL)
		{
			stagingTexture->Release();
			if(resolvedTexture != NULL)
			{
				resolvedTexture->Release();
			}
			backBuffer->Release();
			return false;
		}

		const bool isBgra =
			desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
			desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		const bool isRgba =
			desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
			desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

		if(!isBgra && !isRgba)
		{
			g_pImmediateContext->Unmap(stagingTexture, 0);
			stagingTexture->Release();
			if(resolvedTexture != NULL)
			{
				resolvedTexture->Release();
			}
			backBuffer->Release();
			return false;
		}

		const DWORD rowBytes = desc.Width * 4;
		std::vector<BYTE> rgbaPixels((size_t)rowBytes * desc.Height);
		for(UINT y = 0; y < desc.Height; ++y)
		{
			const BYTE *srcRow = (const BYTE *)mappedResource.pData + ((size_t)mappedResource.RowPitch * y);
			BYTE *destRow = &rgbaPixels[(size_t)rowBytes * y];
			for(UINT x = 0; x < desc.Width; ++x)
			{
				const BYTE c0 = srcRow[(x * 4) + 0];
				const BYTE c1 = srcRow[(x * 4) + 1];
				const BYTE c2 = srcRow[(x * 4) + 2];

				if(isBgra)
				{
					destRow[(x * 4) + 0] = c2;
					destRow[(x * 4) + 1] = c1;
					destRow[(x * 4) + 2] = c0;
					destRow[(x * 4) + 3] = 0xFF;
				}
				else
				{
					destRow[(x * 4) + 0] = c0;
					destRow[(x * 4) + 1] = c1;
					destRow[(x * 4) + 2] = c2;
					destRow[(x * 4) + 3] = 0xFF;
				}
			}
		}

		g_pImmediateContext->Unmap(stagingTexture, 0);
		stagingTexture->Release();
		if(resolvedTexture != NULL)
		{
			resolvedTexture->Release();
		}
		backBuffer->Release();

		std::vector<BYTE> thumbnailPixels;
		const DWORD thumbnailRowBytes = WINDOWS64_SAVE_THUMBNAIL_DIMENSION * 4;
		if(!BuildSquareThumbnailFromRgba(&rgbaPixels[0], desc.Width, desc.Height, rowBytes, thumbnailPixels) ||
			!IsRgbaImageVisuallyUseful(&thumbnailPixels[0], WINDOWS64_SAVE_THUMBNAIL_DIMENSION, WINDOWS64_SAVE_THUMBNAIL_DIMENSION, thumbnailRowBytes))
		{
			pngData.clear();
			return false;
		}

		if(!EncodeRgbaPixelsAsPng(&thumbnailPixels[0], WINDOWS64_SAVE_THUMBNAIL_DIMENSION, WINDOWS64_SAVE_THUMBNAIL_DIMENSION, thumbnailRowBytes, pngData) || pngData.empty())
		{
			pngData.clear();
			return false;
		}

		return IsEncodedPngVisuallyUseful(&pngData[0], (DWORD)pngData.size());
	}

	bool TryCaptureCurrentFrameSaveThumbnailPng(std::vector<BYTE> &pngData)
	{
		pngData.clear();

		if(TryCaptureBackBufferAsPng(pngData))
		{
			return true;
		}

		ImageFileBuffer capturedBuffer;
		ZeroMemory(&capturedBuffer, sizeof(capturedBuffer));
		XSOCIAL_PREVIEWIMAGE previewImage;
		ZeroMemory(&previewImage, sizeof(previewImage));
		RenderManager.CaptureScreen(&capturedBuffer, &previewImage);

		bool captured = false;
		if(IsPreviewImageVisuallyUseful(previewImage) && EncodePreviewImageAsPng(previewImage, pngData) && !pngData.empty() && IsEncodedPngVisuallyUseful(&pngData[0], (DWORD)pngData.size()))
		{
			captured = true;
		}
		else if(EncodeImageBufferAsPng(capturedBuffer, pngData) && !pngData.empty() && IsEncodedPngVisuallyUseful(&pngData[0], (DWORD)pngData.size()))
		{
			captured = true;
		}

		if(previewImage.pBytes != NULL)
		{
			free(previewImage.pBytes);
			previewImage.pBytes = NULL;
		}

		if(capturedBuffer.GetBufferPointer() != NULL)
		{
			capturedBuffer.Release();
		}

		if(!captured)
		{
			pngData.clear();
		}

		return captured;
	}
}

CConsoleMinecraftApp app;

CConsoleMinecraftApp::CConsoleMinecraftApp() : CMinecraftApp()
{
	m_saveThumbnailCapturePending = false;
	m_saveThumbnailCaptureRetryCount = 0;
}

void CConsoleMinecraftApp::SetRichPresenceContext(int iPad, int contextId)
{
	ProfileManager.SetRichPresenceContextValue(iPad,CONTEXT_GAME_STATE,contextId);
}

void CConsoleMinecraftApp::StoreLaunchData()
{
}
void CConsoleMinecraftApp::ExitGame()
{
}
void CConsoleMinecraftApp::FatalLoadError()
{
}

void CConsoleMinecraftApp::CaptureSaveThumbnail()
{
	if(m_saveThumbnailCapturePending)
	{
		return;
	}

	ReleaseSaveThumbnail();
	m_saveThumbnailCapturePending = true;
	m_saveThumbnailCaptureRetryCount = 0;
	app.DebugPrintf("Queued world-only save thumbnail capture for Windows64\n");
}
void CConsoleMinecraftApp::GetSaveThumbnail(PBYTE *pbData,DWORD *pdwSize)
{
	if(pbData == NULL || pdwSize == NULL)
	{
		ReleaseSaveThumbnail();
		return;
	}

	if(m_saveThumbnailData.empty())
	{
		*pbData = NULL;
		*pdwSize = 0;
		return;
	}

	*pbData = &m_saveThumbnailData[0];
	*pdwSize = (DWORD)m_saveThumbnailData.size();
}
void CConsoleMinecraftApp::ReleaseSaveThumbnail()
{
	m_saveThumbnailCapturePending = false;
	m_saveThumbnailCaptureRetryCount = 0;
	std::vector<BYTE>().swap(m_saveThumbnailData);
}

bool CConsoleMinecraftApp::IsSaveThumbnailCaptureComplete()
{
	return !m_saveThumbnailCapturePending;
}

bool CConsoleMinecraftApp::ShouldCaptureSaveThumbnailFromWorldFrame(int iPad)
{
	return m_saveThumbnailCapturePending && iPad == ProfileManager.GetPrimaryPad();
}

void CConsoleMinecraftApp::CaptureSaveThumbnailFromWorldFrame(int iPad)
{
	if(!ShouldCaptureSaveThumbnailFromWorldFrame(iPad))
	{
		return;
	}

	std::vector<BYTE> encodedThumbnailData;
	if(TryCaptureCurrentFrameSaveThumbnailPng(encodedThumbnailData))
	{
		m_saveThumbnailCapturePending = false;
		m_saveThumbnailCaptureRetryCount = 0;
		m_saveThumbnailData = encodedThumbnailData;
		app.DebugPrintf("Captured world-only save thumbnail on Windows64 (%d bytes)\n", (int)m_saveThumbnailData.size());
		return;
	}

	++m_saveThumbnailCaptureRetryCount;
	if(m_saveThumbnailCaptureRetryCount < WINDOWS64_SAVE_THUMBNAIL_CAPTURE_MAX_RETRIES)
	{
		app.DebugPrintf("Retrying world-only save thumbnail capture on Windows64 (%d)\n", m_saveThumbnailCaptureRetryCount);
		return;
	}

	m_saveThumbnailCapturePending = false;
	std::vector<BYTE>().swap(m_saveThumbnailData);
	app.DebugPrintf("World-only save thumbnail capture failed on Windows64 after %d retries; no placeholder will be written\n", m_saveThumbnailCaptureRetryCount);
	m_saveThumbnailCaptureRetryCount = 0;
}

void CConsoleMinecraftApp::GetScreenshot(int iPad,PBYTE *pbData,DWORD *pdwSize)
{
}

void CConsoleMinecraftApp::TemporaryCreateGameStart()
{
	////////////////////////////////////////////////////////////////////////////////////////////// From CScene_Main::OnInit

	app.setLevelGenerationOptions(NULL);

	// From CScene_Main::RunPlayGame
	Minecraft *pMinecraft=Minecraft::GetInstance();
	app.ReleaseSaveThumbnail();
	ProfileManager.SetLockedProfile(0);
	pMinecraft->user->name = L"Windows";
	app.ApplyGameSettingsChanged(0);

	////////////////////////////////////////////////////////////////////////////////////////////// From CScene_MultiGameJoinLoad::OnInit
	MinecraftServer::resetFlags();

	// From CScene_MultiGameJoinLoad::OnNotifyPressEx
	app.SetTutorialMode( false );
	app.SetCorruptSaveDeleted(false);

	////////////////////////////////////////////////////////////////////////////////////////////// From CScene_MultiGameCreate::CreateGame

	app.ClearTerrainFeaturePosition();
	wstring wWorldName = L"TestWorld";

	app.PrepareNewSaveData(wWorldName.c_str());

	bool isFlat = false;
	__int64 seedValue = 0; // BiomeSource::findSeed(isFlat?LevelType::lvl_flat:LevelType::lvl_normal);	// 4J - was (new Random())->nextLong() - now trying to actually find a seed to suit our requirements

	NetworkGameInitData *param = new NetworkGameInitData();
	param->seed = seedValue;
	param->saveData = NULL;

	app.SetGameHostOption(eGameHostOption_Difficulty,0);
	app.SetGameHostOption(eGameHostOption_FriendsOfFriends,0);
	app.SetGameHostOption(eGameHostOption_Gamertags,1);
	app.SetGameHostOption(eGameHostOption_BedrockFog,1);

	app.SetGameHostOption(eGameHostOption_GameType,GameType::CREATIVE->getId() ); // LevelSettings::GAMETYPE_SURVIVAL
	app.SetGameHostOption(eGameHostOption_LevelType, 0 );
	app.SetGameHostOption(eGameHostOption_Structures, 1 );
	app.SetGameHostOption(eGameHostOption_BonusChest, 0 );

	app.SetGameHostOption(eGameHostOption_PvP, 1);
	app.SetGameHostOption(eGameHostOption_TrustPlayers, 1 );
	app.SetGameHostOption(eGameHostOption_FireSpreads, 1 );
	app.SetGameHostOption(eGameHostOption_TNT, 1 );
	app.SetGameHostOption(eGameHostOption_HostCanFly, 1);
	app.SetGameHostOption(eGameHostOption_HostCanChangeHunger, 1);
	app.SetGameHostOption(eGameHostOption_HostCanBeInvisible, 1 );

	param->settings = app.GetGameHostOption( eGameHostOption_All );

	g_NetworkManager.FakeLocalPlayerJoined();

	LoadingInputParams *loadingParams = new LoadingInputParams();
	loadingParams->func = &CGameNetworkManager::RunNetworkGameThreadProc;
	loadingParams->lpParam = (LPVOID)param;

	// Reset the autosave time
	app.SetAutosaveTimerTime();

	C4JThread* thread = new C4JThread(loadingParams->func, loadingParams->lpParam, "RunNetworkGame");
	thread->Run();
}

int CConsoleMinecraftApp::GetLocalTMSFileIndex(WCHAR *wchTMSFile,bool bFilenameIncludesExtension,eFileExtensionType eEXT)
{
	return -1;
}

int CConsoleMinecraftApp::LoadLocalTMSFile(WCHAR *wchTMSFile)
{
	return -1;
}

int CConsoleMinecraftApp::LoadLocalTMSFile(WCHAR *wchTMSFile, eFileExtensionType eExt)
{
	return -1;
}

void CConsoleMinecraftApp::FreeLocalTMSFiles(eTMSFileType eType)
{
}
