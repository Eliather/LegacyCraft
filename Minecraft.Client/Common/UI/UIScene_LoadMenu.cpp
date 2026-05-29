#include "stdafx.h"
#include "UI.h"
#include "UIScene_LoadMenu.h"
#include "..\..\Minecraft.h"
#include "..\..\TexturePackRepository.h"
#include "..\..\Options.h"
#include "..\..\MinecraftServer.h"
#include "..\..\..\Minecraft.World\ConsoleSaveFileOriginal.h"
#include "..\..\..\Minecraft.World\ConsoleSaveFileSplit.h"
#include "..\..\..\Minecraft.World\FileInputStream.h"
#include "..\..\..\Minecraft.World\LevelData.h"
#include "..\..\..\Minecraft.World\LevelSettings.h"
#include "..\..\..\Minecraft.World\StringHelpers.h"
#include "..\..\..\Minecraft.World\DirectoryLevelStorageSource.h"
#include "..\..\DLCTexturePack.h"
#ifdef _WINDOWS64
#include "..\zlib\zlib.h"
#endif

#define GAME_CREATE_ONLINE_TIMER_ID 0
#define GAME_CREATE_ONLINE_TIMER_TIME 100
// 4J-PB - Only Xbox will not have trial DLC patched into the game
#ifdef _XBOX
#define CHECKFORAVAILABLETEXTUREPACKS_TIMER_ID 1
#define CHECKFORAVAILABLETEXTUREPACKS_TIMER_TIME 50
#endif

#ifdef _WINDOWS64
namespace
{
	const DWORD WINDOWS64_LOAD_MENU_ICON_SIZE = 96;
	static const BYTE WINDOWS64_PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

	void AppendBigEndianUInt32(std::vector<unsigned char> &buffer, DWORD value)
	{
		buffer.push_back((BYTE)((value >> 24) & 0xFF));
		buffer.push_back((BYTE)((value >> 16) & 0xFF));
		buffer.push_back((BYTE)((value >> 8) & 0xFF));
		buffer.push_back((BYTE)(value & 0xFF));
	}

	void AppendPngChunk(std::vector<unsigned char> &pngData, const char chunkType[4], const BYTE *chunkData, DWORD chunkSize)
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

	bool EncodeArgbPixelsAsPng(const int *argbPixels, DWORD width, DWORD height, std::vector<unsigned char> &pngData)
	{
		pngData.clear();
		if(argbPixels == NULL || width == 0 || height == 0)
		{
			return false;
		}

		const DWORD rowBytes = width * 4;
		const size_t filteredRowBytes = (size_t)rowBytes + 1;
		std::vector<unsigned char> filteredPixels(filteredRowBytes * height);

		for(DWORD y = 0; y < height; ++y)
		{
			unsigned char *destRow = &filteredPixels[filteredRowBytes * y];
			destRow[0] = 0;
			for(DWORD x = 0; x < width; ++x)
			{
				const unsigned int argb = (unsigned int)argbPixels[(y * width) + x];
				unsigned char *destPixel = destRow + 1 + (x * 4);
				destPixel[0] = (BYTE)((argb >> 16) & 0xFF);
				destPixel[1] = (BYTE)((argb >> 8) & 0xFF);
				destPixel[2] = (BYTE)(argb & 0xFF);
				destPixel[3] = (BYTE)((argb >> 24) & 0xFF);
			}
		}

		uLongf compressedSize = compressBound((uLong)filteredPixels.size());
		std::vector<unsigned char> compressedPixels((size_t)compressedSize);
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

	int SampleArgbNearest(const int *argbPixels, DWORD width, DWORD height, DWORD cropLeft, DWORD cropTop, DWORD cropSize, DWORD x, DWORD y)
	{
		DWORD sourceX = cropLeft + ((x * cropSize) / WINDOWS64_LOAD_MENU_ICON_SIZE);
		DWORD sourceY = cropTop + ((y * cropSize) / WINDOWS64_LOAD_MENU_ICON_SIZE);
		if(sourceX >= width) sourceX = width - 1;
		if(sourceY >= height) sourceY = height - 1;

		unsigned int argb = (unsigned int)argbPixels[(sourceY * width) + sourceX];
		if(((argb >> 24) & 0xFF) <= 8 && (argb & 0x00FFFFFF) != 0)
		{
			argb |= 0xFF000000;
		}
		return (int)argb;
	}

	bool BuildLoadMenuIconTexture(const unsigned char *rawData, DWORD rawDataSize, std::vector<unsigned char> &normalizedPngData)
	{
		normalizedPngData.clear();
		if(rawData == NULL || rawDataSize == 0)
		{
			return false;
		}

		D3DXIMAGE_INFO imageInfo;
		ZeroMemory(&imageInfo, sizeof(imageInfo));
		int *decodedPixels = NULL;
		const HRESULT hr = RenderManager.LoadTextureData((BYTE *)rawData, rawDataSize, &imageInfo, &decodedPixels);
		if(hr != ERROR_SUCCESS || decodedPixels == NULL || imageInfo.Width <= 0 || imageInfo.Height <= 0)
		{
			delete [] decodedPixels;
			return false;
		}

		const DWORD sourceWidth = (DWORD)imageInfo.Width;
		const DWORD sourceHeight = (DWORD)imageInfo.Height;
		const DWORD cropSize = sourceWidth < sourceHeight ? sourceWidth : sourceHeight;
		const DWORD cropLeft = (sourceWidth - cropSize) / 2;
		const DWORD cropTop = (sourceHeight - cropSize) / 2;

		std::vector<int> resizedArgb(WINDOWS64_LOAD_MENU_ICON_SIZE * WINDOWS64_LOAD_MENU_ICON_SIZE);
		for(DWORD y = 0; y < WINDOWS64_LOAD_MENU_ICON_SIZE; ++y)
		{
			for(DWORD x = 0; x < WINDOWS64_LOAD_MENU_ICON_SIZE; ++x)
			{
				resizedArgb[(y * WINDOWS64_LOAD_MENU_ICON_SIZE) + x] = SampleArgbNearest(decodedPixels, sourceWidth, sourceHeight, cropLeft, cropTop, cropSize, x, y);
			}
		}

		delete [] decodedPixels;
		return EncodeArgbPixelsAsPng(&resizedArgb[0], WINDOWS64_LOAD_MENU_ICON_SIZE, WINDOWS64_LOAD_MENU_ICON_SIZE, normalizedPngData);
	}

	std::wstring Utf8ToWideString(const char *value)
	{
		if(value == NULL || value[0] == 0)
		{
			return std::wstring();
		}

		const int wideLength = MultiByteToWideChar(CP_UTF8, 0, value, -1, NULL, 0);
		if(wideLength <= 1)
		{
			return convStringToWstring(value);
		}

		std::wstring wideValue(wideLength, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, value, -1, &wideValue[0], wideLength);
		wideValue.resize(wideLength - 1);
		return wideValue;
	}

	std::string WideStringToUtf8(const std::wstring &value)
	{
		if(value.empty())
		{
			return std::string();
		}

		const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, NULL, 0, NULL, NULL);
		if(utf8Length <= 1)
		{
			return std::string();
		}

		std::string utf8Value(utf8Length, '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &utf8Value[0], utf8Length, NULL, NULL);
		utf8Value.resize(utf8Length - 1);
		return utf8Value;
	}

	bool IsSafeWindows64DirectSaveId(const char *saveId)
	{
		if(saveId == NULL || saveId[0] == 0)
		{
			return false;
		}

		for(const char *ptr = saveId; *ptr != 0; ++ptr)
		{
			const char ch = *ptr;
			const bool isLetter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
			const bool isDigit = (ch >= '0' && ch <= '9');
			const bool isAllowedPunctuation = (ch == '-');
			if(!isLetter && !isDigit && !isAllowedPunctuation)
			{
				return false;
			}
		}

		return true;
	}

	bool TryDeleteWindows64DirectSave(const char *saveId)
	{
		if(!IsSafeWindows64DirectSaveId(saveId))
		{
			return false;
		}

		File storageRoot(L"Windows64\\GameHDD");
		File storageDir(storageRoot, filenametowstring(saveId));
		if(!storageDir.exists() || !storageDir.isDirectory())
		{
			return false;
		}

		DirectoryLevelStorageSource storageSource(storageRoot);
		storageSource.deleteLevel(filenametowstring(saveId));
		return !storageDir.exists();
	}

	bool TryResolveWindows64DirectSaveTitleFromBytesUnsafe(const std::wstring &saveName, void *saveData, DWORD fileSize, std::string &resolvedTitle)
	{
		resolvedTitle.clear();
		if(saveData == NULL || fileSize == 0)
		{
			return false;
		}

		DirectoryLevelStorageSource levelStorageSource(File(L"."));
		LevelData *levelData = NULL;

		{
			ConsoleSaveFileOriginal saveFile(saveName, saveData, fileSize, false, SAVE_FILE_PLATFORM_LOCAL);
			levelData = levelStorageSource.getDataTagFor(&saveFile, L"");

#ifdef SPLIT_SAVES
			if(levelData == NULL)
			{
				ConsoleSaveFileSplit splitSaveFile(&saveFile);
				levelData = levelStorageSource.getDataTagFor(&splitSaveFile, L"");
			}
#endif
		}

		if(levelData != NULL)
		{
			const std::wstring levelName = trimString(levelData->getLevelName());
			delete levelData;

			if(!levelName.empty())
			{
				resolvedTitle = WideStringToUtf8(levelName);
				return !resolvedTitle.empty();
			}
		}

		return false;
	}

	bool TryResolveWindows64DirectSaveTitleFromBytes(const std::wstring &saveName, void *saveData, DWORD fileSize, std::string &resolvedTitle)
	{
		bool success = false;
		__try
		{
			success = TryResolveWindows64DirectSaveTitleFromBytesUnsafe(saveName, saveData, fileSize, resolvedTitle);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			success = false;
		}

		return success;
	}
}
#endif

int UIScene_LoadMenu::m_iDifficultyTitleSettingA[4]=
{
	IDS_DIFFICULTY_TITLE_PEACEFUL,
	IDS_DIFFICULTY_TITLE_EASY,
	IDS_DIFFICULTY_TITLE_NORMAL,
	IDS_DIFFICULTY_TITLE_HARD
};

int UIScene_LoadMenu::LoadSaveDataThumbnailReturned(LPVOID lpParam,PBYTE pbThumbnail,DWORD dwThumbnailBytes)
{
	UIScene_LoadMenu *pClass= (UIScene_LoadMenu *)lpParam;

	app.DebugPrintf("Received data for a thumbnail\n");

	if(pbThumbnail && dwThumbnailBytes)
	{
#ifndef _WINDOWS64
		pClass->registerSubstitutionTexture(pClass->m_thumbnailName,pbThumbnail,dwThumbnailBytes);

		pClass->m_pbThumbnailData = pbThumbnail;
		pClass->m_uiThumbnailSize = dwThumbnailBytes;
#else
		if(pClass->m_bOwnsThumbnailData && pClass->m_pbThumbnailData != NULL)
		{
			delete [] pClass->m_pbThumbnailData;
		}

		pClass->m_uiThumbnailSize = dwThumbnailBytes;
		pClass->m_pbThumbnailData = new BYTE[pClass->m_uiThumbnailSize];
		memcpy(pClass->m_pbThumbnailData, pbThumbnail, pClass->m_uiThumbnailSize);
		pClass->m_bOwnsThumbnailData = true;
#endif
		pClass->m_bSaveThumbnailReady = true;
	}
	else
	{
		app.DebugPrintf("Thumbnail data is NULL, or has size 0\n");
		pClass->m_bThumbnailGetFailed = true;
	}
	pClass->m_bRetrievingSaveThumbnail = false;

	return 0;
}

UIScene_LoadMenu::UIScene_LoadMenu(int iPad, void *initData, UILayer *parentLayer) : IUIScene_StartGame(iPad, parentLayer)
{
	// Setup all the Iggy references we need for this scene
	initialiseMovie();

	LoadMenuInitData *params = (LoadMenuInitData *)initData;

	//m_labelGameName.init(app.GetString(IDS_WORLD_NAME));
	m_labelSeed.init(L"");
	m_labelCreatedMode.init(app.GetString(IDS_CREATED_IN_SURVIVAL));

	m_buttonGamemode.init(app.GetString(IDS_GAMEMODE_SURVIVAL),eControl_GameMode);
	m_buttonMoreOptions.init(app.GetString(IDS_MORE_OPTIONS),eControl_MoreOptions);
	m_buttonLoadWorld.init(app.GetString(IDS_LOAD),eControl_LoadWorld);
	m_texturePackList.init(app.GetString(IDS_DLC_MENU_TEXTUREPACKS), eControl_TexturePackList);

	m_labelTexturePackName.init(L"");
	m_labelTexturePackDescription.init(L"");

	m_CurrentDifficulty=app.GetGameSettings(m_iPad,eGameSetting_Difficulty);
	WCHAR TempString[256];
	swprintf( (WCHAR *)TempString, 256, L"%ls: %ls", app.GetString( IDS_SLIDER_DIFFICULTY ),app.GetString(m_iDifficultyTitleSettingA[app.GetGameSettings(m_iPad,eGameSetting_Difficulty)]));	
	m_sliderDifficulty.init(TempString,eControl_Difficulty,0,3,app.GetGameSettings(m_iPad,eGameSetting_Difficulty));

	m_MoreOptionsParams.bGenerateOptions=FALSE;
	m_MoreOptionsParams.bPVP = TRUE;
	m_MoreOptionsParams.bTrust = TRUE;
	m_MoreOptionsParams.bFireSpreads = TRUE;
	m_MoreOptionsParams.bHostPrivileges = FALSE;
	m_MoreOptionsParams.bTNT = TRUE;
	m_MoreOptionsParams.iPad = iPad;

	m_iSaveGameInfoIndex=params->iSaveGameInfoIndex;
	m_levelGen = params->levelGen;

	m_bGameModeSurvival=true;
	m_bHasBeenInCreative = false;

	m_bSaveThumbnailReady = false;
	m_bRetrievingSaveThumbnail = true;
	m_bShowTimer = false;
	m_pDLCPack = NULL;
	m_bAvailableTexturePacksChecked=false;
	m_bRequestQuadrantSignin = false;
	m_iTexturePacksNotInstalled=0;
	m_bRebuildTouchBoxes = false;
	m_bThumbnailGetFailed = false;
	m_bIsCorrupt = false;
	m_seed = 0;
	m_pbThumbnailData = NULL;
	m_uiThumbnailSize = 0;
	m_bOwnsThumbnailData = false;
#ifdef _WINDOWS64
	m_bWindows64DirectDiskSave = false;
	ZeroMemory(m_windows64DirectSaveId, sizeof(m_windows64DirectSaveId));
	ZeroMemory(m_windows64DirectSaveName, sizeof(m_windows64DirectSaveName));
#endif

	m_bMultiplayerAllowed = ProfileManager.IsSignedInLive( m_iPad ) && ProfileManager.AllowedToPlayMultiplayer(m_iPad);
	// 4J-PB - read the settings for the online flag. We'll only save this setting if the user changed it.
	bool bGameSetting_Online=(app.GetGameSettings(m_iPad,eGameSetting_Online)!=0);
	m_MoreOptionsParams.bOnlineSettingChangedBySystem=false;

	// Set the text for friends of friends, and default to on
	if( m_bMultiplayerAllowed)
	{
		m_MoreOptionsParams.bOnlineGame = bGameSetting_Online?TRUE:FALSE;
		if(bGameSetting_Online)
		{
			m_MoreOptionsParams.bInviteOnly = (app.GetGameSettings(m_iPad,eGameSetting_InviteOnly)!=0)?TRUE:FALSE;
			m_MoreOptionsParams.bAllowFriendsOfFriends = (app.GetGameSettings(m_iPad,eGameSetting_FriendsOfFriends)!=0)?TRUE:FALSE;
		}
		else
		{
			m_MoreOptionsParams.bInviteOnly = FALSE;
			m_MoreOptionsParams.bAllowFriendsOfFriends = FALSE;
		}
	}
	else
	{
		m_MoreOptionsParams.bOnlineGame = FALSE;
		m_MoreOptionsParams.bInviteOnly = FALSE;
		m_MoreOptionsParams.bAllowFriendsOfFriends = FALSE;
		if(bGameSetting_Online)
		{
			// The profile settings say Online, but either the player is offline, or they are not allowed to play online
			m_MoreOptionsParams.bOnlineSettingChangedBySystem=true;
		}	
	}

	
#if defined _XBOX_ONE || defined _WINDOWS64
	if(getSceneResolution() == eSceneResolution_1080)
	{
		// Set up online game checkbox
		bool bOnlineGame = m_MoreOptionsParams.bOnlineGame;
		m_checkboxOnline.SetEnable(true);

		// 4J-PB - to stop an offline game being able to select the online flag
		if(ProfileManager.IsSignedInLive(m_iPad) == false)
		{
			m_checkboxOnline.SetEnable(false);
		}

		if(m_MoreOptionsParams.bOnlineSettingChangedBySystem)
		{
			m_checkboxOnline.SetEnable(false);
			bOnlineGame = false;
		}

		m_checkboxOnline.init(app.GetString(IDS_ONLINE_GAME), eControl_OnlineGame, bOnlineGame);
	}
#endif

	// Level gen
	if(m_levelGen)
	{
		m_labelGameName.init(m_levelGen->getDisplayName());
		if(m_levelGen->requiresTexturePack())
		{
			m_MoreOptionsParams.dwTexturePack = m_levelGen->getRequiredTexturePackId();

			m_texturePackList.setEnabled(false);


			// retrieve the save icon from the texture pack, if there is one
			TexturePack *tp = Minecraft::GetInstance()->skins->getTexturePackById(m_MoreOptionsParams.dwTexturePack);
			DWORD dwImageBytes;
			PBYTE pbImageData = tp->getPackIcon(dwImageBytes);

			if(dwImageBytes > 0 && pbImageData)
			{
				wchar_t textureName[64];
				swprintf(textureName,64,L"loadsave");				
				registerSubstitutionTexture(textureName,pbImageData,dwImageBytes);
				m_bitmapIcon.setTextureName( textureName );
			}
		}
		// Set this level as created in creative mode, so that people can't use the themed worlds as an easy way to get achievements
		m_bHasBeenInCreative = true;
		m_labelCreatedMode.setLabel( app.GetString(IDS_CREATED_IN_CREATIVE) );
	}
	else
	{
#ifdef _WINDOWS64
		if(params->saveDetails != NULL)
		{
			m_labelGameName.init(params->saveDetails->UTF8SaveName);
			m_bRetrievingSaveThumbnail = false;
			m_thumbnailName = Utf8ToWideString(params->saveDetails->UTF8SaveFilename);
			app.SetPreparedSaveIdentity(Utf8ToWideString(params->saveDetails->UTF8SaveName).c_str(), params->saveDetails->UTF8SaveFilename);
			if(m_iSaveGameInfoIndex < 0)
			{
				m_bWindows64DirectDiskSave = true;
				strncpy(m_windows64DirectSaveId, params->saveDetails->UTF8SaveFilename, MAX_SAVEFILENAME_LENGTH - 1);
				strncpy(m_windows64DirectSaveName, params->saveDetails->UTF8SaveName, 127);
			}

			if(params->saveDetails->pbThumbnailData != NULL && params->saveDetails->dwThumbnailSize > 0)
			{
				m_uiThumbnailSize = params->saveDetails->dwThumbnailSize;
				m_pbThumbnailData = new BYTE[m_uiThumbnailSize];
				memcpy(m_pbThumbnailData, params->saveDetails->pbThumbnailData, m_uiThumbnailSize);
				m_bOwnsThumbnailData = true;
				m_bSaveThumbnailReady = true;
			}
		}
#else
#ifdef _DURANGO
		// convert to utf16
		uint16_t u16Message[MAX_SAVEFILENAME_LENGTH];
		size_t srclen,dstlen;
		srclen=MAX_SAVEFILENAME_LENGTH;
		dstlen=MAX_SAVEFILENAME_LENGTH;
		// Already utf16 on durango
		memcpy(u16Message,params->saveDetails->UTF16SaveFilename, MAX_SAVEFILENAME_LENGTH);
		m_thumbnailName = (wchar_t *)u16Message;
		if(params->saveDetails->pbThumbnailData)
		{
			m_pbThumbnailData = params->saveDetails->pbThumbnailData;
			m_uiThumbnailSize = params->saveDetails->dwThumbnailSize;
			m_bSaveThumbnailReady = true;
		}
		else
		{
			app.DebugPrintf("Requesting the save thumbnail\n");
			// set the save to load
			PSAVE_DETAILS pSaveDetails=StorageManager.ReturnSavesInfo();
#ifdef _DURANGO
			// On Durango, we have an extra flag possible with LoadSaveDataThumbnail, which if true will force the loading of this thumbnail even if the save data isn't sync'd from
			// the cloud at this stage. This could mean that there could be a pretty large delay before the callback happens, in this case.
			C4JStorage::ESaveGameState eLoadStatus=StorageManager.LoadSaveDataThumbnail(&pSaveDetails->SaveInfoA[(int)m_iSaveGameInfoIndex],&LoadSaveDataThumbnailReturned,this,true);
#else
			C4JStorage::ESaveGameState eLoadStatus=StorageManager.LoadSaveDataThumbnail(&pSaveDetails->SaveInfoA[(int)m_iSaveGameInfoIndex],&LoadSaveDataThumbnailReturned,this);
#endif
			m_bShowTimer = true;
		}
#if defined(_DURANGO) 
		m_labelGameName.init(params->saveDetails->UTF16SaveName);
#else

		m_labelGameName.init(params->saveDetails->UTF8SaveName);
#endif
#endif
#endif
	}

	TelemetryManager->RecordMenuShown(m_iPad, eUIScene_LoadMenu, 0);
	m_iTexturePacksNotInstalled=0;

	// block input if we're waiting for DLC to install, and wipe the saves list. The end of dlc mounting custom message will fill the list again
	if(app.StartInstallDLCProcess(m_iPad)==true)
	{
		// not doing a mount, so enable input
		m_bIgnoreInput=true;
	}
	else
	{
		m_bIgnoreInput = false;

		Minecraft *pMinecraft = Minecraft::GetInstance();
		int texturePacksCount = pMinecraft->skins->getTexturePackCount();
		for(unsigned int i = 0; i < texturePacksCount; ++i)
		{
			TexturePack *tp = pMinecraft->skins->getTexturePackByIndex(i);

			DWORD dwImageBytes;
			PBYTE pbImageData = tp->getPackIcon(dwImageBytes);

			if(dwImageBytes > 0 && pbImageData)
			{
				wchar_t imageName[64];
				swprintf(imageName,64,L"tpack%08x",tp->getId());
				registerSubstitutionTexture(imageName, pbImageData, dwImageBytes);
				m_texturePackList.addPack(i,imageName);
			}
		}
		m_currentTexturePackIndex = pMinecraft->skins->getTexturePackIndex(m_MoreOptionsParams.dwTexturePack);
		UpdateTexturePackDescription(m_currentTexturePackIndex);
		m_texturePackList.selectSlot(m_currentTexturePackIndex);

		// 4J-PB - Only Xbox will not have trial DLC patched into the game
#ifdef _XBOX
		// 4J-PB - there may be texture packs we don't have, so use the info from TMS for this

		// 4J-PB - Any texture packs available that we don't have installed?
		if(!m_bAvailableTexturePacksChecked)
		{		
			DLC_INFO *pDLCInfo=NULL;

			// first pass - look to see if there are any that are not in the list
			bool bTexturePackAlreadyListed;
			bool bNeedToGetTPD=false;

			for(unsigned int i = 0; i < app.GetDLCInfoTexturesOffersCount(); ++i)
			{
				bTexturePackAlreadyListed=false;
				ULONGLONG ull=app.GetDLCInfoTexturesFullOffer(i);
				pDLCInfo=app.GetDLCInfoForFullOfferID(ull);

				for(unsigned int i = 0; i < texturePacksCount; ++i)
				{
					TexturePack *tp = pMinecraft->skins->getTexturePackByIndex(i);
					if(pDLCInfo && pDLCInfo->iConfig==tp->getDLCParentPackId())
					{
						bTexturePackAlreadyListed=true;
					}
				}
				if(bTexturePackAlreadyListed==false)
				{
					// some missing
					bNeedToGetTPD=true;

					m_iTexturePacksNotInstalled++;
				}
			}

			if(bNeedToGetTPD==true)
			{
				// add a TMS request for them
				app.DebugPrintf("+++ Adding TMSPP request for texture pack data\n");
				app.AddTMSPPFileTypeRequest(e_DLC_TexturePackData);
				m_iConfigA= new int [m_iTexturePacksNotInstalled];
				m_iTexturePacksNotInstalled=0;

				for(unsigned int i = 0; i < app.GetDLCInfoTexturesOffersCount(); ++i)
				{
					bTexturePackAlreadyListed=false;
					ULONGLONG ull=app.GetDLCInfoTexturesFullOffer(i);
					pDLCInfo=app.GetDLCInfoForFullOfferID(ull);

					if(pDLCInfo)
					{
						for(unsigned int i = 0; i < texturePacksCount; ++i)
						{
							TexturePack *tp = pMinecraft->skins->getTexturePackByIndex(i);
							if(pDLCInfo && pDLCInfo->iConfig==tp->getDLCParentPackId())
							{
								bTexturePackAlreadyListed=true;
							}
						}
						if(bTexturePackAlreadyListed==false)
						{
							m_iConfigA[m_iTexturePacksNotInstalled++]=pDLCInfo->iConfig;
						}
					}
				}
			}
		}
#endif
	}

#ifdef _XBOX
	addTimer(CHECKFORAVAILABLETEXTUREPACKS_TIMER_ID,CHECKFORAVAILABLETEXTUREPACKS_TIMER_TIME);
#endif

	if(params) delete params;
	addTimer(GAME_CREATE_ONLINE_TIMER_ID,GAME_CREATE_ONLINE_TIMER_TIME);
}

UIScene_LoadMenu::~UIScene_LoadMenu()
{
	if(m_bOwnsThumbnailData && m_pbThumbnailData != NULL)
	{
		delete [] m_pbThumbnailData;
		m_pbThumbnailData = NULL;
	}
}

void UIScene_LoadMenu::updateTooltips()
{
	ui.SetTooltips( DEFAULT_XUI_MENU_USER, IDS_TOOLTIPS_SELECT,IDS_TOOLTIPS_BACK, -1, -1);
}

void UIScene_LoadMenu::updateComponents()
{
	m_parentLayer->showComponent(m_iPad,eUIComponent_Panorama,true);

	if(RenderManager.IsWidescreen())
	{
		m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,true);
	}
	else
	{
		m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,false);
	}
}

wstring UIScene_LoadMenu::getMoviePath()
{
	return L"LoadMenu";
}

UIControl* UIScene_LoadMenu::GetMainPanel()
{
	return &m_controlMainPanel;
}

void UIScene_LoadMenu::tick()
{
	if(m_bShowTimer)
	{
		m_bShowTimer = false;
		ui.NavigateToScene(m_iPad, eUIScene_Timer);
	}

	if( m_bThumbnailGetFailed )
	{
		// On Durango, this can happen if a save is still not been synchronised (user cancelled, or some error). Return back to give them a choice to pick another save.
		ui.NavigateBack(m_iPad, false, eUIScene_LoadOrJoinMenu);
		return;
	}

	if( m_bSaveThumbnailReady )
	{
		m_bSaveThumbnailReady = false;

#ifdef _WINDOWS64
		const std::wstring displayThumbnailName = m_thumbnailName.empty() ? std::wstring() : (m_thumbnailName + L"_LoadMenuIcon");
#else
		const std::wstring displayThumbnailName = m_thumbnailName;
#endif

		if(m_pbThumbnailData != NULL && m_uiThumbnailSize > 0 && !displayThumbnailName.empty() && !hasRegisteredSubstitutionTexture(displayThumbnailName))
		{
#ifdef _WINDOWS64
			std::vector<unsigned char> normalizedIconData;
			if(BuildLoadMenuIconTexture(m_pbThumbnailData, m_uiThumbnailSize, normalizedIconData) && !normalizedIconData.empty())
			{
				BYTE *persistentTextureData = new BYTE[normalizedIconData.size()];
				memcpy(persistentTextureData, &normalizedIconData[0], normalizedIconData.size());
				registerSubstitutionTexture(displayThumbnailName, persistentTextureData, (DWORD)normalizedIconData.size(), true);
			}
			else
			{
				BYTE *persistentTextureData = new BYTE[m_uiThumbnailSize];
				memcpy(persistentTextureData, m_pbThumbnailData, m_uiThumbnailSize);
				registerSubstitutionTexture(displayThumbnailName, persistentTextureData, m_uiThumbnailSize, true);
			}
#else
			registerSubstitutionTexture(displayThumbnailName, m_pbThumbnailData, m_uiThumbnailSize);
#endif
		}
		m_bitmapIcon.setTextureName( displayThumbnailName.c_str() );

		// retrieve the seed value from the image metadata
		bool bHostOptionsRead = false;
		unsigned int uiHostOptions = 0;

		char szSeed[50];
		ZeroMemory(szSeed,50);
		app.GetImageTextData(m_pbThumbnailData,m_uiThumbnailSize,(unsigned char *)&szSeed,uiHostOptions,bHostOptionsRead,m_MoreOptionsParams.dwTexturePack);

#if defined(_XBOX_ONE)
		sscanf_s(szSeed, "%I64d", &m_seed);
#endif

		// #ifdef _DEBUG
		// 			// dump out the thumbnail
		// 			HANDLE hThumbnail = CreateFile("GAME:\\thumbnail.png", GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS, NULL);
		// 			DWORD dwBytes;
		// 			WriteFile(hThumbnail,pbImageData,dwImageBytes,&dwBytes,NULL);
		// 			XCloseHandle(hThumbnail);
		// #endif

		if(szSeed[0]!=0)
		{
			WCHAR TempString[256];
			swprintf( (WCHAR *)TempString, 256, L"%ls: %hs", app.GetString( IDS_SEED ),szSeed);	
			m_labelSeed.setLabel(TempString);
		}
		else
		{
			m_labelSeed.setLabel(L"");
		}

		// Setup all the text and checkboxes to match what the game was saved with on
		if(bHostOptionsRead)
		{
			m_MoreOptionsParams.bPVP = app.GetGameHostOption(uiHostOptions,eGameHostOption_PvP)>0?TRUE:FALSE;
			m_MoreOptionsParams.bTrust = app.GetGameHostOption(uiHostOptions,eGameHostOption_TrustPlayers)>0?TRUE:FALSE;
			m_MoreOptionsParams.bFireSpreads = app.GetGameHostOption(uiHostOptions,eGameHostOption_FireSpreads)>0?TRUE:FALSE;
			m_MoreOptionsParams.bTNT = app.GetGameHostOption(uiHostOptions,eGameHostOption_TNT)>0?TRUE:FALSE;
			m_MoreOptionsParams.bHostPrivileges = app.GetGameHostOption(uiHostOptions,eGameHostOption_CheatsEnabled)>0?TRUE:FALSE;
			m_MoreOptionsParams.bDisableSaving = app.GetGameHostOption(uiHostOptions,eGameHostOption_DisableSaving)>0?TRUE:FALSE;

			// turn off creative mode on the save
			// #ifdef _DEBUG
			//  			uiHostOptions&=~GAME_HOST_OPTION_BITMASK_BEENINCREATIVE;
			//  			app.SetGameHostOption(eGameHostOption_HasBeenInCreative, 0);
			// #endif

			m_bHasBeenInCreative = app.GetGameHostOption(uiHostOptions,eGameHostOption_HasBeenInCreative)>0;
			if(app.GetGameHostOption(uiHostOptions,eGameHostOption_HasBeenInCreative)>0)
			{
				m_labelCreatedMode.setLabel( app.GetString(IDS_CREATED_IN_CREATIVE) );
			}
			else
			{
				m_labelCreatedMode.setLabel( app.GetString(IDS_CREATED_IN_SURVIVAL) );
			}

			if(app.GetGameHostOption(uiHostOptions,eGameHostOption_GameType)>0)
			{			
				m_buttonGamemode.setLabel(app.GetString(IDS_GAMEMODE_CREATIVE));
				m_bGameModeSurvival=false;
			}

			bool bGameSetting_Online=(app.GetGameSettings(m_iPad,eGameSetting_Online)!=0);
			if(app.GetGameHostOption(uiHostOptions,eGameHostOption_FriendsOfFriends) && !(m_bMultiplayerAllowed && bGameSetting_Online))
			{
				m_MoreOptionsParams.bAllowFriendsOfFriends = TRUE;
			}
		}

		Minecraft *pMinecraft = Minecraft::GetInstance();
		m_currentTexturePackIndex = pMinecraft->skins->getTexturePackIndex(m_MoreOptionsParams.dwTexturePack);

		UpdateTexturePackDescription(m_currentTexturePackIndex);

		m_texturePackList.selectSlot(m_currentTexturePackIndex);

		//m_labelGameName.setLabel(m_XContentData.szDisplayName);

		ui.NavigateBack(m_iPad, false, getSceneType() );
	}

	if(m_iSetTexturePackDescription >= 0 )
	{
		UpdateTexturePackDescription( m_iSetTexturePackDescription );
		m_iSetTexturePackDescription = -1;
	}
	if(m_bShowTexturePackDescription)
	{
		slideLeft();
		m_texturePackDescDisplayed = true;

		m_bShowTexturePackDescription = false;
	}

	if(m_bRequestQuadrantSignin)
	{
		m_bRequestQuadrantSignin = false;
		SignInInfo info;
		info.Func = &UIScene_LoadMenu::StartGame_SignInReturned;
		info.lpParam = this;
		info.requireOnline = m_MoreOptionsParams.bOnlineGame;
		ui.NavigateToScene(ProfileManager.GetPrimaryPad(),eUIScene_QuadrantSignin,&info);
	}

	UIScene::tick();
}

void UIScene_LoadMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	if(m_bIgnoreInput) return;

	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	switch(key)
	{
	case ACTION_MENU_CANCEL:
		if(pressed)
		{
			app.SetCorruptSaveDeleted(false);
			navigateBack();
			handled = true;
		}
		break;
	case ACTION_MENU_OK:
		
	// 4J-JEV: Inform user why their game must be offline.
#if defined _XBOX_ONE
		if ( pressed && controlHasFocus(m_checkboxOnline.getId()) && !m_checkboxOnline.IsEnabled() )
		{
			UINT uiIDA[1] = { IDS_CONFIRM_OK };
			ui.RequestMessageBox(IDS_PRO_NOTONLINE_TITLE, IDS_PRO_XBOXLIVE_NOTIFICATION, uiIDA, 1, iPad, NULL, NULL, app.GetStringTable()); 
		}
#endif

	case ACTION_MENU_UP:
	case ACTION_MENU_DOWN:
	case ACTION_MENU_LEFT:
	case ACTION_MENU_RIGHT:
	case ACTION_MENU_OTHER_STICK_UP:
	case ACTION_MENU_OTHER_STICK_DOWN:
		sendInputToMovie(key, repeat, pressed, released);
		
#if defined _XBOX_ONE || defined _WINDOWS64
		if(getSceneResolution() == eSceneResolution_1080)
		{
			bool bOnlineGame = m_checkboxOnline.IsChecked();
			if (m_MoreOptionsParams.bOnlineGame != bOnlineGame)
			{
				m_MoreOptionsParams.bOnlineGame = bOnlineGame;

				if (!m_MoreOptionsParams.bOnlineGame)
				{
					m_MoreOptionsParams.bInviteOnly = false;
					m_MoreOptionsParams.bAllowFriendsOfFriends = false;
				}
			}
		}
#endif 
		handled = true;
		break;
	}
}

void UIScene_LoadMenu::handlePress(F64 controlId, F64 childId)
{
	if(m_bIgnoreInput) return;

	//CD - Added for audio
	ui.PlayUISFX(eSFX_Press);

	switch((int)controlId)
	{
	case eControl_GameMode:
		if(m_bGameModeSurvival)
		{
			m_buttonGamemode.setLabel(app.GetString(IDS_GAMEMODE_CREATIVE));
			m_bGameModeSurvival=false;
		}
		else
		{
			m_buttonGamemode.setLabel(app.GetString(IDS_GAMEMODE_SURVIVAL));
			m_bGameModeSurvival=true;
		}
		break;
	case eControl_MoreOptions:
		ui.NavigateToScene(m_iPad, eUIScene_LaunchMoreOptionsMenu, &m_MoreOptionsParams);
		break;
	case eControl_TexturePackList:
		{
			UpdateCurrentTexturePack((int)childId);
		}
		break;
	case eControl_LoadWorld:
		{
#ifdef _DURANGO
			if(m_MoreOptionsParams.bOnlineGame)
			{
				m_bIgnoreInput = true;
				ProfileManager.CheckMultiplayerPrivileges(m_iPad, true, &checkPrivilegeCallback, this);
			}
			else
#endif
			{
				StartSharedLaunchFlow();
			}
		}
		break;
	};
}

#ifdef _DURANGO
void UIScene_LoadMenu::checkPrivilegeCallback(LPVOID lpParam, bool hasPrivilege, int iPad)
{
	UIScene_LoadMenu* pClass = (UIScene_LoadMenu*)lpParam;

	if(hasPrivilege)
	{
		pClass->StartSharedLaunchFlow();
	}
	else
	{
		pClass->m_bIgnoreInput = false;
	}
}
#endif

void UIScene_LoadMenu::StartSharedLaunchFlow()
{
	Minecraft *pMinecraft=Minecraft::GetInstance();
	// Check if we need to upsell the texture pack
	if(m_MoreOptionsParams.dwTexturePack!=0)
	{
		// texture pack hasn't been set yet, so check what it will be
		TexturePack *pTexturePack = pMinecraft->skins->getTexturePackById(m_MoreOptionsParams.dwTexturePack);

		if(pTexturePack==NULL)
		{
#if TO_BE_IMPLEMENTED
			// They've selected a texture pack they don't have yet
			// upsell
			CXuiCtrl4JList::LIST_ITEM_INFO ListItem;
			// get the current index of the list, and then get the data
			ListItem=m_pTexturePacksList->GetData(m_currentTexturePackIndex);


			// upsell the texture pack
			// tell sentient about the upsell of the full version of the skin pack
			ULONGLONG ullOfferID_Full;
			app.GetDLCFullOfferIDForPackID(m_MoreOptionsParams.dwTexturePack,&ullOfferID_Full);

			TelemetryManager->RecordUpsellPresented(ProfileManager.GetPrimaryPad(), eSet_UpsellID_Texture_DLC, ullOfferID_Full & 0xFFFFFFFF);
#endif

			UINT uiIDA[2];

			uiIDA[0]=IDS_TEXTUREPACK_FULLVERSION;
			//uiIDA[1]=IDS_TEXTURE_PACK_TRIALVERSION;
			uiIDA[1]=IDS_CONFIRM_CANCEL;

			// Give the player a warning about the texture pack missing
			ui.RequestMessageBox(IDS_DLC_TEXTUREPACK_NOT_PRESENT_TITLE, IDS_DLC_TEXTUREPACK_NOT_PRESENT, uiIDA, 2, ProfileManager.GetPrimaryPad(),&TexturePackDialogReturned,this,app.GetStringTable(),NULL,0,false);
			return;
		}
	}
	m_bIgnoreInput = true;

	// if the profile data has been changed, then force a profile write (we save the online/invite/friends of friends settings)
	// It seems we're allowed to break the 5 minute rule if it's the result of a user action
	// check the checkboxes

	// Only save the online setting if the user changed it - we may change it because we're offline, but don't want that saved
	if(!m_MoreOptionsParams.bOnlineSettingChangedBySystem)
	{
		app.SetGameSettings(m_iPad,eGameSetting_Online,m_MoreOptionsParams.bOnlineGame?1:0);
	}
	app.SetGameSettings(m_iPad,eGameSetting_InviteOnly,m_MoreOptionsParams.bInviteOnly?1:0);
	app.SetGameSettings(m_iPad,eGameSetting_FriendsOfFriends,m_MoreOptionsParams.bAllowFriendsOfFriends?1:0);

	app.CheckGameSettingsChanged(true,m_iPad);

	// Check that we have the rights to use a texture pack we have selected.
	if(m_MoreOptionsParams.dwTexturePack!=0)
	{
		// texture pack hasn't been set yet, so check what it will be
		TexturePack *pTexturePack = pMinecraft->skins->getTexturePackById(m_MoreOptionsParams.dwTexturePack);
		DLCTexturePack *pDLCTexPack=(DLCTexturePack *)pTexturePack;
		m_pDLCPack=pDLCTexPack->getDLCInfoParentPack();

		// do we have a license?
		if(m_pDLCPack && !m_pDLCPack->hasPurchasedFile( DLCManager::e_DLCType_Texture, L"" ))
		{
			// no

			// We need to allow people to use a trial texture pack if they are offline - we only need them online if they want to buy it.

			/*
			UINT uiIDA[1];
			uiIDA[0]=IDS_OK;

			if(!ProfileManager.IsSignedInLive(m_iPad))
			{
				// need to be signed in to live
				ui.RequestMessageBox(IDS_PRO_NOTONLINE_TITLE, IDS_PRO_XBOXLIVE_NOTIFICATION, uiIDA, 1);
				m_bIgnoreInput = false;
				return;
			}
			else */
			{
				// upsell
#ifdef _XBOX
				DLC_INFO *pDLCInfo = app.GetDLCInfoForTrialOfferID(m_pDLCPack->getPurchaseOfferId());
				ULONGLONG ullOfferID_Full;

				if(pDLCInfo!=NULL)
				{
					ullOfferID_Full=pDLCInfo->ullOfferID_Full;
				}
				else
				{
					ullOfferID_Full=pTexturePack->getDLCPack()->getPurchaseOfferId();
				}

				// tell sentient about the upsell of the full version of the texture pack
				TelemetryManager->RecordUpsellPresented(m_iPad, eSet_UpsellID_Texture_DLC, ullOfferID_Full & 0xFFFFFFFF);
#endif

#if defined(_WINDOWS64) || defined(_DURANGO)
				// trial pack warning
				UINT uiIDA[1];
				uiIDA[0]=IDS_CONFIRM_OK;
				ui.RequestMessageBox(IDS_WARNING_DLC_TRIALTEXTUREPACK_TITLE, IDS_USING_TRIAL_TEXUREPACK_WARNING, uiIDA, 1, m_iPad,&TrialTexturePackWarningReturned,this,app.GetStringTable(),NULL,0,false);
#endif

#if defined _XBOX_ONE
				StorageManager.SetSaveDisabled(true);
#endif
				return;
			}
		}			
	}

#if defined _XBOX_ONE
	app.SetGameHostOption(eGameHostOption_DisableSaving, m_MoreOptionsParams.bDisableSaving?1:0);

	StorageManager.SetSaveDisabled(m_MoreOptionsParams.bDisableSaving);
#endif

#if TO_BE_IMPLEMENTED
	// Reset the background downloading, in case we changed it by attempting to download a texture pack
	XBackgroundDownloadSetMode(XBACKGROUND_DOWNLOAD_MODE_AUTO);
#endif

	// Check if they have the Reset Nether flag set, and confirm they want to do this
	if(m_MoreOptionsParams.bResetNether==TRUE)
	{
		UINT uiIDA[2];
		uiIDA[0]=IDS_DONT_RESET_NETHER;
		uiIDA[1]=IDS_RESET_NETHER;

		ui.RequestMessageBox(IDS_RESETNETHER_TITLE, IDS_RESETNETHER_TEXT, uiIDA, 2, m_iPad,&UIScene_LoadMenu::CheckResetNetherReturned,this,app.GetStringTable(),NULL,0,false);
	}
	else
	{
		LaunchGame();
	}
}

void UIScene_LoadMenu::handleSliderMove(F64 sliderId, F64 currentValue)
{
	WCHAR TempString[256];
	int value = (int)currentValue;
	switch((int)sliderId)
	{
	case eControl_Difficulty:
		m_sliderDifficulty.handleSliderMove(value);

		app.SetGameSettings(m_iPad,eGameSetting_Difficulty,value);
		swprintf( (WCHAR *)TempString, 256, L"%ls: %ls", app.GetString( IDS_SLIDER_DIFFICULTY ),app.GetString(m_iDifficultyTitleSettingA[value]));		
		m_sliderDifficulty.setLabel(TempString);
		break;
	}
}

void UIScene_LoadMenu::handleTouchBoxRebuild()
{
	m_bRebuildTouchBoxes = true;
}


void UIScene_LoadMenu::handleTimerComplete(int id)
{
	switch(id)
	{
	case GAME_CREATE_ONLINE_TIMER_ID:
		{
			bool bMultiplayerAllowed = ProfileManager.IsSignedInLive( m_iPad ) && ProfileManager.AllowedToPlayMultiplayer(m_iPad);

			if(bMultiplayerAllowed != m_bMultiplayerAllowed)
			{
				if( bMultiplayerAllowed )
				{
					bool bGameSetting_Online=(app.GetGameSettings(m_iPad,eGameSetting_Online)!=0);
					m_MoreOptionsParams.bOnlineGame = bGameSetting_Online?TRUE:FALSE;
					if(bGameSetting_Online)
					{
						m_MoreOptionsParams.bInviteOnly = (app.GetGameSettings(m_iPad,eGameSetting_InviteOnly)!=0)?TRUE:FALSE;
						m_MoreOptionsParams.bAllowFriendsOfFriends = (app.GetGameSettings(m_iPad,eGameSetting_FriendsOfFriends)!=0)?TRUE:FALSE;
					}
					else
					{
						m_MoreOptionsParams.bInviteOnly = FALSE;
						m_MoreOptionsParams.bAllowFriendsOfFriends = FALSE;
					}
				}
				else
				{
					m_MoreOptionsParams.bOnlineGame = FALSE;
					m_MoreOptionsParams.bInviteOnly = FALSE;
					m_MoreOptionsParams.bAllowFriendsOfFriends = FALSE;
				}
#if defined _XBOX_ONE || defined _WINDOWS64
				if(getSceneResolution() == eSceneResolution_1080)
				{
					m_checkboxOnline.SetEnable(bMultiplayerAllowed);
					m_checkboxOnline.setChecked(m_MoreOptionsParams.bOnlineGame);
				}
#endif

				m_bMultiplayerAllowed = bMultiplayerAllowed;
			}
		}
		break;
		// 4J-PB - Only Xbox will not have trial DLC patched into the game
#ifdef _XBOX
	case CHECKFORAVAILABLETEXTUREPACKS_TIMER_ID:
		{
			bool bAllDone=true;
			for(int i=0;i<m_iTexturePacksNotInstalled;i++)
			{
				if(m_iConfigA[i]!=-1) 
				{
					bAllDone = false;
				}
			}

			if(bAllDone)
			{
				// kill this timer
				killTimer(CHECKFORAVAILABLETEXTUREPACKS_TIMER_ID);
			}

		}
		break;
#endif	
	}
}

void UIScene_LoadMenu::LaunchGame(void)
{
	// stop the timer running that causes a check for new texture packs in TMS but not installed, since this will run all through the load game, and will crash if it tries to create an hbrush
#ifdef _XBOX
	killTimer(CHECKFORAVAILABLETEXTUREPACKS_TIMER_ID);
#endif

	if( (m_bGameModeSurvival != true || m_bHasBeenInCreative) || m_MoreOptionsParams.bHostPrivileges == TRUE)
	{			
		UINT uiIDA[2];
		uiIDA[0]=IDS_CONFIRM_OK;
		uiIDA[1]=IDS_CONFIRM_CANCEL;
		if(m_bGameModeSurvival != true || m_bHasBeenInCreative)
		{
			// 4J-PB - Need different text for Survival mode with a level that has been saved in Creative
			if(m_bGameModeSurvival)
			{
				ui.RequestMessageBox(IDS_TITLE_START_GAME, IDS_CONFIRM_START_SAVEDINCREATIVE, uiIDA, 2, m_iPad,&UIScene_LoadMenu::ConfirmLoadReturned,this,app.GetStringTable(),NULL,0,false);
			}
			else // it's creative mode
			{
				// has it previously been saved in creative?
				if(m_bHasBeenInCreative)
				{
					// 4J-PB - We don't really need to tell the user this will have achievements disabled, since they already saved it in creative
					// and they got the warning then
					// inform them that leaderboard writes and achievements will be disabled
					//ui.RequestMessageBox(IDS_TITLE_START_GAME, IDS_CONFIRM_START_SAVEDINCREATIVE_CONTINUE, uiIDA, 1, m_iPad,&CScene_LoadGameSettings::ConfirmLoadReturned,this,app.GetStringTable());

					if(m_levelGen != NULL)
					{
						LoadLevelGen(m_levelGen);
					}
					else
					{

						// set the save to load
#ifdef _WINDOWS64
						if(m_bWindows64DirectDiskSave)
						{
							LoadDataComplete(this);
							return;
						}
#endif
						PSAVE_DETAILS pSaveDetails=StorageManager.ReturnSavesInfo();
#ifndef _DURANGO
						app.DebugPrintf("Loading save s [%s]\n",pSaveDetails->SaveInfoA[(int)m_iSaveGameInfoIndex].UTF8SaveTitle,pSaveDetails->SaveInfoA[(int)m_iSaveGameInfoIndex].UTF8SaveFilename);
#endif
						C4JStorage::ESaveGameState eLoadStatus=StorageManager.LoadSaveData(&pSaveDetails->SaveInfoA[(int)m_iSaveGameInfoIndex],&LoadSaveDataReturned,this);

#if TO_BE_IMPLEMENTED
						if(eLoadStatus==C4JStorage::ELoadGame_DeviceRemoved)
						{
							// disable saving 
							StorageManager.SetSaveDisabled(true);
							StorageManager.SetSaveDeviceSelected(m_iPad,false);
							UINT uiIDA[1];
							uiIDA[0]=IDS_OK;
							ui.RequestMessageBox(IDS_STORAGEDEVICEPROBLEM_TITLE, IDS_FAILED_TO_LOADSAVE_TEXT, uiIDA, 1, m_iPad,&CScene_LoadGameSettings::DeviceRemovedDialogReturned,this);

						}
#endif
					}
				}
				else
				{
					// ask if they're sure they want to turn this into a creative map
					ui.RequestMessageBox(IDS_TITLE_START_GAME, IDS_CONFIRM_START_CREATIVE, uiIDA, 2, m_iPad,&UIScene_LoadMenu::ConfirmLoadReturned,this,app.GetStringTable(),NULL,0,false);
				}
			}
		}
		else
		{
			ui.RequestMessageBox(IDS_TITLE_START_GAME, IDS_CONFIRM_START_HOST_PRIVILEGES, uiIDA, 2, m_iPad,&UIScene_LoadMenu::ConfirmLoadReturned,this,app.GetStringTable(),NULL,0,false);
		}
	}
	else
	{
		if(m_levelGen != NULL)
		{
			LoadLevelGen(m_levelGen);
		}
		else
		{
			// set the save to load
#ifdef _WINDOWS64
			if(m_bWindows64DirectDiskSave)
			{
				LoadDataComplete(this);
				return;
			}
#endif
			PSAVE_DETAILS pSaveDetails=StorageManager.ReturnSavesInfo();
#ifndef _DURANGO
			app.DebugPrintf("Loading save %s [%s]\n",pSaveDetails->SaveInfoA[(int)m_iSaveGameInfoIndex].UTF8SaveTitle,pSaveDetails->SaveInfoA[(int)m_iSaveGameInfoIndex].UTF8SaveFilename);
#endif
			C4JStorage::ESaveGameState eLoadStatus=StorageManager.LoadSaveData(&pSaveDetails->SaveInfoA[(int)m_iSaveGameInfoIndex],&LoadSaveDataReturned,this);

#if TO_BE_IMPLEMENTED
			if(eLoadStatus==C4JStorage::ELoadGame_DeviceRemoved)
			{
				// disable saving 
				StorageManager.SetSaveDisabled(true);
				StorageManager.SetSaveDeviceSelected(m_iPad,false);
				UINT uiIDA[1];
				uiIDA[0]=IDS_OK;
				ui.RequestMessageBox(IDS_STORAGEDEVICEPROBLEM_TITLE, IDS_FAILED_TO_LOADSAVE_TEXT, uiIDA, 1, m_iPad,&CScene_LoadGameSettings::DeviceRemovedDialogReturned,this);
			}
#endif
		}
	}
	//return 0;
}

int UIScene_LoadMenu::CheckResetNetherReturned(void *pParam,int iPad,C4JStorage::EMessageResult result)
{
	UIScene_LoadMenu* pClass = (UIScene_LoadMenu*)pParam;

	// results switched for this dialog
	if(result==C4JStorage::EMessage_ResultDecline) 
	{
		// continue and reset the nether
		pClass->LaunchGame();
	}
	else if(result==C4JStorage::EMessage_ResultAccept)
	{
		// turn off the reset nether and continue
		pClass->m_MoreOptionsParams.bResetNether=FALSE;
		pClass->LaunchGame();
	}
	else
	{
		// else they chose cancel
		pClass->m_bIgnoreInput=false;
	}
	return 0;
}

int UIScene_LoadMenu::ConfirmLoadReturned(void *pParam,int iPad,C4JStorage::EMessageResult result)
{
	UIScene_LoadMenu* pClass = (UIScene_LoadMenu*)pParam;

	if(result==C4JStorage::EMessage_ResultAccept) 
	{
		if(pClass->m_levelGen != NULL)
		{
			pClass->LoadLevelGen(pClass->m_levelGen);
		}
		else
		{
			// set the save to load
#ifdef _WINDOWS64
			if(pClass->m_bWindows64DirectDiskSave)
			{
				LoadDataComplete(pClass);
				return 0;
			}
#endif
			PSAVE_DETAILS pSaveDetails=StorageManager.ReturnSavesInfo();
#ifndef _DURANGO
			app.DebugPrintf("Loading save %s [%s]\n",pSaveDetails->SaveInfoA[(int)pClass->m_iSaveGameInfoIndex].UTF8SaveTitle,pSaveDetails->SaveInfoA[(int)pClass->m_iSaveGameInfoIndex].UTF8SaveFilename);
#endif
			C4JStorage::ESaveGameState eLoadStatus=StorageManager.LoadSaveData(&pSaveDetails->SaveInfoA[(int)pClass->m_iSaveGameInfoIndex],&LoadSaveDataReturned,pClass);

#if TO_BE_IMPLEMENTED
			if(eLoadStatus==C4JStorage::ELoadGame_DeviceRemoved)
			{
				// disable saving 
				StorageManager.SetSaveDisabled(true);
				StorageManager.SetSaveDeviceSelected(m_iPad,false);
				UINT uiIDA[1];
				uiIDA[0]=IDS_OK;
				ui.RequestMessageBox(IDS_STORAGEDEVICEPROBLEM_TITLE, IDS_FAILED_TO_LOADSAVE_TEXT, uiIDA, 1, m_iPad,&CScene_LoadGameSettings::DeviceRemovedDialogReturned,this);
			}
#endif		
		}
	}
	else
	{
		pClass->m_bIgnoreInput=false;
	}
	return 0;
}

int UIScene_LoadMenu::LoadDataComplete(void *pParam)
{
	UIScene_LoadMenu* pClass = (UIScene_LoadMenu*)pParam;

	if(!pClass->m_bIsCorrupt)
	{
		int iPrimaryPad = ProfileManager.GetPrimaryPad();
		bool isSignedInLive = true;
		bool isOnlineGame = pClass->m_MoreOptionsParams.bOnlineGame;
		int iPadNotSignedInLive = -1;
		bool isLocalMultiplayerAvailable = app.IsLocalMultiplayerAvailable();

		for(unsigned int i = 0; i < XUSER_MAX_COUNT; ++i)
		{
			if (ProfileManager.IsSignedIn(i) && ((i == iPrimaryPad) || isLocalMultiplayerAvailable))
			{
				if (isSignedInLive && !ProfileManager.IsSignedInLive(i))
				{
					// Record the first non signed in live pad
					iPadNotSignedInLive = i;
				}

				isSignedInLive = isSignedInLive && ProfileManager.IsSignedInLive(i);
			}
		}

		// If this is an online game but not all players are signed in to Live, stop!
		if (isOnlineGame && !isSignedInLive)
		{
			pClass->m_bIgnoreInput=false;
			UINT uiIDA[1];
			uiIDA[0]=IDS_CONFIRM_OK;
			ui.RequestMessageBox( IDS_PRO_NOTONLINE_TITLE, IDS_PRO_NOTONLINE_TEXT, uiIDA,1,ProfileManager.GetPrimaryPad(),NULL,NULL, app.GetStringTable(),NULL,0,false);
			return 0;
		}

		// Check if user-created content is allowed, as we cannot play multiplayer if it's not
		bool noUGC = false;
		BOOL pccAllowed = TRUE;
		BOOL pccFriendsAllowed = TRUE;
		ProfileManager.AllowedPlayerCreatedContent(ProfileManager.GetPrimaryPad(),false,&pccAllowed,&pccFriendsAllowed);
		noUGC = !pccAllowed && !pccFriendsAllowed;

		if(!isOnlineGame || !isLocalMultiplayerAvailable)
		{
			if(isOnlineGame && noUGC )
			{
				pClass->setVisible( true );

				ui.RequestUGCMessageBox();

				pClass->m_bIgnoreInput=false;
			}
			else
			{
				DWORD dwLocalUsersMask = CGameNetworkManager::GetLocalPlayerMask(ProfileManager.GetPrimaryPad());

				// No guest problems so we don't need to force a sign-in of players here
				StartGameFromSave(pClass, dwLocalUsersMask);
			}
		}
		else
		{
			// 4J-PB not sure why we aren't checking the content restriction for the main player here when multiple controllers are connected - adding now
			if(isOnlineGame && noUGC )
			{
				pClass->setVisible( true );
				ui.RequestUGCMessageBox();
				pClass->m_bIgnoreInput=false;
			}
			else
			{
				pClass->m_bRequestQuadrantSignin = true;
			}
		}
	}
	else
	{
		// the save is corrupt!
		pClass->m_bIgnoreInput=false;

		// give the option to delete the save
		UINT uiIDA[2];
		uiIDA[0]=IDS_CONFIRM_CANCEL;
		uiIDA[1]=IDS_CONFIRM_OK;
		ui.RequestMessageBox(IDS_CORRUPT_OR_DAMAGED_SAVE_TITLE, IDS_CORRUPT_OR_DAMAGED_SAVE_TEXT, uiIDA, 2, pClass->m_iPad,&UIScene_LoadMenu::DeleteSaveDialogReturned,pClass, app.GetStringTable(),NULL,0,false);

	}

	return 0;
}

int UIScene_LoadMenu::LoadSaveDataReturned(void *pParam,bool bIsCorrupt, bool bIsOwner)
{
	UIScene_LoadMenu* pClass = (UIScene_LoadMenu*)pParam;

	pClass->m_bIsCorrupt=bIsCorrupt;
	if(bIsOwner)
	{
		LoadDataComplete(pClass);
	}
	else
	{
		// messagebox
		pClass->m_bIgnoreInput=false;
	}


	return 0;
}

int UIScene_LoadMenu::TrophyDialogReturned(void *pParam,int iPad,C4JStorage::EMessageResult result)
{
	UIScene_LoadMenu* pClass = (UIScene_LoadMenu*)pParam;
	return LoadDataComplete(pClass);
}

int UIScene_LoadMenu::DeleteSaveDialogReturned(void *pParam,int iPad,C4JStorage::EMessageResult result)
{
	UIScene_LoadMenu* pClass = (UIScene_LoadMenu*)pParam;

	// results switched for this dialog
	if(result==C4JStorage::EMessage_ResultDecline) 
	{
#ifdef _WINDOWS64
		if(pClass->m_bWindows64DirectDiskSave)
		{
			return DeleteSaveDataReturned(pClass, TryDeleteWindows64DirectSave(pClass->m_windows64DirectSaveId));
		}
#endif
		PSAVE_DETAILS pSaveDetails=StorageManager.ReturnSavesInfo();
		if(pSaveDetails != NULL && pClass->m_iSaveGameInfoIndex >= 0 && pClass->m_iSaveGameInfoIndex < pSaveDetails->iSaveC)
		{
			StorageManager.DeleteSaveData(&pSaveDetails->SaveInfoA[(int)pClass->m_iSaveGameInfoIndex],UIScene_LoadMenu::DeleteSaveDataReturned,pClass);
		}
		else
		{
			pClass->m_bIgnoreInput=false;
		}
	}
	else
	{
		pClass->m_bIgnoreInput=false;
	}
	return 0;
}

int UIScene_LoadMenu::DeleteSaveDataReturned(void *pParam,bool bSuccess)
{
	UIScene_LoadMenu* pClass = (UIScene_LoadMenu*)pParam;

	if(bSuccess)
	{
		app.SetCorruptSaveDeleted(true);
		pClass->navigateBack();
	}
	else
	{
		pClass->m_bIgnoreInput = false;
	}

	return 0;
}

#ifdef _WINDOWS64
void UIScene_LoadMenu::StartGameFromWindows64DirectDiskSave(DWORD dwLocalUsersMask)
{
	File storageRoot(L"Windows64\\GameHDD");
	File storageDir(storageRoot, filenametowstring(m_windows64DirectSaveId));
	File saveDataFile(storageDir, L"saveData.ms");
	const std::wstring saveDisplayName = Utf8ToWideString(m_windows64DirectSaveName);

	if(!saveDataFile.exists() || !saveDataFile.isFile())
	{
		m_bIgnoreInput = false;
		UINT uiIDA[1];
		uiIDA[0]=IDS_CONFIRM_OK;
		ui.RequestMessageBox(IDS_STORAGEDEVICEPROBLEM_TITLE, IDS_FAILED_TO_LOADSAVE_TEXT, uiIDA, 1, m_iPad, NULL, NULL, app.GetStringTable(), NULL, 0, false);
		return;
	}
	__int64 fileSize = saveDataFile.length();
	if(fileSize <= 0 || fileSize > 0x7fffffff)
	{
		m_bIgnoreInput = false;
		UINT uiIDA[2];
		uiIDA[0]=IDS_CONFIRM_CANCEL;
		uiIDA[1]=IDS_CONFIRM_OK;
		ui.RequestMessageBox(IDS_CORRUPT_OR_DAMAGED_SAVE_TITLE, IDS_CORRUPT_OR_DAMAGED_SAVE_TEXT, uiIDA, 2, m_iPad,&UIScene_LoadMenu::DeleteSaveDialogReturned,this, app.GetStringTable(),NULL,0,false);
		return;
	}

	FileInputStream fis(saveDataFile);
	byteArray ba((unsigned int)fileSize);
	fis.read(ba);
	fis.close();

	std::string resolvedTitleUtf8;
	if(!TryResolveWindows64DirectSaveTitleFromBytes(saveDisplayName, ba.data, (DWORD)ba.length, resolvedTitleUtf8))
	{
		m_bIsCorrupt = true;
		m_bIgnoreInput = false;
		UINT uiIDA[2];
		uiIDA[0]=IDS_CONFIRM_CANCEL;
		uiIDA[1]=IDS_CONFIRM_OK;
		ui.RequestMessageBox(IDS_CORRUPT_OR_DAMAGED_SAVE_TITLE, IDS_CORRUPT_OR_DAMAGED_SAVE_TEXT, uiIDA, 2, m_iPad,&UIScene_LoadMenu::DeleteSaveDialogReturned,this, app.GetStringTable(),NULL,0,false);
		return;
	}

	const std::wstring preparedSaveTitle = resolvedTitleUtf8.empty() ? saveDisplayName : Utf8ToWideString(resolvedTitleUtf8.c_str());
	StorageManager.ResetSaveData();
	app.SetPreparedSaveIdentity(preparedSaveTitle.c_str(), m_windows64DirectSaveId);

	bool isClientSide = ProfileManager.IsSignedInLive(ProfileManager.GetPrimaryPad()) && m_MoreOptionsParams.bOnlineGame;
	bool isPrivate = (app.GetGameSettings(m_iPad,eGameSetting_InviteOnly)>0)?true:false;

	NetworkGameInitData *param = new NetworkGameInitData();
	param->seed = m_seed;
	param->saveData = new LoadSaveDataThreadParam(ba.data, ba.length, preparedSaveTitle);
	param->texturePackId = m_MoreOptionsParams.dwTexturePack;

	Minecraft *pMinecraft = Minecraft::GetInstance();
	pMinecraft->skins->selectTexturePackById(m_MoreOptionsParams.dwTexturePack);

	app.SetGameHostOption(eGameHostOption_Difficulty,Minecraft::GetInstance()->options->difficulty);
	app.SetGameHostOption(eGameHostOption_FriendsOfFriends,app.GetGameSettings(m_iPad,eGameSetting_FriendsOfFriends));
	app.SetGameHostOption(eGameHostOption_Gamertags,app.GetGameSettings(m_iPad,eGameSetting_GamertagsVisible));
	app.SetGameHostOption(eGameHostOption_BedrockFog,app.GetGameSettings(m_iPad,eGameSetting_BedrockFog)?1:0);
	app.SetGameHostOption(eGameHostOption_PvP,m_MoreOptionsParams.bPVP);
	app.SetGameHostOption(eGameHostOption_TrustPlayers,m_MoreOptionsParams.bTrust);
	app.SetGameHostOption(eGameHostOption_FireSpreads,m_MoreOptionsParams.bFireSpreads);
	app.SetGameHostOption(eGameHostOption_TNT,m_MoreOptionsParams.bTNT);
	app.SetGameHostOption(eGameHostOption_HostCanFly,m_MoreOptionsParams.bHostPrivileges);
	app.SetGameHostOption(eGameHostOption_HostCanChangeHunger,m_MoreOptionsParams.bHostPrivileges);
	app.SetGameHostOption(eGameHostOption_HostCanBeInvisible,m_MoreOptionsParams.bHostPrivileges);
	app.SetResetNether((m_MoreOptionsParams.bResetNether==TRUE)?true:false);
	app.ClearTerrainFeaturePosition();
	app.SetGameHostOption(eGameHostOption_GameType,m_bGameModeSurvival?GameType::SURVIVAL->getId():GameType::CREATIVE->getId());

	g_NetworkManager.HostGame(dwLocalUsersMask,isClientSide,isPrivate,MINECRAFT_NET_MAX_PLAYERS,0);

	param->settings = app.GetGameHostOption( eGameHostOption_All );

#ifndef _XBOX
	g_NetworkManager.FakeLocalPlayerJoined();
#endif

	LoadingInputParams *loadingParams = new LoadingInputParams();
	loadingParams->func = &CGameNetworkManager::RunNetworkGameThreadProc;
	loadingParams->lpParam = (LPVOID)param;

	app.SetAutosaveTimerTime();

	UIFullscreenProgressCompletionData *completionData = new UIFullscreenProgressCompletionData();
	completionData->bShowBackground=TRUE;
	completionData->bShowLogo=TRUE;
	completionData->type = e_ProgressCompletion_CloseAllPlayersUIScenes;
	completionData->iPad = DEFAULT_XUI_MENU_USER;
	loadingParams->completionData = completionData;

	ui.NavigateToScene(ProfileManager.GetPrimaryPad(),eUIScene_FullscreenProgress, loadingParams);
}
#endif

// 4J Stu - Shared functionality that is the same whether we needed a quadrant sign-in or not
void UIScene_LoadMenu::StartGameFromSave(UIScene_LoadMenu* pClass, DWORD dwLocalUsersMask)
{
#ifdef _WINDOWS64
	if(pClass->m_bWindows64DirectDiskSave)
	{
		pClass->StartGameFromWindows64DirectDiskSave(dwLocalUsersMask);
		return;
	}
#endif
	INT saveOrCheckpointId = 0;
	bool validSave = StorageManager.GetSaveUniqueNumber(&saveOrCheckpointId);
	TelemetryManager->RecordLevelResume(pClass->m_iPad, eSen_FriendOrMatch_Playing_With_Invited_Friends, eSen_CompeteOrCoop_Coop_and_Competitive, app.GetGameSettings(pClass->m_iPad,eGameSetting_Difficulty), app.GetLocalPlayerCount(), g_NetworkManager.GetOnlinePlayerCount(), saveOrCheckpointId);

	bool isClientSide = ProfileManager.IsSignedInLive(ProfileManager.GetPrimaryPad()) && pClass->m_MoreOptionsParams.bOnlineGame;

	bool isPrivate = (app.GetGameSettings(pClass->m_iPad,eGameSetting_InviteOnly)>0)?true:false;

	PSAVE_DETAILS pSaveDetails=StorageManager.ReturnSavesInfo();

	NetworkGameInitData *param = new NetworkGameInitData();
	param->seed = pClass->m_seed;
	param->saveData = NULL;
	param->texturePackId = pClass->m_MoreOptionsParams.dwTexturePack;

	Minecraft *pMinecraft = Minecraft::GetInstance();
	pMinecraft->skins->selectTexturePackById(pClass->m_MoreOptionsParams.dwTexturePack);
	//pMinecraft->skins->updateUI();

	app.SetGameHostOption(eGameHostOption_Difficulty,Minecraft::GetInstance()->options->difficulty);
	app.SetGameHostOption(eGameHostOption_FriendsOfFriends,app.GetGameSettings(pClass->m_iPad,eGameSetting_FriendsOfFriends));
	app.SetGameHostOption(eGameHostOption_Gamertags,app.GetGameSettings(pClass->m_iPad,eGameSetting_GamertagsVisible));

	app.SetGameHostOption(eGameHostOption_BedrockFog,app.GetGameSettings(pClass->m_iPad,eGameSetting_BedrockFog)?1:0);

	app.SetGameHostOption(eGameHostOption_PvP,pClass->m_MoreOptionsParams.bPVP);
	app.SetGameHostOption(eGameHostOption_TrustPlayers,pClass->m_MoreOptionsParams.bTrust );
	app.SetGameHostOption(eGameHostOption_FireSpreads,pClass->m_MoreOptionsParams.bFireSpreads );
	app.SetGameHostOption(eGameHostOption_TNT,pClass->m_MoreOptionsParams.bTNT );
	app.SetGameHostOption(eGameHostOption_HostCanFly,pClass->m_MoreOptionsParams.bHostPrivileges);
	app.SetGameHostOption(eGameHostOption_HostCanChangeHunger,pClass->m_MoreOptionsParams.bHostPrivileges);
	app.SetGameHostOption(eGameHostOption_HostCanBeInvisible,pClass->m_MoreOptionsParams.bHostPrivileges );

	// flag if the user wants to reset the Nether to force a Fortress with netherwart etc.
	app.SetResetNether((pClass->m_MoreOptionsParams.bResetNether==TRUE)?true:false);
	// clear out the app's terrain features list
	app.ClearTerrainFeaturePosition();

	app.SetGameHostOption(eGameHostOption_GameType,pClass->m_bGameModeSurvival?GameType::SURVIVAL->getId():GameType::CREATIVE->getId() );

	g_NetworkManager.HostGame(dwLocalUsersMask,isClientSide,isPrivate,MINECRAFT_NET_MAX_PLAYERS,0);

	param->settings = app.GetGameHostOption( eGameHostOption_All );

#ifndef _XBOX
	g_NetworkManager.FakeLocalPlayerJoined();
#endif

	LoadingInputParams *loadingParams = new LoadingInputParams();
	loadingParams->func = &CGameNetworkManager::RunNetworkGameThreadProc;
	loadingParams->lpParam = (LPVOID)param;

	// Reset the autosave time
	app.SetAutosaveTimerTime();

	UIFullscreenProgressCompletionData *completionData = new UIFullscreenProgressCompletionData();
	completionData->bShowBackground=TRUE;
	completionData->bShowLogo=TRUE;
	completionData->type = e_ProgressCompletion_CloseAllPlayersUIScenes;
	completionData->iPad = DEFAULT_XUI_MENU_USER;
	loadingParams->completionData = completionData;

	ui.NavigateToScene(ProfileManager.GetPrimaryPad(),eUIScene_FullscreenProgress, loadingParams);
}

void UIScene_LoadMenu::checkStateAndStartGame()
{
	// Check if they have the Reset Nether flag set, and confirm they want to do this
	if(m_MoreOptionsParams.bResetNether==TRUE)
	{
		UINT uiIDA[2];
		uiIDA[0]=IDS_DONT_RESET_NETHER;
		uiIDA[1]=IDS_RESET_NETHER;

		ui.RequestMessageBox(IDS_RESETNETHER_TITLE, IDS_RESETNETHER_TEXT, uiIDA, 2, m_iPad,&UIScene_LoadMenu::CheckResetNetherReturned,this,app.GetStringTable(),NULL,0,false);
	}
	else
	{
		LaunchGame();
	}
}

void UIScene_LoadMenu::LoadLevelGen(LevelGenerationOptions *levelGen)
{
	bool isClientSide = ProfileManager.IsSignedInLive(ProfileManager.GetPrimaryPad()) && m_MoreOptionsParams.bOnlineGame;

	// 4J Stu - If we only have one controller connected, then don't show the sign-in UI again
	DWORD connectedControllers = 0;
	for(unsigned int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		if( InputManager.IsPadConnected(i) || ProfileManager.IsSignedIn(i) ) ++connectedControllers;
	}

	if(!isClientSide || connectedControllers == 1 || !RenderManager.IsHiDef())
	{

		// Check if user-created content is allowed, as we cannot play multiplayer if it's not
		bool noUGC = false;
		BOOL pccAllowed = TRUE;
		BOOL pccFriendsAllowed = TRUE;

		ProfileManager.AllowedPlayerCreatedContent(ProfileManager.GetPrimaryPad(),false,&pccAllowed,&pccFriendsAllowed);
		if(!pccAllowed && !pccFriendsAllowed) noUGC = true;

		if(isClientSide && noUGC )
		{
			m_bIgnoreInput=false;
			UINT uiIDA[1];
			uiIDA[0]=IDS_CONFIRM_OK;
			ui.RequestMessageBox( IDS_FAILED_TO_CREATE_GAME_TITLE, IDS_NO_USER_CREATED_CONTENT_PRIVILEGE_CREATE, uiIDA,1,ProfileManager.GetPrimaryPad(),NULL,NULL, app.GetStringTable(),NULL,0,false);
			return;
		}

	}

	DWORD dwLocalUsersMask = 0;

	dwLocalUsersMask |= CGameNetworkManager::GetLocalPlayerMask(ProfileManager.GetPrimaryPad());
	// Load data from disc
	//File saveFile( L"Tutorial\\Tutorial" );
	//LoadSaveFromDisk(&saveFile);

	app.PrepareNewSaveData(levelGen->getDefaultSaveName().c_str());

	bool isPrivate = (app.GetGameSettings(m_iPad,eGameSetting_InviteOnly)>0)?true:false;

	g_NetworkManager.HostGame(dwLocalUsersMask,isClientSide,isPrivate,MINECRAFT_NET_MAX_PLAYERS,0);

	NetworkGameInitData *param = new NetworkGameInitData();
	param->seed = 0;
	param->saveData = NULL;
	param->levelGen = levelGen;

	if(levelGen->requiresTexturePack())
	{
		param->texturePackId = levelGen->getRequiredTexturePackId();

		Minecraft *pMinecraft = Minecraft::GetInstance();
		pMinecraft->skins->selectTexturePackById(param->texturePackId);
		//pMinecraft->skins->updateUI();
	}


	app.SetGameHostOption(eGameHostOption_Difficulty,Minecraft::GetInstance()->options->difficulty);
	app.SetGameHostOption(eGameHostOption_FriendsOfFriends,app.GetGameSettings(m_iPad,eGameSetting_FriendsOfFriends));
	app.SetGameHostOption(eGameHostOption_Gamertags,app.GetGameSettings(m_iPad,eGameSetting_GamertagsVisible));

	app.SetGameHostOption(eGameHostOption_BedrockFog,app.GetGameSettings(m_iPad,eGameSetting_BedrockFog)?1:0);

	app.SetGameHostOption(eGameHostOption_PvP,m_MoreOptionsParams.bPVP);
	app.SetGameHostOption(eGameHostOption_TrustPlayers,m_MoreOptionsParams.bTrust );
	app.SetGameHostOption(eGameHostOption_FireSpreads,m_MoreOptionsParams.bFireSpreads );
	app.SetGameHostOption(eGameHostOption_TNT,m_MoreOptionsParams.bTNT );
	app.SetGameHostOption(eGameHostOption_HostCanFly,m_MoreOptionsParams.bHostPrivileges);
	app.SetGameHostOption(eGameHostOption_HostCanChangeHunger,m_MoreOptionsParams.bHostPrivileges);
	app.SetGameHostOption(eGameHostOption_HostCanBeInvisible,m_MoreOptionsParams.bHostPrivileges );

	// flag if the user wants to reset the Nether to force a Fortress with netherwart etc.
	app.SetResetNether((m_MoreOptionsParams.bResetNether==TRUE)?true:false);
	// clear out the app's terrain features list
	app.ClearTerrainFeaturePosition();

	app.SetGameHostOption(eGameHostOption_GameType,m_bGameModeSurvival?GameType::SURVIVAL->getId():GameType::CREATIVE->getId() );

	param->settings = app.GetGameHostOption( eGameHostOption_All );

#ifndef _XBOX
	g_NetworkManager.FakeLocalPlayerJoined();
#endif

	LoadingInputParams *loadingParams = new LoadingInputParams();
	loadingParams->func = &CGameNetworkManager::RunNetworkGameThreadProc;
	loadingParams->lpParam = (LPVOID)param;

	// Reset the autosave time
	app.SetAutosaveTimerTime();

	UIFullscreenProgressCompletionData *completionData = new UIFullscreenProgressCompletionData();
	completionData->bShowBackground=TRUE;
	completionData->bShowLogo=TRUE;
	completionData->type = e_ProgressCompletion_CloseAllPlayersUIScenes;
	completionData->iPad = DEFAULT_XUI_MENU_USER;
	loadingParams->completionData = completionData;

	ui.NavigateToScene(ProfileManager.GetPrimaryPad(),eUIScene_FullscreenProgress, loadingParams);
}

int UIScene_LoadMenu::StartGame_SignInReturned(void *pParam,bool bContinue, int iPad)
{
	UIScene_LoadMenu* pClass = (UIScene_LoadMenu*)pParam;

	if(bContinue==true)
	{
		// It's possible that the player has not signed in - they can back out
		if(ProfileManager.IsSignedIn(pClass->m_iPad))
		{
			int primaryPad = ProfileManager.GetPrimaryPad();
			bool noPrivileges = false;
			DWORD dwLocalUsersMask = 0;
			bool isSignedInLive = ProfileManager.IsSignedInLive(primaryPad);
			bool isOnlineGame = pClass->m_MoreOptionsParams.bOnlineGame;
			int iPadNotSignedInLive = -1;
			bool isLocalMultiplayerAvailable = app.IsLocalMultiplayerAvailable();

			for(unsigned int i = 0; i < XUSER_MAX_COUNT; ++i)
			{
				if (ProfileManager.IsSignedIn(i) && ((i == primaryPad) || isLocalMultiplayerAvailable))
				{
					if (isSignedInLive && !ProfileManager.IsSignedInLive(i))
					{
						// Record the first non signed in live pad
						iPadNotSignedInLive = i;
					}

					if( !ProfileManager.AllowedToPlayMultiplayer(i) ) noPrivileges = true;
					dwLocalUsersMask |= CGameNetworkManager::GetLocalPlayerMask(i);
					isSignedInLive = isSignedInLive && ProfileManager.IsSignedInLive(i);
				}
			}

			// If this is an online game but not all players are signed in to Live, stop!
			if (isOnlineGame && !isSignedInLive)
			{
				pClass->m_bIgnoreInput=false;
				UINT uiIDA[1];
				uiIDA[0]=IDS_CONFIRM_OK;
				ui.RequestMessageBox( IDS_PRO_NOTONLINE_TITLE, IDS_PRO_NOTONLINE_TEXT, uiIDA,1,ProfileManager.GetPrimaryPad(),NULL,NULL, app.GetStringTable(),NULL,0,false);
				return 0;
			}

			// Check if user-created content is allowed, as we cannot play multiplayer if it's not
			bool noUGC = false;
			BOOL pccAllowed = TRUE;
			BOOL pccFriendsAllowed = TRUE;

			ProfileManager.AllowedPlayerCreatedContent(ProfileManager.GetPrimaryPad(),false,&pccAllowed,&pccFriendsAllowed);
			if(!pccAllowed && !pccFriendsAllowed) noUGC = true;

			if(isSignedInLive && isOnlineGame && (noPrivileges || noUGC) )
			{
				if( noUGC )
				{
					pClass->m_bIgnoreInput = false;
					pClass->setVisible( true );
					UINT uiIDA[1];
					uiIDA[0]=IDS_CONFIRM_OK;
					ui.RequestMessageBox( IDS_FAILED_TO_CREATE_GAME_TITLE, IDS_NO_USER_CREATED_CONTENT_PRIVILEGE_CREATE, uiIDA,1,ProfileManager.GetPrimaryPad(),NULL,NULL, app.GetStringTable(),NULL,0,false);
				}
				else
				{
					pClass->m_bIgnoreInput = false;
					pClass->setVisible( true );
					UINT uiIDA[1];
					uiIDA[0]=IDS_CONFIRM_OK;
					ui.RequestMessageBox( IDS_NO_MULTIPLAYER_PRIVILEGE_TITLE, IDS_NO_MULTIPLAYER_PRIVILEGE_HOST_TEXT, uiIDA,1,ProfileManager.GetPrimaryPad(),NULL,NULL, app.GetStringTable(),NULL,0,false);
				}
			}
			else
			{
				// This is NOT called from a storage manager thread, and is in fact called from the main thread in the Profile library tick. Therefore we use the main threads IntCache.
				StartGameFromSave(pClass, dwLocalUsersMask);
			}
		}
	}
	else
	{
		pClass->m_bIgnoreInput=false;
	}

	return 0;
}

void UIScene_LoadMenu::handleGainFocus(bool navBack)
{
	if(navBack)
	{
		
#if defined _XBOX_ONE || defined _WINDOWS64
		if(getSceneResolution() == eSceneResolution_1080)
		{
			m_checkboxOnline.setChecked(m_MoreOptionsParams.bOnlineGame == TRUE);
		}
#endif
	}
}
