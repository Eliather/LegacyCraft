#include "stdafx.h"
#include "UserData_Info.h"

#include <cstdlib>

#include "..\Minecraft.World\Definitions.h"
#include "Common\Consoles_App.h"

namespace
{
	const wchar_t *kUserDataInfoFileName = L"UserData_Info.userdat";
	const wchar_t *kDefaultPlayerName = L"Steve";
	const unsigned int kDefaultSkinId = eDefaultSkins_Skin0;
	const unsigned char kDefaultGamma = 0;
	const unsigned char kDefaultFov = 70;
	const unsigned char kDefaultRenderDistance = 18;
	const unsigned char kDefaultResolutionMode = 3;
	const unsigned char kDefaultRendererBackend = 0; // 0 = DX11, 1 = OpenGL, 2 = Vulkan, 3 = DX12
	const unsigned char kDefaultVsyncEnabled = 0;
	const unsigned int kUserDataInfoVersion = 8;

#pragma pack(push, 1)
	struct UserDataInfoFile
	{
		char magic[4];
		unsigned int version;
		char playerName[17];
		unsigned char skinSlim;
		unsigned char languageId;
		unsigned char gamma;
		unsigned char fov;
		unsigned int skinId;
		unsigned char renderDistance;
		unsigned char resolutionMode;
		unsigned char rendererBackend;
		unsigned char vsyncEnabled;
	};
#pragma pack(pop)

	bool g_userDataLoaded = false;
	UserData_Info::Data g_userData = { L"Steve", false, MINECRAFT_LANGUAGE_SPANISH, kDefaultSkinId, kDefaultGamma, kDefaultFov, kDefaultRenderDistance, kDefaultResolutionMode, kDefaultRendererBackend, kDefaultVsyncEnabled };
	char g_userDataPlayerNameAnsi[17] = "Steve";

	unsigned char ClampFovValue(unsigned char fov)
	{
		if(fov < 30)
		{
			return 30;
		}
		if(fov > 120)
		{
			return 120;
		}
		return fov;
	}

	unsigned char ClampRenderDistanceValue(unsigned char renderDistance)
	{
		if(renderDistance < 2)
		{
			return 2;
		}
		if(renderDistance > 32)
		{
			return 32;
		}
		return renderDistance;
	}

	unsigned char ClampResolutionModeValue(unsigned char resolutionMode)
	{
		if(resolutionMode < 1 || resolutionMode > 4)
		{
			return kDefaultResolutionMode;
		}
		return resolutionMode;
	}

	unsigned char ClampRendererBackendValue(unsigned char rendererBackend)
	{
		if(rendererBackend > 3)
		{
			return kDefaultRendererBackend;
		}
		return rendererBackend;
	}

	unsigned char ClampVsyncValue(unsigned char vsyncEnabled)
	{
		return (vsyncEnabled != 0) ? 1 : 0;
	}

	unsigned char MigrateVersion6ResolutionModeValue(unsigned char resolutionMode)
	{
		switch(resolutionMode)
		{
		case 1:
			return 1;
		case 2:
			return 3;
		case 3:
			return 4;
		default:
			return kDefaultResolutionMode;
		}
	}

	unsigned char MigrateLegacyResolutionModeValue(unsigned char resolutionMode)
	{
		switch(resolutionMode)
		{
		case 1:
			return 1;
		case 4:
			return 3;
		case 5:
			return 4;
		default:
			return kDefaultResolutionMode;
		}
	}

	bool IsAllowedPlayerNameChar(wchar_t ch)
	{
		return ((ch >= L'0' && ch <= L'9') ||
				(ch >= L'A' && ch <= L'Z') ||
				(ch >= L'a' && ch <= L'z') ||
				ch == L'_');
	}

	std::wstring SanitisePlayerName(const std::wstring &playerName)
	{
		if(playerName.length() < 3 || playerName.length() > 16)
		{
			return kDefaultPlayerName;
		}

		for(size_t i = 0; i < playerName.length(); ++i)
		{
			if(!IsAllowedPlayerNameChar(playerName[i]))
			{
				return kDefaultPlayerName;
			}
		}

		return playerName;
	}

	std::wstring GetUserDataInfoPath()
	{
		wchar_t modulePath[MAX_PATH] = {0};
		if(GetModuleFileNameW(NULL, modulePath, MAX_PATH) == 0)
		{
			return kUserDataInfoFileName;
		}

		wchar_t *lastSlash = wcsrchr(modulePath, L'\\');
		if(lastSlash == NULL)
		{
			lastSlash = wcsrchr(modulePath, L'/');
		}

		if(lastSlash != NULL)
		{
			*(lastSlash + 1) = L'\0';
		}
		else
		{
			modulePath[0] = L'\0';
		}

		return std::wstring(modulePath) + kUserDataInfoFileName;
	}

	void SyncPlayerNameAnsi()
	{
		memset(g_userDataPlayerNameAnsi, 0, sizeof(g_userDataPlayerNameAnsi));

		const size_t copyLength = min((size_t)16, g_userData.playerName.length());
		for(size_t i = 0; i < copyLength; ++i)
		{
			g_userDataPlayerNameAnsi[i] = (char)g_userData.playerName[i];
		}
	}

	bool SkinExists(unsigned int skinId)
	{
		if(skinId == kDefaultSkinId)
		{
			return true;
		}

		wstring skinPath = app.getSkinPathFromId((DWORD)skinId);
		return !skinPath.empty();
	}

	bool ValidateData(bool validateSkin)
	{
		bool changed = false;

		const std::wstring validatedName = SanitisePlayerName(g_userData.playerName);
		if(validatedName != g_userData.playerName)
		{
			g_userData.playerName = validatedName;
			changed = true;
		}

		if(validateSkin && !SkinExists(g_userData.skinId))
		{
			g_userData.skinId = kDefaultSkinId;
			g_userData.skinSlim = false;
			changed = true;
		}

		const unsigned char validatedFov = ClampFovValue(g_userData.fov);
		if(validatedFov != g_userData.fov)
		{
			g_userData.fov = validatedFov;
			changed = true;
		}

		const unsigned char validatedRenderDistance = ClampRenderDistanceValue(g_userData.renderDistance);
		if(validatedRenderDistance != g_userData.renderDistance)
		{
			g_userData.renderDistance = validatedRenderDistance;
			changed = true;
		}

		const unsigned char validatedResolutionMode = ClampResolutionModeValue(g_userData.resolutionMode);
		if(validatedResolutionMode != g_userData.resolutionMode)
		{
			g_userData.resolutionMode = validatedResolutionMode;
			changed = true;
		}

		const unsigned char validatedBackend = ClampRendererBackendValue(g_userData.rendererBackend);
		if(validatedBackend != g_userData.rendererBackend)
		{
			g_userData.rendererBackend = validatedBackend;
			changed = true;
		}

		const unsigned char validatedVsync = ClampVsyncValue(g_userData.vsyncEnabled);
		if(validatedVsync != g_userData.vsyncEnabled)
		{
			g_userData.vsyncEnabled = validatedVsync;
			changed = true;
		}

		SyncPlayerNameAnsi();
		return changed;
	}

	bool ReadBinaryFile(UserDataInfoFile &fileOut)
	{
		const std::wstring path = GetUserDataInfoPath();
		HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if(hFile == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		LARGE_INTEGER fileSize = {0};
		if(!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < 8 || fileSize.QuadPart > sizeof(UserDataInfoFile))
		{
			CloseHandle(hFile);
			return false;
		}

		memset(&fileOut, 0, sizeof(fileOut));

		const DWORD bytesToRead = (DWORD)fileSize.QuadPart;
		DWORD bytesRead = 0;
		if(!ReadFile(hFile, &fileOut, bytesToRead, &bytesRead, NULL))
		{
			CloseHandle(hFile);
			return false;
		}

		CloseHandle(hFile);
		return (bytesRead == bytesToRead);
	}

	void ParseBinaryFile(const UserDataInfoFile &fileData)
	{
		UserData_Info::Data parsed = { kDefaultPlayerName, false, MINECRAFT_LANGUAGE_SPANISH, kDefaultSkinId, kDefaultGamma, kDefaultFov, kDefaultRenderDistance, kDefaultResolutionMode, kDefaultRendererBackend, kDefaultVsyncEnabled };

		if(memcmp(fileData.magic, "UDAT", 4) != 0 ||
			(fileData.version != 1 && fileData.version != 2 && fileData.version != 3 && fileData.version != 4 && fileData.version != 5 && fileData.version != 6 && fileData.version != 7 && fileData.version != kUserDataInfoVersion))
		{
			g_userData = parsed;
			return;
		}

		parsed.playerName.clear();
		for(size_t i = 0; i < 16 && fileData.playerName[i] != '\0'; ++i)
		{
			parsed.playerName.push_back((wchar_t)(unsigned char)fileData.playerName[i]);
		}
		parsed.skinSlim = (fileData.skinSlim != 0);
		
		if (fileData.version >= 2)
		{
			parsed.languageId = fileData.languageId;
		}
		else
		{
			parsed.languageId = MINECRAFT_LANGUAGE_SPANISH; // Default for v1 files
		}

		if(fileData.version >= 3)
		{
			parsed.gamma = fileData.gamma;
			parsed.fov = fileData.fov;
		}
		else
		{
			parsed.gamma = kDefaultGamma;
			parsed.fov = kDefaultFov;
		}

		if(fileData.version >= 4)
		{
			parsed.renderDistance = fileData.renderDistance;
		}
		else
		{
			parsed.renderDistance = kDefaultRenderDistance;
		}

		if(fileData.version >= 7)
		{
			parsed.resolutionMode = fileData.resolutionMode;
		}
		else if(fileData.version == 6)
		{
			parsed.resolutionMode = MigrateVersion6ResolutionModeValue(fileData.resolutionMode);
		}
		else if(fileData.version == 5)
		{
			parsed.resolutionMode = MigrateLegacyResolutionModeValue(fileData.resolutionMode);
		}
		else
		{
			parsed.resolutionMode = kDefaultResolutionMode;
		}

		parsed.skinId = fileData.skinId;

		if(fileData.version >= 8)
		{
			parsed.rendererBackend = ClampRendererBackendValue(fileData.rendererBackend);
			parsed.vsyncEnabled = ClampVsyncValue(fileData.vsyncEnabled);
		}
		else
		{
			parsed.rendererBackend = kDefaultRendererBackend;
			parsed.vsyncEnabled = kDefaultVsyncEnabled;
		}

		g_userData = parsed;
	}
}

void UserData_Info::EnsureLoaded()
{
	if(g_userDataLoaded)
	{
		return;
	}

	g_userDataLoaded = true;

	UserDataInfoFile fileData;
	bool fileLoaded = ReadBinaryFile(fileData);
	if(fileLoaded)
	{
		ParseBinaryFile(fileData);
	}
	else
	{
		g_userData.playerName = kDefaultPlayerName;
		g_userData.skinSlim = false;
		g_userData.languageId = MINECRAFT_LANGUAGE_SPANISH;
		g_userData.skinId = kDefaultSkinId;
		g_userData.gamma = kDefaultGamma;
		g_userData.fov = kDefaultFov;
		g_userData.renderDistance = kDefaultRenderDistance;
		g_userData.resolutionMode = kDefaultResolutionMode;
		g_userData.rendererBackend = kDefaultRendererBackend;
		g_userData.vsyncEnabled = kDefaultVsyncEnabled;
	}

	if(!fileLoaded || ValidateData(false))
	{
		Save();
	}
}

void UserData_Info::Save()
{
	EnsureLoaded();
	ValidateData(false);

	UserDataInfoFile fileData;
	memset(&fileData, 0, sizeof(fileData));
	memcpy(fileData.magic, "UDAT", 4);
	fileData.version = kUserDataInfoVersion;
	memcpy(fileData.playerName, g_userDataPlayerNameAnsi, sizeof(fileData.playerName) - 1);
	fileData.skinSlim = g_userData.skinSlim ? 1 : 0;
	fileData.languageId = g_userData.languageId;
	fileData.gamma = g_userData.gamma;
	fileData.fov = g_userData.fov;
	fileData.renderDistance = g_userData.renderDistance;
	fileData.resolutionMode = g_userData.resolutionMode;
	fileData.skinId = g_userData.skinId;
	fileData.rendererBackend = g_userData.rendererBackend;
	fileData.vsyncEnabled = g_userData.vsyncEnabled;

	const std::wstring path = GetUserDataInfoPath();

	HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
	{
		return;
	}

	DWORD bytesWritten = 0;
	WriteFile(hFile, &fileData, sizeof(fileData), &bytesWritten, NULL);
	CloseHandle(hFile);
}

const UserData_Info::Data &UserData_Info::GetData()
{
	EnsureLoaded();
	return g_userData;
}

std::wstring UserData_Info::GetPlayerName()
{
	EnsureLoaded();
	return g_userData.playerName;
}

const char *UserData_Info::GetPlayerNameAnsi()
{
	EnsureLoaded();
	return g_userDataPlayerNameAnsi;
}

bool UserData_Info::GetSkinSlim()
{
	EnsureLoaded();
	return g_userData.skinSlim;
}

bool UserData_Info::GetModelType()
{
	return GetSkinSlim();
}

unsigned char UserData_Info::GetLanguageId()
{
	EnsureLoaded();
	return g_userData.languageId;
}

unsigned char UserData_Info::GetGamma()
{
	EnsureLoaded();
	return g_userData.gamma;
}

unsigned char UserData_Info::GetFov()
{
	EnsureLoaded();
	return g_userData.fov;
}

unsigned char UserData_Info::GetRenderDistance()
{
	EnsureLoaded();
	return g_userData.renderDistance;
}

unsigned char UserData_Info::GetResolutionMode()
{
	EnsureLoaded();
	return g_userData.resolutionMode;
}

unsigned char UserData_Info::GetRendererBackend()
{
	EnsureLoaded();
	return g_userData.rendererBackend;
}

bool UserData_Info::GetVsyncEnabled()
{
	EnsureLoaded();
	return (g_userData.vsyncEnabled != 0);
}

unsigned int UserData_Info::GetSkinId()
{
	EnsureLoaded();
	if(ValidateData(true))
	{
		Save();
	}
	return g_userData.skinId;
}

void UserData_Info::SetPlayerName(const std::wstring &playerName)
{
	EnsureLoaded();
	g_userData.playerName = playerName;
	Save();
}

void UserData_Info::SetSkinSlim(bool skinSlim)
{
	EnsureLoaded();
	g_userData.skinSlim = skinSlim;
	Save();
}

void UserData_Info::SetModelType(bool modelType)
{
	SetSkinSlim(modelType);
}

void UserData_Info::SetLanguageId(unsigned char languageId)
{
	EnsureLoaded();
	g_userData.languageId = languageId;
	Save();
}

void UserData_Info::SetSkinId(unsigned int skinId)
{
	EnsureLoaded();
	g_userData.skinId = skinId;
	ValidateData(true);
	Save();
}

void UserData_Info::SetGamma(unsigned char gamma)
{
	EnsureLoaded();
	g_userData.gamma = gamma;
	Save();
}

void UserData_Info::SetFov(unsigned char fov)
{
	EnsureLoaded();
	g_userData.fov = ClampFovValue(fov);
	Save();
}

void UserData_Info::SetRenderDistance(unsigned char renderDistance)
{
	EnsureLoaded();
	g_userData.renderDistance = ClampRenderDistanceValue(renderDistance);
	Save();
}

void UserData_Info::SetResolutionMode(unsigned char resolutionMode)
{
	EnsureLoaded();
	g_userData.resolutionMode = ClampResolutionModeValue(resolutionMode);
	Save();
}

void UserData_Info::SetRendererBackend(unsigned char rendererBackend)
{
	EnsureLoaded();
	g_userData.rendererBackend = ClampRendererBackendValue(rendererBackend);
	Save();
}

void UserData_Info::SetVsyncEnabled(bool vsyncEnabled)
{
	EnsureLoaded();
	g_userData.vsyncEnabled = ClampVsyncValue(vsyncEnabled ? 1 : 0);
	Save();
}
