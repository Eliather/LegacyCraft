#pragma once

#include <string>

namespace UserData_Info
{
	struct Data
	{
		std::wstring playerName;
		bool skinSlim;
		unsigned char languageId;
		unsigned int skinId;
		unsigned char gamma;
		unsigned char fov;
		unsigned char renderDistance;
		unsigned char resolutionMode;
		unsigned char rendererBackend;
		unsigned char vsyncEnabled;
	};

	void EnsureLoaded();
	void Save();

	const Data &GetData();
	std::wstring GetPlayerName();
	const char *GetPlayerNameAnsi();
	bool GetSkinSlim();
	bool GetModelType();
	unsigned char GetLanguageId();
	unsigned int GetSkinId();
	unsigned char GetGamma();
	unsigned char GetFov();
	unsigned char GetRenderDistance();
	unsigned char GetResolutionMode();
	unsigned char GetRendererBackend();
	bool GetVsyncEnabled();

	void SetPlayerName(const std::wstring &playerName);
	void SetSkinSlim(bool skinSlim);
	void SetModelType(bool modelType);
	void SetLanguageId(unsigned char languageId);
	void SetSkinId(unsigned int skinId);
	void SetGamma(unsigned char gamma);
	void SetFov(unsigned char fov);
	void SetRenderDistance(unsigned char renderDistance);
	void SetResolutionMode(unsigned char resolutionMode);
	void SetRendererBackend(unsigned char rendererBackend);
	void SetVsyncEnabled(bool vsyncEnabled);
}
