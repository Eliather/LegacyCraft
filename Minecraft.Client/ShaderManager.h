#pragma once

#include <string>
#include <vector>
#include "Windows64/Render/ShaderTypes.h"

namespace ShaderManager
{
	struct PackEntry
	{
		std::wstring id;          /* "Default" or folder name under shaders/ */
		std::wstring displayName; /* Friendly label from shader.ini or folder name */
		std::wstring folderPath;  /* Empty for built-in Default */
		bool isBuiltIn;
	};

	/* Initialize from UserData_Info, scan shaders/ folder, and apply active shader to RenderManager */
	void Initialize();

	/* Rescan shaders/ folder next to the executable */
	void RefreshPackList();

	/* Access available shader packs (index 0 is always built-in "Default") */
	const std::vector<PackEntry> &GetAvailablePacks();

	/* Toggle shaders On / Off (persists to UserData_Info and updates backend immediately) */
	void SetEnabled(bool enabled);
	bool IsEnabled();
	bool ToggleEnabled();

	/* Select a shader pack by id ("Default" or folder name) */
	bool SelectPack(const std::wstring &packId);
	void CyclePack();

	/* Hot-reload the currently active shader pack from disk (F3+R) */
	bool ReloadCurrent();

	/* Re-apply current state to the active render backend */
	void ApplyToBackend();

	/* Current status for F3 overlay and UI */
	std::wstring GetActivePackId();
	std::wstring GetActiveDisplayName();
	std::wstring GetLastError();

	/* Per-frame scene uniforms helper */
	void UpdateWorldFrameParams(const C4JShaderFrameParams &params);
	void SetInWorldPass(bool inWorld);
}
