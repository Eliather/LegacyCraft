#include "stdafx.h"
#include "UI.h"
#include "..\..\..\Minecraft.World\StringHelpers.h"
#include "..\..\..\Minecraft.World\File.h"
#include "UITTFFont.h"

UITTFFont::UITTFFont(const string &path, const string &fontName, S32 fallbackCharacter)
	: pbData(NULL)
	, m_fontName(fontName)
{
	app.DebugPrintf("UITTFFont opening %s\n",path.c_str());

#ifdef _UNICODE
	wstring wPath = convStringToWstring(path);
	HANDLE file = CreateFile(wPath.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#else
	HANDLE file = CreateFile(path.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#endif
	if( file == INVALID_HANDLE_VALUE )
	{
		DWORD error = GetLastError();
		app.DebugPrintf("Failed to open TTF file with error code %d (%x)\n", error, error);
		assert(false);
	}

	DWORD dwHigh=0;
	DWORD dwFileSize = GetFileSize(file,&dwHigh);

	if(dwFileSize!=0)
	{
		DWORD bytesRead;

		pbData =  (PBYTE) new BYTE[dwFileSize];
		BOOL bSuccess = ReadFile(file,pbData,dwFileSize,&bytesRead,NULL);
		if(bSuccess==FALSE)
		{
			app.FatalLoadError();
		}
		CloseHandle(file);

		IggyFontInstallTruetypeUTF8 ( (void *)pbData, IGGY_TTC_INDEX_none, m_fontName.c_str(), -1, IGGY_FONTFLAG_none );
		IggyFontInstallTruetypeFallbackCodepointUTF8( m_fontName.c_str(), -1, IGGY_FONTFLAG_none, fallbackCharacter );
	}
}

UITTFFont::~UITTFFont()
{
}
