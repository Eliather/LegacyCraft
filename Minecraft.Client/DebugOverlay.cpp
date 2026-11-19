#include "stdafx.h"
#include "DebugOverlay.h"
#include "ShaderManager.h"
#include "Minecraft.h"
#include "MultiplayerLocalPlayer.h"
#include "MultiPlayerLevel.h"
#include "ClientConstants.h"
#include "Font.h"
#include "Chunk.h"
#include "Tesselator.h"
#include "stubs.h"
#include "..\Minecraft.World\net.minecraft.world.level.h"
#include "..\Minecraft.World\net.minecraft.world.level.chunk.h"
#include "..\Minecraft.World\net.minecraft.world.level.biome.h"
#include "..\Minecraft.World\net.minecraft.world.level.dimension.h"
#include "..\Minecraft.World\Mth.h"
#include "..\Minecraft.World\StringHelpers.h"

#ifdef _WINDOWS64
#include <intrin.h>
#include <psapi.h>
#endif

namespace
{
	bool s_initialized = false;
	bool s_visible = false;

	// Real-time FPS & frame counter
	LONGLONG s_qpcFreq = 0;
	LONGLONG s_lastSampleTick = 0;
	int s_framesAccum = 0;
	int s_currentFps = 0;
	int s_lastChunkUpdates = 0;

	// Cached hardware strings (queried once)
	std::wstring s_cpuName;
	int s_cpuLogicalCores = 0;

	std::vector<DebugOverlay::SectionProviderFn> s_leftSections;
	std::vector<DebugOverlay::SectionProviderFn> s_rightSections;

	std::wstring AsciiToWide(const char *str)
	{
		if (str == NULL || str[0] == '\0')
		{
			return L"Unknown";
		}
		std::wstring out;
		while (*str)
		{
			unsigned char ch = (unsigned char)(*str++);
			out.push_back((ch >= 32 && ch < 127) ? (wchar_t)ch : L'?');
		}
		return out;
	}

	std::wstring TrimWide(const std::wstring &s)
	{
		size_t first = 0;
		while (first < s.size() && (s[first] == L' ' || s[first] == L'\t' || s[first] == L'\r' || s[first] == L'\n'))
		{
			++first;
		}
		size_t last = s.size();
		while (last > first && (s[last - 1] == L' ' || s[last - 1] == L'\t' || s[last - 1] == L'\r' || s[last - 1] == L'\n'))
		{
			--last;
		}
		return s.substr(first, last - first);
	}

	void DetectCpuHardware()
	{
		if (!s_cpuName.empty())
		{
			return;
		}

#ifdef _WINDOWS64
		int cpuInfo[4] = { 0, 0, 0, 0 };
		__cpuid(cpuInfo, 0x80000000);
		unsigned int maxExtId = (unsigned int)cpuInfo[0];

		char brand[64];
		memset(brand, 0, sizeof(brand));
		if (maxExtId >= 0x80000004)
		{
			__cpuid((int *)(brand + 0), 0x80000002);
			__cpuid((int *)(brand + 16), 0x80000003);
			__cpuid((int *)(brand + 32), 0x80000004);
			brand[48] = '\0';
		}

		s_cpuName = TrimWide(AsciiToWide(brand));

		SYSTEM_INFO sysInfo;
		ZeroMemory(&sysInfo, sizeof(sysInfo));
		GetSystemInfo(&sysInfo);
		s_cpuLogicalCores = (int)sysInfo.dwNumberOfProcessors;
#endif

		if (s_cpuName.empty())
		{
			s_cpuName = L"x64 Processor";
		}
		if (s_cpuLogicalCores <= 0)
		{
			s_cpuLogicalCores = 1;
		}
	}

	void DrawBackdropRect(int x0, int y0, int x1, int y1, int argbColor)
	{
		if (x1 <= x0 || y1 <= y0)
		{
			return;
		}

		float a = ((argbColor >> 24) & 0xff) / 255.0f;
		float r = ((argbColor >> 16) & 0xff) / 255.0f;
		float g = ((argbColor >> 8) & 0xff) / 255.0f;
		float b = (argbColor & 0xff) / 255.0f;

		Tesselator *t = Tesselator::getInstance();
		glEnable(GL_BLEND);
		glDisable(GL_TEXTURE_2D);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(r, g, b, a);
		t->begin();
		t->vertex((float)x0, (float)y1, 0.0f);
		t->vertex((float)x1, (float)y1, 0.0f);
		t->vertex((float)x1, (float)y0, 0.0f);
		t->vertex((float)x0, (float)y0, 0.0f);
		t->end();
		glEnable(GL_TEXTURE_2D);
	}

	// -------------------------------------------------------------------------
	// Built-in Modular Section Providers (Left Column)
	// -------------------------------------------------------------------------
	void ProvideLeftTitleAndFpsSection(const DebugOverlay::OverlayContext &ctx, std::vector<std::wstring> &outLines)
	{
		wchar_t buf[256];
		swprintf(buf, 256, L"%ls", ClientConstants::VERSION_STRING.c_str());
		outLines.push_back(buf);

		const wchar_t *vsyncState = RenderManager.IsVsyncEnabled() ? L"vsync" : L"unlimited";
		swprintf(buf, 256, L"%d fps (%d chunk updates) T: %ls",
			ctx.currentFps, ctx.chunkUpdates, vsyncState);
		outLines.push_back(buf);
	}

	void ProvideLeftCoordinatesSection(const DebugOverlay::OverlayContext &ctx, std::vector<std::wstring> &outLines)
	{
		if (ctx.minecraft == NULL || ctx.minecraft->player == NULL)
		{
			return;
		}

		shared_ptr<MultiplayerLocalPlayer> player = ctx.minecraft->player;
		const double px = player->x;
		const double pyFeet = player->bb != NULL ? player->bb->y0 : (player->y - player->heightOffset);
		const double pz = player->z;

		const int blockX = Mth::floor(px);
		const int blockY = Mth::floor(pyFeet);
		const int blockZ = Mth::floor(pz);

		const int chunkX = blockX >> 4;
		const int chunkY = blockY >> 4;
		const int chunkZ = blockZ >> 4;
		const int localX = blockX & 15;
		const int localY = blockY & 15;
		const int localZ = blockZ & 15;

		outLines.push_back(L"");

		wchar_t buf[256];
		swprintf(buf, 256, L"XYZ: %.3f / %.5f / %.3f", px, pyFeet, pz);
		outLines.push_back(buf);

		swprintf(buf, 256, L"Block: %d %d %d [%d %d %d]",
			blockX, blockY, blockZ, localX, localY, localZ);
		outLines.push_back(buf);

		swprintf(buf, 256, L"Chunk: %d %d %d", chunkX, chunkY, chunkZ);
		outLines.push_back(buf);
	}

	void ProvideLeftFacingAndWorldSection(const DebugOverlay::OverlayContext &ctx, std::vector<std::wstring> &outLines)
	{
		if (ctx.minecraft == NULL || ctx.minecraft->player == NULL)
		{
			return;
		}

		shared_ptr<MultiplayerLocalPlayer> player = ctx.minecraft->player;
		float yaw = fmodf(player->yRot, 360.0f);
		if (yaw >= 180.0f) yaw -= 360.0f;
		if (yaw < -180.0f) yaw += 360.0f;
		float pitch = player->xRot;

		int dir = Mth::floor((double)(player->yRot * 4.0f / 360.0f) + 0.5) & 3;
		const wchar_t *cardinal = L"south";
		const wchar_t *axisHint = L"Towards positive Z";
		switch (dir)
		{
		case 0: cardinal = L"south"; axisHint = L"Towards positive Z"; break;
		case 1: cardinal = L"west";  axisHint = L"Towards negative X"; break;
		case 2: cardinal = L"north"; axisHint = L"Towards negative Z"; break;
		case 3: cardinal = L"east";  axisHint = L"Towards positive X"; break;
		}

		wchar_t buf[256];
		swprintf(buf, 256, L"Facing: %ls (%ls) (%.1f / %.1f)",
			cardinal, axisHint, yaw, pitch);
		outLines.push_back(buf);

		if (ctx.minecraft->level != NULL)
		{
			const int bx = Mth::floor(player->x);
			const int by = Mth::floor(player->bb != NULL ? player->bb->y0 : player->y);
			const int bz = Mth::floor(player->z);

			if (ctx.minecraft->level->hasChunkAt(bx, by, bz))
			{
				LevelChunk *chunk = ctx.minecraft->level->getChunkAt(bx, bz);
				if (chunk != NULL)
				{
					Biome *biome = chunk->getBiome(bx & 15, bz & 15, ctx.minecraft->level->getBiomeSource());
					if (biome != NULL)
					{
						swprintf(buf, 256, L"Biome: %ls", biome->m_name.c_str());
						outLines.push_back(buf);
					}
				}
			}
		}
	}

	// -------------------------------------------------------------------------
	// Built-in Modular Section Providers (Right Column)
	// -------------------------------------------------------------------------
	void ProvideRightHardwareSection(const DebugOverlay::OverlayContext &ctx, std::vector<std::wstring> &outLines)
	{
		DetectCpuHardware();

		wchar_t buf[256];

#ifdef _WINDOWS64
		PROCESS_MEMORY_COUNTERS pmc;
		ZeroMemory(&pmc, sizeof(pmc));
		pmc.cb = sizeof(pmc);
		MEMORYSTATUSEX memStatus;
		ZeroMemory(&memStatus, sizeof(memStatus));
		memStatus.dwLength = sizeof(memStatus);
		if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) && GlobalMemoryStatusEx(&memStatus))
		{
			unsigned long long usedMB = (unsigned long long)(pmc.WorkingSetSize / (1024ULL * 1024ULL));
			unsigned long long totalMB = (unsigned long long)(memStatus.ullTotalPhys / (1024ULL * 1024ULL));
			swprintf(buf, 256, L"Mem: %lluMB / %lluMB", usedMB, totalMB);
			outLines.push_back(buf);
			outLines.push_back(L"");
		}
#endif

		swprintf(buf, 256, L"CPU: %dx %ls", s_cpuLogicalCores, s_cpuName.c_str());
		outLines.push_back(buf);

		outLines.push_back(L"");

		int physW = (ctx.minecraft != NULL) ? ctx.minecraft->width : ctx.screenWidth;
		int physH = (ctx.minecraft != NULL) ? ctx.minecraft->height : ctx.screenHeight;
		std::wstring backendName = AsciiToWide(RenderManager.GetBackendName());
		swprintf(buf, 256, L"Display: %dx%d (%ls)", physW, physH, backendName.c_str());
		outLines.push_back(buf);

		std::wstring gpuAdapter = TrimWide(AsciiToWide(RenderManager.GetAdapterName()));
		swprintf(buf, 256, L"GPU: %ls", gpuAdapter.c_str());
		outLines.push_back(buf);

		std::wstring shaderDisplay = ShaderManager::GetActiveDisplayName();
		swprintf(buf, 256, L"Shaders: %ls", shaderDisplay.c_str());
		outLines.push_back(buf);

		const std::wstring &shaderErr = ShaderManager::GetLastError();
		if (!shaderErr.empty())
		{
			swprintf(buf, 256, L"Shader Error: %.64ls", shaderErr.c_str());
			outLines.push_back(buf);
		}
	}
}

void DebugOverlay::Init()
{
	if (s_initialized)
	{
		return;
	}
	s_initialized = true;

	LARGE_INTEGER freq = {};
	if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0)
	{
		s_qpcFreq = freq.QuadPart;
	}
	LARGE_INTEGER now = {};
	QueryPerformanceCounter(&now);
	s_lastSampleTick = now.QuadPart;

	DetectCpuHardware();

	s_leftSections.clear();
	s_rightSections.clear();

	RegisterLeftSection(&ProvideLeftTitleAndFpsSection);
	RegisterLeftSection(&ProvideLeftCoordinatesSection);
	RegisterLeftSection(&ProvideLeftFacingAndWorldSection);

	RegisterRightSection(&ProvideRightHardwareSection);
}

void DebugOverlay::Reset()
{
	s_visible = false;
}

void DebugOverlay::Toggle()
{
	Init();
	s_visible = !s_visible;
}

void DebugOverlay::SetVisible(bool visible)
{
	Init();
	s_visible = visible;
}

bool DebugOverlay::IsVisible()
{
	return s_visible;
}

void DebugOverlay::NotifyFrameRendered()
{
	Init();
	++s_framesAccum;

	LARGE_INTEGER now = {};
	QueryPerformanceCounter(&now);
	if (s_qpcFreq > 0)
	{
		LONGLONG elapsedTicks = now.QuadPart - s_lastSampleTick;
		if (elapsedTicks >= s_qpcFreq)
		{
			double elapsedSec = (double)elapsedTicks / (double)s_qpcFreq;
			s_currentFps = (elapsedSec > 0.0) ? (int)((double)s_framesAccum / elapsedSec + 0.5) : s_framesAccum;
			s_lastChunkUpdates = Chunk::updates;
			s_framesAccum = 0;
			s_lastSampleTick = now.QuadPart;
		}
	}
}

void DebugOverlay::HandleInput(Minecraft *minecraft)
{
	Init();

	// Only active when inside a loaded world with a local player
	if (minecraft == NULL || minecraft->level == NULL || minecraft->player == NULL || !app.GetGameStarted())
	{
		s_visible = false;
		return;
	}

	int primaryPad = ProfileManager.GetPrimaryPad();
	bool inActiveGameplay = (minecraft->getScreen() == NULL) && !ui.GetMenuDisplayed(primaryPad);
	if (inActiveGameplay && Keyboard::isKeyPressed(Keyboard::KEY_F3))
	{
		Toggle();
	}
#ifdef _WINDOWS64
	static bool s_f4WasDown = false;
	static bool s_f3RWasDown = false;
	const bool f4Down = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
	const bool f3RDown = ((GetAsyncKeyState(VK_F3) & 0x8000) != 0) && ((GetAsyncKeyState('R') & 0x8000) != 0);
	if (inActiveGameplay)
	{
		if (f4Down && !s_f4WasDown)
		{
			ShaderManager::ToggleEnabled();
		}
		if (f3RDown && !s_f3RWasDown)
		{
			ShaderManager::ReloadCurrent();
		}
	}
	s_f4WasDown = f4Down;
	s_f3RWasDown = f3RDown;
#endif
}

void DebugOverlay::RegisterLeftSection(SectionProviderFn provider)
{
	if (provider != NULL)
	{
		s_leftSections.push_back(provider);
	}
}

void DebugOverlay::RegisterRightSection(SectionProviderFn provider)
{
	if (provider != NULL)
	{
		s_rightSections.push_back(provider);
	}
}

void DebugOverlay::Render(Minecraft *minecraft, Font *font, int screenWidth, int screenHeight, int safezoneXHalf, int safezoneYHalf)
{
	if (!s_visible || minecraft == NULL || font == NULL || minecraft->level == NULL || minecraft->player == NULL)
	{
		return;
	}

	int primaryPad = ProfileManager.GetPrimaryPad();
	// Only display during active in-game HUD (hide when pause/inventory/menus or HUD disabled)
	if (minecraft->getScreen() != NULL || ui.GetMenuDisplayed(primaryPad))
	{
		return;
	}

	Init();

	OverlayContext ctx;
	ctx.minecraft = minecraft;
	ctx.font = font;
	ctx.screenWidth = screenWidth;
	ctx.screenHeight = screenHeight;
	ctx.safeLeft = 4;
	ctx.safeRight = screenWidth - 4;
	ctx.safeTop = 4;
	ctx.currentFps = s_currentFps;
	ctx.chunkUpdates = s_lastChunkUpdates;

	std::vector<std::wstring> leftLines;
	for (size_t i = 0; i < s_leftSections.size(); ++i)
	{
		s_leftSections[i](ctx, leftLines);
	}

	std::vector<std::wstring> rightLines;
	for (size_t i = 0; i < s_rightSections.size(); ++i)
	{
		s_rightSections[i](ctx, rightLines);
	}

	const int lineHeight = 9;
	const int backdropColor = (int)0x90505050;
	const int textColor = 0xE0E0E0;

	glPushMatrix();
	glDisable(GL_DEPTH_TEST);

	// Left column
	int yLeft = ctx.safeTop;
	for (size_t i = 0; i < leftLines.size(); ++i)
	{
		const std::wstring &line = leftLines[i];
		if (!line.empty())
		{
			int textW = font->width(line);
			DrawBackdropRect(ctx.safeLeft - 1, yLeft - 1, ctx.safeLeft + textW + 1, yLeft + lineHeight - 1, backdropColor);
			font->draw(line, ctx.safeLeft, yLeft, textColor);
		}
		yLeft += lineHeight;
	}

	// Right column (right-aligned)
	int yRight = ctx.safeTop;
	for (size_t i = 0; i < rightLines.size(); ++i)
	{
		const std::wstring &line = rightLines[i];
		if (!line.empty())
		{
			int textW = font->width(line);
			int xPos = ctx.safeRight - textW;
			DrawBackdropRect(xPos - 1, yRight - 1, ctx.safeRight + 1, yRight + lineHeight - 1, backdropColor);
			font->draw(line, xPos, yRight, textColor);
		}
		yRight += lineHeight;
	}

	glEnable(GL_DEPTH_TEST);
	glPopMatrix();
}
