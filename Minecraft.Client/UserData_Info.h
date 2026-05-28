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

	void SetPlayerName(const std::wstring &playerName);
	void SetSkinSlim(bool skinSlim);
	void SetModelType(bool modelType);
	void SetLanguageId(unsigned char languageId);
	void SetSkinId(unsigned int skinId);
	void SetGamma(unsigned char gamma);
	void SetFov(unsigned char fov);
	void SetRenderDistance(unsigned char renderDistance);
}
