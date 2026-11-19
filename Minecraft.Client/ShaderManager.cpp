#include "stdafx.h"
#include "ShaderManager.h"
#include "UserData_Info.h"

#ifdef _WINDOWS64
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#endif

namespace
{
	bool g_initialized = false;
	bool g_enabled = true;
	std::wstring g_activePackId = L"Default";
	std::wstring g_lastError;
	std::vector<ShaderManager::PackEntry> g_packs;
	C4JShaderFrameParams g_lastFrameParams = {};

#ifdef _WINDOWS64
	std::wstring GetShadersRootDir()
	{
		wchar_t exePath[MAX_PATH] = { 0 };
		DWORD len = GetModuleFileNameW(NULL, exePath, MAX_PATH);
		if (len == 0 || len >= MAX_PATH)
		{
			return L"shaders";
		}
		std::wstring dir(exePath, len);
		size_t slash = dir.find_last_of(L"\\/");
		if (slash != std::wstring::npos)
		{
			dir.resize(slash + 1);
		}
		dir += L"shaders";
		return dir;
	}

	bool ReadFileToString(const std::wstring &path, std::string &out)
	{
		HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE) return false;

		LARGE_INTEGER sz = { 0 };
		if (!GetFileSizeEx(hFile, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (4 * 1024 * 1024))
		{
			CloseHandle(hFile);
			return false;
		}
		out.resize((size_t)sz.QuadPart);
		DWORD bytesRead = 0;
		BOOL ok = ReadFile(hFile, &out[0], (DWORD)sz.QuadPart, &bytesRead, NULL);
		CloseHandle(hFile);
		if (!ok || bytesRead != (DWORD)sz.QuadPart)
		{
			out.clear();
			return false;
		}
		return true;
	}

	bool ReadFileToUint32Vec(const std::wstring &path, std::vector<uint32_t> &out)
	{
		std::string raw;
		if (!ReadFileToString(path, raw) || raw.size() < 4 || (raw.size() % 4) != 0)
		{
			return false;
		}
		out.resize(raw.size() / 4);
		memcpy(out.data(), raw.data(), raw.size());
		return true;
	}

	std::wstring ReadIniName(const std::wstring &iniPath, const std::wstring &fallback)
	{
		std::string content;
		if (!ReadFileToString(iniPath, content))
		{
			return fallback;
		}
		std::istringstream iss(content);
		std::string line;
		while (std::getline(iss, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
				line.pop_back();
			if (line.compare(0, 5, "name=") == 0 || line.compare(0, 5, "Name=") == 0)
			{
				std::string val = line.substr(5);
				while (!val.empty() && val.front() == ' ') val.erase(val.begin());
				if (!val.empty())
				{
					std::wstring wval;
					for (char c : val) wval.push_back((wchar_t)(unsigned char)c);
					return wval;
				}
			}
		}
		return fallback;
	}

	const ShaderManager::PackEntry *FindPackById(const std::wstring &id)
	{
		for (size_t i = 0; i < g_packs.size(); ++i)
		{
			if (_wcsicmp(g_packs[i].id.c_str(), id.c_str()) == 0)
			{
				return &g_packs[i];
			}
		}
		return NULL;
	}
#endif
}

void ShaderManager::Initialize()
{
	if (g_initialized) return;
	g_initialized = true;

	UserData_Info::EnsureLoaded();
	g_enabled = UserData_Info::GetShaderEnabled();
	g_activePackId = UserData_Info::GetShaderPackName();

	RefreshPackList();
	ApplyToBackend();
}

void ShaderManager::RefreshPackList()
{
	g_packs.clear();

	PackEntry defEntry;
	defEntry.id = L"Default";
	defEntry.displayName = L"Default (LegacyCraft)";
	defEntry.folderPath = L"";
	defEntry.isBuiltIn = true;
	g_packs.push_back(defEntry);

#ifdef _WINDOWS64
	const std::wstring rootDir = GetShadersRootDir();
	CreateDirectoryW(rootDir.c_str(), NULL);

	const std::wstring pattern = rootDir + L"\\*";
	WIN32_FIND_DATAW fd;
	HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		std::vector<PackEntry> found;
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
			if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
			if (_wcsicmp(fd.cFileName, L"Default") == 0) continue;

			const std::wstring folder = rootDir + L"\\" + fd.cFileName;
			// Accept folder if it contains any supported shader file
			const bool hasHlsl = (GetFileAttributesW((folder + L"\\world_dx.hlsl").c_str()) != INVALID_FILE_ATTRIBUTES);
			const bool hasGlsl = (GetFileAttributesW((folder + L"\\world_gl.vert").c_str()) != INVALID_FILE_ATTRIBUTES &&
			                      GetFileAttributesW((folder + L"\\world_gl.frag").c_str()) != INVALID_FILE_ATTRIBUTES);
			const bool hasVk   = (GetFileAttributesW((folder + L"\\world_vk_ps.spv").c_str()) != INVALID_FILE_ATTRIBUTES);
			if (!hasHlsl && !hasGlsl && !hasVk) continue;

			PackEntry entry;
			entry.id = fd.cFileName;
			entry.folderPath = folder;
			entry.displayName = ReadIniName(folder + L"\\shader.ini", entry.id);
			entry.isBuiltIn = false;
			found.push_back(entry);
		}
		while (FindNextFileW(hFind, &fd));
		FindClose(hFind);

		std::sort(found.begin(), found.end(), [](const PackEntry &a, const PackEntry &b) {
			return _wcsicmp(a.id.c_str(), b.id.c_str()) < 0;
		});
		for (size_t i = 0; i < found.size(); ++i)
		{
			g_packs.push_back(found[i]);
		}
	}
#endif
}

const std::vector<ShaderManager::PackEntry> &ShaderManager::GetAvailablePacks()
{
	if (!g_initialized) Initialize();
	return g_packs;
}

void ShaderManager::SetEnabled(bool enabled)
{
	if (!g_initialized) Initialize();
	g_enabled = enabled;
	UserData_Info::SetShaderEnabled(g_enabled);
	ApplyToBackend();
}

bool ShaderManager::IsEnabled()
{
	if (!g_initialized) Initialize();
	return g_enabled;
}

bool ShaderManager::ToggleEnabled()
{
	SetEnabled(!IsEnabled());
	return g_enabled;
}

bool ShaderManager::SelectPack(const std::wstring &packId)
{
	if (!g_initialized) Initialize();
	RefreshPackList();
	g_activePackId = packId.empty() ? L"Default" : packId;
	UserData_Info::SetShaderPackName(g_activePackId);
	ApplyToBackend();
	return g_lastError.empty();
}

void ShaderManager::CyclePack()
{
	if (!g_initialized) Initialize();
	RefreshPackList();
	if (g_packs.empty()) return;

	size_t curIdx = 0;
	for (size_t i = 0; i < g_packs.size(); ++i)
	{
		if (_wcsicmp(g_packs[i].id.c_str(), g_activePackId.c_str()) == 0)
		{
			curIdx = i;
			break;
		}
	}
	size_t nextIdx = (curIdx + 1) % g_packs.size();
	SelectPack(g_packs[nextIdx].id);
}

bool ShaderManager::ReloadCurrent()
{
	if (!g_initialized) Initialize();
	RefreshPackList();
	ApplyToBackend();
	return g_lastError.empty();
}

void ShaderManager::ApplyToBackend()
{
	g_lastError.clear();

	if (!g_enabled)
	{
		RenderManager.ShaderClearCustomProgram();
		RenderManager.ShaderSetMode(C4J_SHADER_MODE_OFF);
		g_lastFrameParams.waterTint[3] = (float)C4J_SHADER_MODE_OFF;
		RenderManager.ShaderSetFrameParams(&g_lastFrameParams);
		return;
	}

	if (_wcsicmp(g_activePackId.c_str(), L"Default") == 0)
	{
		RenderManager.ShaderClearCustomProgram();
		RenderManager.ShaderSetMode(C4J_SHADER_MODE_DEFAULT);
		g_lastFrameParams.waterTint[3] = (float)C4J_SHADER_MODE_DEFAULT;
		RenderManager.ShaderSetFrameParams(&g_lastFrameParams);
		return;
	}

#ifdef _WINDOWS64
	const PackEntry *pack = FindPackById(g_activePackId);
	if (pack == NULL || pack->isBuiltIn)
	{
		g_activePackId = L"Default";
		RenderManager.ShaderClearCustomProgram();
		RenderManager.ShaderSetMode(C4J_SHADER_MODE_DEFAULT);
		g_lastFrameParams.waterTint[3] = (float)C4J_SHADER_MODE_DEFAULT;
		RenderManager.ShaderSetFrameParams(&g_lastFrameParams);
		return;
	}

	std::string hlslSrc, glslVert, glslFrag;
	std::vector<uint32_t> vkStd, vkCmp, vkPs;

	ReadFileToString(pack->folderPath + L"\\world_dx.hlsl", hlslSrc);
	ReadFileToString(pack->folderPath + L"\\world_gl.vert", glslVert);
	ReadFileToString(pack->folderPath + L"\\world_gl.frag", glslFrag);
	ReadFileToUint32Vec(pack->folderPath + L"\\world_vk_std.spv", vkStd);
	ReadFileToUint32Vec(pack->folderPath + L"\\world_vk_cmp.spv", vkCmp);
	ReadFileToUint32Vec(pack->folderPath + L"\\world_vk_ps.spv", vkPs);

	C4JShaderProgramDesc desc = {};
	desc.hlslSource        = hlslSrc.empty()  ? NULL : hlslSrc.c_str();
	desc.glslVertSource    = glslVert.empty() ? NULL : glslVert.c_str();
	desc.glslFragSource    = glslFrag.empty() ? NULL : glslFrag.c_str();
	desc.vkVertStdSpv      = vkStd.empty()    ? NULL : vkStd.data();
	desc.vkVertStdSpvBytes = vkStd.size() * sizeof(uint32_t);
	desc.vkVertCmpSpv      = vkCmp.empty()    ? NULL : vkCmp.data();
	desc.vkVertCmpSpvBytes = vkCmp.size() * sizeof(uint32_t);
	desc.vkFragSpv         = vkPs.empty()     ? NULL : vkPs.data();
	desc.vkFragSpvBytes    = vkPs.size() * sizeof(uint32_t);

	char errBuf[512] = { 0 };
	if (RenderManager.ShaderLoadCustomProgram(&desc, errBuf, sizeof(errBuf)))
	{
		RenderManager.ShaderSetMode(C4J_SHADER_MODE_CUSTOM);
		g_lastFrameParams.waterTint[3] = (float)C4J_SHADER_MODE_CUSTOM;
		RenderManager.ShaderSetFrameParams(&g_lastFrameParams);
	}
	else
	{
		/* Safe fallback to built-in Default shader if custom pack fails to compile or lacks backend file */
		if (errBuf[0] != '\0')
		{
			g_lastError.clear();
			for (const char *p = errBuf; *p; ++p) g_lastError.push_back((wchar_t)(unsigned char)*p);
		}
		else
		{
			g_lastError = L"Pack missing shader for active backend; using Default";
		}
		RenderManager.ShaderClearCustomProgram();
		RenderManager.ShaderSetMode(C4J_SHADER_MODE_DEFAULT);
		g_lastFrameParams.waterTint[3] = (float)C4J_SHADER_MODE_DEFAULT;
		RenderManager.ShaderSetFrameParams(&g_lastFrameParams);
	}
#endif
}

std::wstring ShaderManager::GetActivePackId()
{
	if (!g_initialized) Initialize();
	return g_activePackId;
}

std::wstring ShaderManager::GetActiveDisplayName()
{
	if (!g_initialized) Initialize();
	if (!g_enabled) return L"Off";
#ifdef _WINDOWS64
	const PackEntry *pack = FindPackById(g_activePackId);
	if (pack != NULL) return pack->displayName;
#endif
	return g_activePackId;
}

std::wstring ShaderManager::GetLastError()
{
	return g_lastError;
}

void ShaderManager::UpdateWorldFrameParams(const C4JShaderFrameParams &params)
{
	g_lastFrameParams = params;
	int mode = RenderManager.ShaderGetMode();
	g_lastFrameParams.waterTint[3] = (float)mode;
	RenderManager.ShaderSetFrameParams(&g_lastFrameParams);
}

void ShaderManager::SetInWorldPass(bool inWorld)
{
	const float want = inWorld ? 1.0f : 0.0f;
	if (g_lastFrameParams.worldMeta[3] == want)
	{
		return;
	}
	g_lastFrameParams.worldMeta[3] = want;
	int mode = RenderManager.ShaderGetMode();
	g_lastFrameParams.waterTint[3] = (float)mode;
	RenderManager.ShaderSetFrameParams(&g_lastFrameParams);
}
