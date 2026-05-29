#pragma once

#include <vector>

class CConsoleMinecraftApp : public CMinecraftApp
{
public:
	CConsoleMinecraftApp();

	virtual void SetRichPresenceContext(int iPad, int contextId);

	virtual void StoreLaunchData();
	virtual void ExitGame();
	virtual void FatalLoadError();

	virtual void CaptureSaveThumbnail();
	virtual void GetSaveThumbnail(PBYTE*,DWORD*);
	virtual void ReleaseSaveThumbnail();
	virtual bool IsSaveThumbnailCaptureComplete();
	virtual bool ShouldCaptureSaveThumbnailFromWorldFrame(int iPad);
	virtual void CaptureSaveThumbnailFromWorldFrame(int iPad);
	virtual void GetScreenshot(int iPad,PBYTE *pbData,DWORD *pdwSize);

	virtual int LoadLocalTMSFile(WCHAR *wchTMSFile);
	virtual int LoadLocalTMSFile(WCHAR *wchTMSFile, eFileExtensionType eExt);

	virtual void FreeLocalTMSFiles(eTMSFileType eType);
	virtual int GetLocalTMSFileIndex(WCHAR *wchTMSFile,bool bFilenameIncludesExtension,eFileExtensionType eEXT=eFileExtensionType_PNG);

	// BANNED LEVEL LIST
	virtual void ReadBannedList(int iPad, eTMSAction action=(eTMSAction)0, bool bCallback=false) {}

	C4JStringTable *GetStringTable()																									{ return NULL;}

	// original code
	virtual void TemporaryCreateGameStart();

private:
	std::vector<BYTE> m_saveThumbnailData;
	bool m_saveThumbnailCapturePending;
	int m_saveThumbnailCaptureRetryCount;
};

extern CConsoleMinecraftApp app;
