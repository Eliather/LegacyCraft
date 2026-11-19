#include "stdafx.h"
#include <new>
#include "Common/zlib/zlib.h"
//#include <compressapi.h>

#if defined _DURANGO
#include "Durango\Sentient\SentientManager.h"
#include "StatsCounter.h"
#include "Durango\Social\SocialManager.h"
#include "Durango\Sentient\DynamicConfigurations.h"
#include "Durango\DurangoExtras\xcompress.h"
#elif defined _WINDOWS64
#include "Windows64\Sentient\SentientManager.h"
#include "StatsCounter.h"
#include "Windows64\Social\SocialManager.h"
#include "Windows64\Sentient\DynamicConfigurations.h"
#include "UserData_Info.h"
#elif defined __PSVITA__
#include "PSVita\Sentient\SentientManager.h"
#include "StatsCounter.h"
#include "PSVita\Social\SocialManager.h"
#include "PSVita\Sentient\DynamicConfigurations.h"
#include <libperf.h>
#else
#include "Orbis\Sentient\SentientManager.h"
#include "StatsCounter.h"
#include "Orbis\Social\SocialManager.h"
#include "Orbis\Sentient\DynamicConfigurations.h"
#include <perf.h>
#endif

#if !defined(__PSVITA__)
#ifdef _WINDOWS64
C4JStorage StorageManager;
C_4JProfile ProfileManager;
#endif
#endif // __PS3__
CSentientManager SentientManager;
CXuiStringTable StringTable;

#ifndef _XBOX_ONE
ATG::XMLParser::XMLParser() {}
ATG::XMLParser::~XMLParser() {}
HRESULT    ATG::XMLParser::ParseXMLBuffer( CONST CHAR* strBuffer, UINT uBufferSize ) { return S_OK; }   
VOID ATG::XMLParser::RegisterSAXCallbackInterface( ISAXCallback *pISAXCallback ) {}
#endif

bool	CSocialManager::IsTitleAllowedToPostAnything() { return false; }
bool	CSocialManager::AreAllUsersAllowedToPostImages() { return false; }
bool	CSocialManager::IsTitleAllowedToPostImages() { return false; }

bool	CSocialManager::PostLinkToSocialNetwork( ESocialNetwork eSocialNetwork, DWORD dwUserIndex, bool bUsingKinect ) { return false; }
bool	CSocialManager::PostImageToSocialNetwork( ESocialNetwork eSocialNetwork, DWORD dwUserIndex, bool bUsingKinect ) { return false; }
CSocialManager *CSocialManager::Instance() { return NULL; }
void CSocialManager::SetSocialPostText(LPCWSTR Title, LPCWSTR Caption, LPCWSTR Desc) {};

DWORD XShowPartyUI(DWORD dwUserIndex) { return 0; }
DWORD XShowFriendsUI(DWORD dwUserIndex) { return 0; }
HRESULT XPartyGetUserList(XPARTY_USER_LIST *pUserList) { return S_OK; }
DWORD XContentGetThumbnail(DWORD dwUserIndex, const XCONTENT_DATA *pContentData,  PBYTE pbThumbnail,  PDWORD pcbThumbnail,  PXOVERLAPPED *pOverlapped) { return 0; }
void XShowAchievementsUI(int i) {}
DWORD XBackgroundDownloadSetMode(XBACKGROUND_DOWNLOAD_MODE Mode) { return 0; }

#ifndef _DURANGO
void PIXAddNamedCounter(int a, char *b, ...) {}
//#define PS3_USE_PIX_EVENTS 
//#define PS4_USE_PIX_EVENTS 
void PIXBeginNamedEvent(int a, char *b, ...)
{
#ifdef PS4_USE_PIX_EVENTS
	char buf[512];
    va_list args;
    va_start(args,b);
    vsprintf(buf,b,args);
	sceRazorCpuPushMarker(buf, 0xffffffff, SCE_RAZOR_MARKER_ENABLE_HUD);

#endif
#ifdef PS3_USE_PIX_EVENTS
	char buf[256];
	wchar_t wbuf[256];
    va_list args;
    va_start(args,b);
    vsprintf(buf,b,args);
	snPushMarker(buf);

// 	mbstowcs(wbuf,buf,256);
// 	RenderManager.BeginEvent(wbuf);
    va_end(args);
#endif
}
#if 0//__PSVITA__
	if( PixDepth < 64 )
	{
		char buf[512];
		va_list args;
		va_start(args,b);
		vsprintf(buf,b,args);
		sceRazorCpuPushMarkerWithHud(buf, 0xffffffff, SCE_RAZOR_MARKER_ENABLE_HUD);
	}
	PixDepth += 1;
#endif


void PIXEndNamedEvent()
{
#ifdef PS4_USE_PIX_EVENTS
	sceRazorCpuPopMarker();
#endif
#ifdef PS3_USE_PIX_EVENTS
	snPopMarker();
// 	RenderManager.EndEvent();
#endif
#if 0//__PSVITA__
	if( PixDepth <= 64 )
	{
		sceRazorCpuPopMarker();
	}
	PixDepth -= 1;
#endif
}
void PIXSetMarkerDeprecated(int a, char *b, ...) {}
#else
// 4J Stu - Removed this implementation in favour of a macro that will convert our string format
// conversion at compile time rather than at runtime
//void PIXBeginNamedEvent(int a, char *b, ...)
//{
//	char buf[256];
//	wchar_t wbuf[256];
//	va_list args;
//	va_start(args,b);
//	vsprintf(buf,b,args);
//
//	mbstowcs(wbuf,buf,256);
//	PIXBeginEvent(a,wbuf);
//}
//
//void PIXEndNamedEvent()
//{
//	PIXEndEvent();
//}
//
//void PIXSetMarkerDeprecated(int a, char *b, ...)
//{
//	char buf[256];
//	wchar_t wbuf[256];
//	va_list args;
//	va_start(args,b);
//	vsprintf(buf,b,args);
//
//	mbstowcs(wbuf,buf,256);
//	PIXSetMarker(a, wbuf);
//}
#endif

// void *D3DXBUFFER::GetBufferPointer() { return NULL; }
// int D3DXBUFFER::GetBufferSize() { return 0; }
// void D3DXBUFFER::Release() {}

// #ifdef _DURANGO
// void GetLocalTime(SYSTEMTIME *time) {}
// #endif


bool IsEqualXUID(PlayerUID a, PlayerUID b)
{
#if defined(__PSVITA__) || defined(_DURANGO)
	return (a == b);
#else
	return false;
#endif
}

void XMemCpy(void *a, const void *b, size_t s) { memcpy(a, b, s); }
void XMemSet(void *a, int t, size_t s) { memset(a, t, s); }
void XMemSet128(void *a, int t, size_t s) { memset(a, t, s); }
void *XPhysicalAlloc(SIZE_T a, ULONG_PTR  b, ULONG_PTR c, DWORD d) { return malloc(a); }
void XPhysicalFree(void *a) { free(a); }

D3DXVECTOR3::D3DXVECTOR3() {}
D3DXVECTOR3::D3DXVECTOR3(float x,float y,float z) : x(x), y(y), z(z) {}
D3DXVECTOR3& D3DXVECTOR3::operator += ( CONST D3DXVECTOR3& add ) { x += add.x; y += add.y; z += add.z; return *this; }

BYTE IQNetPlayer::GetSmallId() { return 0; }
void IQNetPlayer::SendData(IQNetPlayer *player, const void *pvData, DWORD dwDataSize, DWORD dwFlags)
{
	app.DebugPrintf("Sending from 0x%x to 0x%x %d bytes\n",this,player,dwDataSize);
}
bool IQNetPlayer::IsSameSystem(IQNetPlayer *player) { return true; }
DWORD IQNetPlayer::GetSendQueueSize( IQNetPlayer *player, DWORD dwFlags ) { return 0; }
DWORD IQNetPlayer::GetCurrentRtt() { return 0; }
bool IQNetPlayer::IsHost() { return this == &IQNet::m_player[0]; }
bool IQNetPlayer::IsGuest() { return false; }
bool IQNetPlayer::IsLocal() { return true; }
PlayerUID IQNetPlayer::GetXuid() { return INVALID_XUID; }
LPCWSTR IQNetPlayer::GetGamertag() { static const wchar_t *test = L"stub"; return test; }
int IQNetPlayer::GetSessionIndex() { return 0; }
bool IQNetPlayer::IsTalking() { return false; }
bool IQNetPlayer::IsMutedByLocalUser(DWORD dwUserIndex) { return false; }
bool IQNetPlayer::HasVoice() { return false; }
bool IQNetPlayer::HasCamera() { return false; }
int IQNetPlayer::GetUserIndex() { return this - &IQNet::m_player[0]; }
void IQNetPlayer::SetCustomDataValue(ULONG_PTR ulpCustomDataValue) {
	m_customData = ulpCustomDataValue;
}
ULONG_PTR IQNetPlayer::GetCustomDataValue() {
	return m_customData;
}

IQNetPlayer IQNet::m_player[4];

bool _bQNetStubGameRunning = false;

HRESULT IQNet::AddLocalPlayerByUserIndex(DWORD dwUserIndex){ return S_OK; }
IQNetPlayer *IQNet::GetHostPlayer() { return &m_player[0]; }
IQNetPlayer *IQNet::GetLocalPlayerByUserIndex(DWORD dwUserIndex) { return &m_player[dwUserIndex]; } 
IQNetPlayer *IQNet::GetPlayerByIndex(DWORD dwPlayerIndex) { return &m_player[0]; }
IQNetPlayer *IQNet::GetPlayerBySmallId(BYTE SmallId){ return &m_player[0]; }
IQNetPlayer *IQNet::GetPlayerByXuid(PlayerUID xuid){ return &m_player[0]; }
DWORD IQNet::GetPlayerCount() { return 1; }
QNET_STATE IQNet::GetState() { return _bQNetStubGameRunning ? QNET_STATE_GAME_PLAY : QNET_STATE_IDLE; }
bool IQNet::IsHost() { return true; }
HRESULT IQNet::JoinGameFromInviteInfo(DWORD dwUserIndex, DWORD dwUserMask, const INVITE_INFO *pInviteInfo) { return S_OK; }
void IQNet::HostGame() { _bQNetStubGameRunning = true; }
void IQNet::EndGame() { _bQNetStubGameRunning = false; }

DWORD MinecraftDynamicConfigurations::GetTrialTime() { return DYNAMIC_CONFIG_DEFAULT_TRIAL_TIME; }

void XSetThreadProcessor(HANDLE a, int b) {}
// #if !(defined __PS3__) && !(defined __ORBIS__)
// BOOL XCloseHandle(HANDLE a) { return CloseHandle(a); }
// #endif // __PS3__

DWORD XUserGetSigninInfo(
         DWORD dwUserIndex,
         DWORD dwFlags,
         PXUSER_SIGNIN_INFO pSigninInfo
)
{
	return 0;
}

LPCWSTR CXuiStringTable::Lookup(LPCWSTR szId) { return szId; }
LPCWSTR CXuiStringTable::Lookup(UINT nIndex) { return L"String"; }
void CXuiStringTable::Clear() {}
HRESULT CXuiStringTable::Load(LPCWSTR szId) { return S_OK; }

DWORD XUserAreUsersFriends( DWORD dwUserIndex, PPlayerUID pXuids, DWORD dwXuidCount, PBOOL pfResult, void *pOverlapped) { return 0; }

#if defined(_XBOX_ONE)
#else
HRESULT XMemDecompress(
         XMEMDECOMPRESSION_CONTEXT Context,
         VOID *pDestination,
         SIZE_T *pDestSize,
         CONST VOID *pSource,
         SIZE_T SrcSize
)
{
	memcpy(pDestination, pSource, SrcSize);
	*pDestSize = SrcSize;
	return S_OK;

	/*
	DECOMPRESSOR_HANDLE Decompressor    = (DECOMPRESSOR_HANDLE)Context;
	if( Decompress(
        Decompressor,           //  Decompressor handle
        (void *)pSource,		//  Compressed data
        SrcSize,				//  Compressed data size
        pDestination,			//  Decompressed buffer
        *pDestSize,				//  Decompressed buffer size
        pDestSize) )				//  Decompressed data size
	{
		return S_OK;
	}
	else
	*/
	{
		return E_FAIL;
	}
}

HRESULT XMemCompress(
         XMEMCOMPRESSION_CONTEXT Context,
         VOID *pDestination,
         SIZE_T *pDestSize,
         CONST VOID *pSource,
         SIZE_T SrcSize
)
{
	memcpy(pDestination, pSource, SrcSize);
	*pDestSize = SrcSize;
	return S_OK;

	/*
	COMPRESSOR_HANDLE Compressor    = (COMPRESSOR_HANDLE)Context;
	if( Compress(
			Compressor,                  //  Compressor Handle
			(void *)pSource,             //  Input buffer, Uncompressed data
			SrcSize,					 //  Uncompressed data size
			pDestination,                //  Compressed Buffer
			*pDestSize,                  //  Compressed Buffer size
			pDestSize)	)				//  Compressed Data size
	{
		return S_OK;
	}
	else
	*/
	{
		return E_FAIL;
	}
}

HRESULT XMemCreateCompressionContext(
         XMEMCODEC_TYPE CodecType,
         CONST VOID *pCodecParams,
         DWORD Flags,
         XMEMCOMPRESSION_CONTEXT *pContext
)
{
	/*
	COMPRESSOR_HANDLE Compressor    = NULL;

	HRESULT hr = CreateCompressor(
		COMPRESS_ALGORITHM_XPRESS_HUFF, //  Compression Algorithm
		NULL,                           //  Optional allocation routine
		&Compressor);                   //  Handle

	pContext = (XMEMDECOMPRESSION_CONTEXT *)Compressor;
	return hr;
	*/
	return 0;
}

HRESULT XMemCreateDecompressionContext(
         XMEMCODEC_TYPE CodecType,
         CONST VOID *pCodecParams,
         DWORD Flags,
         XMEMDECOMPRESSION_CONTEXT *pContext
)
{
	/*
	DECOMPRESSOR_HANDLE  Decompressor    = NULL;

	HRESULT hr = CreateDecompressor(
		COMPRESS_ALGORITHM_XPRESS_HUFF, //  Compression Algorithm
		NULL,                           //  Optional allocation routine
		&Decompressor);                   //  Handle

	pContext = (XMEMDECOMPRESSION_CONTEXT *)Decompressor;
	return hr;
	*/
	return 0;
}

void XMemDestroyCompressionContext(XMEMCOMPRESSION_CONTEXT Context)
{
//	COMPRESSOR_HANDLE Compressor    = (COMPRESSOR_HANDLE)Context;
//	CloseCompressor(Compressor);
}

void XMemDestroyDecompressionContext(XMEMDECOMPRESSION_CONTEXT Context)
{
//	DECOMPRESSOR_HANDLE Decompressor    = (DECOMPRESSOR_HANDLE)Context;
//	CloseDecompressor(Decompressor);
}
#endif

//#ifndef __PS3__
#if !(defined(_DURANGO) || defined(__PSVITA__))
DWORD XGetLanguage() { 
	unsigned char savedLang = UserData_Info::GetLanguageId();
	if (savedLang != MINECRAFT_LANGUAGE_DEFAULT)
	{
		if (savedLang == MINECRAFT_LANGUAGE_BRAZILIAN)
		{
			return XC_LANGUAGE_PORTUGUESE;
		}
		return (DWORD)savedLang;
	}
	return XC_LANGUAGE_SPANISH; 
}
DWORD XGetLocale() {
	unsigned char savedLang = UserData_Info::GetLanguageId();
	if (savedLang == MINECRAFT_LANGUAGE_BRAZILIAN)
	{
		return XC_LOCALE_BRAZIL;
	}
	if (savedLang == MINECRAFT_LANGUAGE_PORTUGUESE)
	{
		return XC_LOCALE_PORTUGAL;
	}
	return XC_LOCALE_SPAIN;
}
DWORD XEnableGuestSignin(BOOL fEnable) { return 0; }
#endif



/////////////////////////////////////////////// Profile library
#ifdef _WINDOWS64
static void *profileData[4];
static bool s_bProfileIsFullVersion;
void				C_4JProfile::Initialise( DWORD dwTitleID,
								DWORD dwOfferID,
								unsigned short usProfileVersion,
								UINT uiProfileValuesC,
								UINT uiProfileSettingsC,
								DWORD *pdwProfileSettingsA, 
								int iGameDefinedDataSizeX4,
								unsigned int *puiGameDefinedDataChangedBitmask)
{
	for( int i = 0; i < 4; i++ )
	{
		profileData[i] = new byte[iGameDefinedDataSizeX4/4];
		ZeroMemory(profileData[i],sizeof(byte)*iGameDefinedDataSizeX4/4);

		// Set some sane initial values!
		GAME_SETTINGS *pGameSettings = (GAME_SETTINGS *)profileData[i];
		pGameSettings->ucMusicVolume=100; //eGameSetting_MusicVolume
		pGameSettings->ucSoundFXVolume=100; //eGameSetting_SoundFXVolume
		pGameSettings->ucGamma=50; //eGameSetting_Gamma
		pGameSettings->ucSensitivity=100; //eGameSetting_Sensitivity_InGame
		pGameSettings->ucMenuSensitivity=100; //eGameSetting_Sensitivity_InMenu
		pGameSettings->usBitmaskValues|=0x0001; //eGameSetting_Difficulty - 1 (Easy)
		pGameSettings->usBitmaskValues|=0x0004; //eGameSetting_ViewBob - on
		pGameSettings->usBitmaskValues|=0x0008; //eGameSetting_GamertagsVisible - on
		pGameSettings->ucInterfaceOpacity=80; //eGameSetting_Sensitivity_InMenu
		pGameSettings->usBitmaskValues|=0x0200; //eGameSetting_DisplaySplitscreenGamertags - on
		pGameSettings->usBitmaskValues|=0x0400; //eGameSetting_Hints - on
		pGameSettings->usBitmaskValues|=0x1000; //eGameSetting_Autosave - 2
		pGameSettings->usBitmaskValues|=0x8000; //eGameSetting_Tooltips - on
		pGameSettings->uiBitmaskValues=0L; // reset
		pGameSettings->uiBitmaskValues|=GAMESETTING_CLOUDS;					//eGameSetting_Clouds - on
		pGameSettings->uiBitmaskValues|=GAMESETTING_ONLINE;					//eGameSetting_GameSetting_Online - on
		pGameSettings->uiBitmaskValues|=GAMESETTING_FRIENDSOFFRIENDS;		//eGameSetting_GameSetting_FriendsOfFriends - on
		pGameSettings->uiBitmaskValues|=GAMESETTING_DISPLAYUPDATEMSG;		//eGameSetting_DisplayUpdateMessage (counter)
		pGameSettings->uiBitmaskValues&=~GAMESETTING_BEDROCKFOG;			//eGameSetting_BedrockFog - off
		pGameSettings->uiBitmaskValues|=GAMESETTING_DISPLAYHUD;				//eGameSetting_DisplayHUD - on
		pGameSettings->uiBitmaskValues|=GAMESETTING_DISPLAYHAND;			//eGameSetting_DisplayHand - on
		pGameSettings->uiBitmaskValues|=GAMESETTING_CUSTOMSKINANIM;			//eGameSetting_CustomSkinAnim - on
		pGameSettings->uiBitmaskValues|=GAMESETTING_DEATHMESSAGES;			//eGameSetting_DeathMessages - on
		pGameSettings->uiBitmaskValues|=(GAMESETTING_UISIZE&0x00000800);				// uisize 2
		pGameSettings->uiBitmaskValues|=(GAMESETTING_UISIZE_SPLITSCREEN&0x00004000);	// splitscreen ui size 3
		pGameSettings->uiBitmaskValues|=GAMESETTING_ANIMATEDCHARACTER;		//eGameSetting_AnimatedCharacter - on

		// TU12
		// favorite skins added, but only set in TU12 - set to FFs
		for(int i=0;i<MAX_FAVORITE_SKINS;i++)
		{
			pGameSettings->uiFavoriteSkinA[i]=0xFFFFFFFF;
		}
		pGameSettings->ucCurrentFavoriteSkinPos=0;
		// Added a bitmask in TU13 to enable/disable display of the Mash-up pack worlds in the saves list
		pGameSettings->uiMashUpPackWorldsDisplay = 0xFFFFFFFF;

		// PS3DEC13
		pGameSettings->uiBitmaskValues&=~GAMESETTING_PS3EULAREAD;		//eGameSetting_PS3_EULA_Read - off

		// PS3 1.05 - added Greek
		unsigned char savedLang = UserData_Info::GetLanguageId();
		if (savedLang != MINECRAFT_LANGUAGE_DEFAULT)
		{
			pGameSettings->ucLanguage = savedLang;
		}
		else
		{
			pGameSettings->ucLanguage = MINECRAFT_LANGUAGE_SPANISH; // force Spanish on Windows64 profile defaults
		}

		// PS Vita - network mode added
		pGameSettings->uiBitmaskValues&=~GAMESETTING_PSVITANETWORKMODEADHOC;		//eGameSetting_PSVita_NetworkModeAdhoc - off


		// Tutorials for most menus, and a few other things
		pGameSettings->ucTutorialCompletion[0] = 0xFF;
		pGameSettings->ucTutorialCompletion[1] = 0xFF;
		pGameSettings->ucTutorialCompletion[2] = 0xF;

		// Has gone halfway through the tutorial
		pGameSettings->ucTutorialCompletion[28] |= 1<<0;
	}
}
void				C_4JProfile::SetTrialTextStringTable(CXuiStringTable *pStringTable,int iAccept,int iReject) {}
void				C_4JProfile::SetTrialAwardText(eAwardType AwardType,int iTitle,int iText) {}
int					C_4JProfile::GetLockedProfile() { return 0; }
void				C_4JProfile::SetLockedProfile(int iProf) {}
bool				C_4JProfile::IsSignedIn(int iQuadrant) { return ( iQuadrant == 0); }
bool				C_4JProfile::IsSignedInLive(int iProf) { return true; }
bool				C_4JProfile::IsGuest(int iQuadrant) { return false; }
UINT				C_4JProfile::RequestSignInUI(bool bFromInvite,bool bLocalGame,bool bNoGuestsAllowed,bool bMultiplayerSignIn,bool bAddUser, int( *Func)(LPVOID,const bool, const int iPad),LPVOID lpParam,int iQuadrant) { return 0; }
UINT				C_4JProfile::DisplayOfflineProfile(int( *Func)(LPVOID,const bool, const int iPad),LPVOID lpParam,int iQuadrant)  { return 0; }
UINT				C_4JProfile::RequestConvertOfflineToGuestUI(int( *Func)(LPVOID,const bool, const int iPad),LPVOID lpParam,int iQuadrant) { return 0; }
void				C_4JProfile::SetPrimaryPlayerChanged(bool bVal) {}
bool				C_4JProfile::QuerySigninStatus(void) { return true; }
void				C_4JProfile::GetXUID(int iPad, PlayerUID *pXuid,bool bOnlineXuid) {*pXuid = 0xe000d45248242f2e; }
BOOL				C_4JProfile::AreXUIDSEqual(PlayerUID xuid1,PlayerUID xuid2) { return false; }
BOOL				C_4JProfile::XUIDIsGuest(PlayerUID xuid) { return false; }
bool				C_4JProfile::AllowedToPlayMultiplayer(int iProf) { return true; }


void				C_4JProfile::StartTrialGame() {}
void				C_4JProfile::AllowedPlayerCreatedContent(int iPad, bool thisQuadrantOnly, BOOL *allAllowed, BOOL *friendsAllowed) {}
BOOL				C_4JProfile::CanViewPlayerCreatedContent(int iPad, bool thisQuadrantOnly, PPlayerUID pXuids, DWORD dwXuidCount ) { return true; }
bool				C_4JProfile::GetProfileAvatar(int iPad,int( *Func)(LPVOID lpParam,PBYTE pbThumbnail,DWORD dwThumbnailBytes), LPVOID lpParam) { return false; }
void				C_4JProfile::CancelProfileAvatarRequest() {}
int					C_4JProfile::GetPrimaryPad() { return 0; }
void				C_4JProfile::SetPrimaryPad(int iPad) {}
#ifdef _DURANGO
char fakeGamerTag[32] = "PlayerName";
void				SetFakeGamertag(char *name){ strcpy_s(fakeGamerTag, name); }
char*				C_4JProfile::GetGamertag(int iPad){ return fakeGamerTag; }
#else
char*				C_4JProfile::GetGamertag(int iPad){ return const_cast<char *>(UserData_Info::GetPlayerNameAnsi()); }
wstring				C_4JProfile::GetDisplayName(int iPad){ return UserData_Info::GetPlayerName(); }
#endif
bool				C_4JProfile::IsFullVersion() { return s_bProfileIsFullVersion; }
void				C_4JProfile::SetSignInChangeCallback(void ( *Func)(LPVOID, bool, unsigned int),LPVOID lpParam) {}
void				C_4JProfile::SetNotificationsCallback(void ( *Func)(LPVOID, DWORD, unsigned int),LPVOID lpParam) {}
bool				C_4JProfile::RegionIsNorthAmerica(void) { return false; }
bool				C_4JProfile::LocaleIsUSorCanada(void) { return false; }
HRESULT				C_4JProfile::GetLiveConnectionStatus() { return S_OK; }
bool				C_4JProfile::IsSystemUIDisplayed() { return false; }
void				C_4JProfile::SetProfileReadErrorCallback(void ( *Func)(LPVOID), LPVOID lpParam) {}
int( *defaultOptionsCallback)(LPVOID,C_4JProfile::PROFILESETTINGS *, const int iPad) = NULL;
LPVOID lpProfileParam = NULL;
int					C_4JProfile::SetDefaultOptionsCallback(int( *Func)(LPVOID,PROFILESETTINGS *, const int iPad),LPVOID lpParam)
{
	defaultOptionsCallback = Func;
	lpProfileParam = lpParam;
	return 0;
}
int					C_4JProfile::SetOldProfileVersionCallback(int( *Func)(LPVOID,unsigned char *, const unsigned short,const int),LPVOID lpParam) { return 0; }

// To store the dashboard preferences for controller flipped, etc.
C_4JProfile::PROFILESETTINGS ProfileSettingsA[XUSER_MAX_COUNT];

C_4JProfile::PROFILESETTINGS *	C_4JProfile::GetDashboardProfileSettings(int iPad) { return &ProfileSettingsA[iPad]; }
void				C_4JProfile::WriteToProfile(int iQuadrant, bool bGameDefinedDataChanged, bool bOverride5MinuteLimitOnProfileWrites) {}
void				C_4JProfile::ForceQueuedProfileWrites(int iPad) {}
void				*C_4JProfile::GetGameDefinedProfileData(int iQuadrant)
{
	// 4J Stu - Don't reset the options when we call this!!
	//defaultOptionsCallback(lpProfileParam, (C_4JProfile::PROFILESETTINGS *)profileData[iQuadrant], iQuadrant);
	//pApp->SetDefaultOptions(pSettings,iPad);

	return profileData[iQuadrant];
}
void				C_4JProfile::ResetProfileProcessState() {}
void				C_4JProfile::Tick( void ) {}
void				C_4JProfile::RegisterAward(int iAwardNumber,int iGamerconfigID, eAwardType eType, bool bLeaderboardAffected, 
	CXuiStringTable*pStringTable, int iTitleStr, int iTextStr, int iAcceptStr, char *pszThemeName, unsigned int ulThemeSize) {}
int					C_4JProfile::GetAwardId(int iAwardNumber) { return 0; }
eAwardType			C_4JProfile::GetAwardType(int iAwardNumber) { return eAwardType_Achievement; }
bool				C_4JProfile::CanBeAwarded(int iQuadrant, int iAwardNumber) { return false; }
void				C_4JProfile::Award(int iQuadrant, int iAwardNumber, bool bForce) {}
bool				C_4JProfile::IsAwardsFlagSet(int iQuadrant, int iAward) { return false; }
void				C_4JProfile::RichPresenceInit(int iPresenceCount, int iContextCount) {}
void				C_4JProfile::RegisterRichPresenceContext(int iGameConfigContextID) {}
void				C_4JProfile::SetRichPresenceContextValue(int iPad,int iContextID, int iVal) {}
void				C_4JProfile::SetCurrentGameActivity(int iPad,int iNewPresence, bool bSetOthersToIdle) {}
void				C_4JProfile::DisplayFullVersionPurchase(bool bRequired, int iQuadrant, int iUpsellParam) {}
void				C_4JProfile::SetUpsellCallback(void ( *Func)(LPVOID lpParam, eUpsellType type, eUpsellResponse response, int iUserData),LPVOID lpParam) {}
void				C_4JProfile::SetDebugFullOverride(bool bVal) {s_bProfileIsFullVersion = bVal;}
void				C_4JProfile::ShowProfileCard(int iPad, PlayerUID targetUid) {}

/////////////////////////////////////////////// Storage library
//#ifdef _WINDOWS64
#if 1
namespace
{
	struct Win64StorageMutex
	{
		CRITICAL_SECTION cs;
		Win64StorageMutex() { InitializeCriticalSection(&cs); }
		~Win64StorageMutex() { DeleteCriticalSection(&cs); }
	} s_win64StorageMutex;

	struct Win64StorageLock
	{
		CRITICAL_SECTION *pcs;
		Win64StorageLock() : pcs(&s_win64StorageMutex.cs) { EnterCriticalSection(pcs); }
		~Win64StorageLock() { LeaveCriticalSection(pcs); }
	};

	BYTE *s_storageSaveBuffer = NULL;
	unsigned int s_storageSaveBufferBytes = 0;
	BYTE *s_storageThumbnailBuffer = NULL;
	DWORD s_storageThumbnailBytes = 0;
	char s_storageSaveUniqueFilename[MAX_SAVEFILENAME_LENGTH] = {0};
	wchar_t s_storageSaveTitle[MAX_DISPLAYNAME_LENGTH] = {0};
	bool s_storageSaveDisabled = false;
	bool s_storageSaveDeviceSelected[XUSER_MAX_COUNT] = { true, true, true, true };
	INT s_storageUniqueSaveCounter = 1;
	SAVE_DETAILS s_storageSaveDetails = { 0, NULL };
	std::vector<SAVE_INFO> s_storageSaveInfos;
	std::vector<BYTE *> s_storageSaveInfoThumbs;

	bool GetWin64GameHddRoot(std::wstring &outRoot)
	{
		wchar_t modulePath[MAX_PATH];
		DWORD len = GetModuleFileNameW(NULL, modulePath, MAX_PATH);
		if(len == 0 || len >= MAX_PATH)
		{
			outRoot = L"Windows64\\GameHDD";
			return true;
		}
		std::wstring exePath(modulePath, len);
		size_t slash = exePath.find_last_of(L"\\/");
		if(slash == std::wstring::npos)
		{
			outRoot = L"Windows64\\GameHDD";
			return true;
		}
		outRoot = exePath.substr(0, slash) + L"\\Windows64\\GameHDD";
		return true;
	}

	bool EnsureWin64GameHddSaveDir(const char *saveId, std::wstring &outSaveDir)
	{
		if(saveId == NULL || saveId[0] == 0)
		{
			return false;
		}
		for(const char *p = saveId; *p != 0; ++p)
		{
			if(*p == '/' || *p == '\\' || *p == ':')
			{
				return false;
			}
		}

		wchar_t modulePath[MAX_PATH];
		DWORD len = GetModuleFileNameW(NULL, modulePath, MAX_PATH);
		std::wstring win64Dir = L"Windows64";
		if(len > 0 && len < MAX_PATH)
		{
			std::wstring exePath(modulePath, len);
			size_t slash = exePath.find_last_of(L"\\/");
			if(slash != std::wstring::npos)
			{
				win64Dir = exePath.substr(0, slash) + L"\\Windows64";
			}
		}
		CreateDirectoryW(win64Dir.c_str(), NULL);
		std::wstring hddDir = win64Dir + L"\\GameHDD";
		CreateDirectoryW(hddDir.c_str(), NULL);

		int wideLen = MultiByteToWideChar(CP_UTF8, 0, saveId, -1, NULL, 0);
		if(wideLen <= 1)
		{
			return false;
		}
		std::wstring wideId((size_t)(wideLen - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, saveId, -1, &wideId[0], wideLen);

		outSaveDir = hddDir + L"\\" + wideId;
		if(!CreateDirectoryW(outSaveDir.c_str(), NULL))
		{
			DWORD err = GetLastError();
			if(err != ERROR_ALREADY_EXISTS)
			{
				return false;
			}
		}
		return true;
	}

	unsigned int ComputeWin64CompressedSaveBytes(const BYTE *buf, unsigned int maxBytes)
	{
		if(buf == NULL || maxBytes <= 8)
		{
			return maxBytes;
		}
		int headerVer = 0;
		memcpy(&headerVer, buf, sizeof(int));
		if(headerVer != 0)
		{
			return maxBytes;
		}

		z_stream zs;
		ZeroMemory(&zs, sizeof(zs));
		zs.next_in = const_cast<Bytef *>(buf + 8);
		zs.avail_in = maxBytes - 8;
		if(inflateInit(&zs) != Z_OK)
		{
			return maxBytes;
		}

		BYTE scratch[4096];
		unsigned int exactBytes = maxBytes;
		int ret = Z_OK;
		while(ret == Z_OK)
		{
			zs.next_out = scratch;
			zs.avail_out = sizeof(scratch);
			ret = inflate(&zs, Z_NO_FLUSH);
			if(ret == Z_STREAM_END)
			{
				if(zs.total_in > 0 && (8 + zs.total_in) <= maxBytes)
				{
					exactBytes = 8 + (unsigned int)zs.total_in;
				}
				break;
			}
		}
		inflateEnd(&zs);
		return exactBytes;
	}

	bool WriteFileAtomicW(const std::wstring &targetPath, const BYTE *data, DWORD sizeBytes)
	{
		if(data == NULL || sizeBytes == 0)
		{
			return false;
		}
		std::wstring tmpPath = targetPath + L".tmp";
		HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if(hFile == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		DWORD totalWritten = 0;
		BOOL writeOk = TRUE;
		while(totalWritten < sizeBytes)
		{
			DWORD chunk = 0;
			if(!WriteFile(hFile, data + totalWritten, sizeBytes - totalWritten, &chunk, NULL) || chunk == 0)
			{
				writeOk = FALSE;
				break;
			}
			totalWritten += chunk;
		}
		CloseHandle(hFile);
		if(!writeOk || totalWritten != sizeBytes)
		{
			DeleteFileW(tmpPath.c_str());
			return false;
		}
		if(!MoveFileExW(tmpPath.c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			DeleteFileW(tmpPath.c_str());
			return false;
		}
		return true;
	}

	bool ReadWholeFileW(const std::wstring &path, std::vector<BYTE> &outBytes)
	{
		outBytes.clear();
		HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if(hFile == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		LARGE_INTEGER fileSize;
		if(!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > 0x7fffffff)
		{
			CloseHandle(hFile);
			return false;
		}
		outBytes.resize((size_t)fileSize.QuadPart);
		DWORD totalRead = 0;
		const DWORD toRead = (DWORD)fileSize.QuadPart;
		while(totalRead < toRead)
		{
			DWORD chunk = 0;
			if(!ReadFile(hFile, &outBytes[totalRead], toRead - totalRead, &chunk, NULL) || chunk == 0)
			{
				CloseHandle(hFile);
				outBytes.clear();
				return false;
			}
			totalRead += chunk;
		}
		CloseHandle(hFile);
		return true;
	}

	void FreeStorageSaveDetailsLocked()
	{
		for(size_t i = 0; i < s_storageSaveInfoThumbs.size(); ++i)
		{
			delete[] s_storageSaveInfoThumbs[i];
		}
		s_storageSaveInfoThumbs.clear();
		s_storageSaveInfos.clear();
		s_storageSaveDetails.iSaveC = 0;
		s_storageSaveDetails.SaveInfoA = NULL;
	}
}

C4JStorage::C4JStorage() {}
void								C4JStorage::Tick() {}
C4JStorage::EMessageResult			C4JStorage::RequestMessageBox(UINT uiTitle, UINT uiText, UINT *uiOptionA,UINT uiOptionC, DWORD dwPad, int( *Func)(LPVOID,int,const C4JStorage::EMessageResult),LPVOID lpParam, C4JStringTable *pStringTable, WCHAR *pwchFormatString,DWORD dwFocusButton) { return C4JStorage::EMessage_Undefined; }
C4JStorage::EMessageResult			C4JStorage::GetMessageBoxResult()  { return C4JStorage::EMessage_Undefined; }
bool								C4JStorage::SetSaveDevice(int( *Func)(LPVOID,const bool),LPVOID lpParam, bool bForceResetOfSaveDevice)
{
	if(Func) Func(lpParam, true);
	return true;
}
void								C4JStorage::Init(unsigned int uiSaveVersion,LPCWSTR pwchDefaultSaveName,char *pszSavePackName,int iMinimumSaveSize,int( *Func)(LPVOID, const ESavingMessage, int),LPVOID lpParam,LPCSTR szGroupID)
{
	(void)uiSaveVersion; (void)pwchDefaultSaveName; (void)pszSavePackName; (void)iMinimumSaveSize; (void)Func; (void)lpParam; (void)szGroupID;
}
void								C4JStorage::ResetSaveData()
{
	Win64StorageLock lock;
	delete[] s_storageSaveBuffer;
	s_storageSaveBuffer = NULL;
	s_storageSaveBufferBytes = 0;
	delete[] s_storageThumbnailBuffer;
	s_storageThumbnailBuffer = NULL;
	s_storageThumbnailBytes = 0;
	ZeroMemory(s_storageSaveUniqueFilename, sizeof(s_storageSaveUniqueFilename));
	ZeroMemory(s_storageSaveTitle, sizeof(s_storageSaveTitle));
}
void								C4JStorage::SetDefaultSaveNameForKeyboardDisplay(LPCWSTR pwchDefaultSaveName)
{
	SetSaveTitle(pwchDefaultSaveName);
}
void								C4JStorage::SetSaveTitle(LPCWSTR pwchDefaultSaveName)
{
	Win64StorageLock lock;
	ZeroMemory(s_storageSaveTitle, sizeof(s_storageSaveTitle));
	if(pwchDefaultSaveName != NULL)
	{
		wcsncpy_s(s_storageSaveTitle, MAX_DISPLAYNAME_LENGTH, pwchDefaultSaveName, _TRUNCATE);
	}
}
bool								C4JStorage::GetSaveUniqueNumber(INT *piVal)
{
	Win64StorageLock lock;
	if(piVal != NULL)
	{
		*piVal = s_storageUniqueSaveCounter++;
	}
	return true;
}
bool								C4JStorage::GetSaveUniqueFilename(char *pszName)
{
	if(pszName == NULL) return false;
	Win64StorageLock lock;
	strncpy_s(pszName, MAX_SAVEFILENAME_LENGTH, s_storageSaveUniqueFilename, _TRUNCATE);
	return (s_storageSaveUniqueFilename[0] != 0);
}
void								C4JStorage::SetSaveUniqueFilename(char *szFilename)
{
	Win64StorageLock lock;
	ZeroMemory(s_storageSaveUniqueFilename, sizeof(s_storageSaveUniqueFilename));
	if(szFilename != NULL)
	{
		strncpy_s(s_storageSaveUniqueFilename, MAX_SAVEFILENAME_LENGTH, szFilename, _TRUNCATE);
	}
}
void								C4JStorage::SetState(ESaveGameControlState eControlState,int( *Func)(LPVOID,const bool),LPVOID lpParam)
{
	(void)eControlState;
	if(Func) Func(lpParam, true);
}
void								C4JStorage::SetSaveDisabled(bool bDisable) { s_storageSaveDisabled = bDisable; }
bool								C4JStorage::GetSaveDisabled(void) { return s_storageSaveDisabled; }
unsigned int						C4JStorage::GetSaveSize()
{
	Win64StorageLock lock;
	return s_storageSaveBufferBytes;
}
void								C4JStorage::GetSaveData(void *pvData,unsigned int *pulBytes)
{
	Win64StorageLock lock;
	if(pvData != NULL && s_storageSaveBuffer != NULL && s_storageSaveBufferBytes > 0)
	{
		memcpy(pvData, s_storageSaveBuffer, s_storageSaveBufferBytes);
	}
	if(pulBytes != NULL)
	{
		*pulBytes = s_storageSaveBufferBytes;
	}
}
PVOID								C4JStorage::AllocateSaveData(unsigned int ulBytes)
{
	if(ulBytes == 0) return NULL;
	Win64StorageLock lock;
	delete[] s_storageSaveBuffer;
	s_storageSaveBuffer = new (std::nothrow) BYTE[ulBytes];
	s_storageSaveBufferBytes = (s_storageSaveBuffer != NULL) ? ulBytes : 0;
	return s_storageSaveBuffer;
}
void								C4JStorage::SetSaveImages( PBYTE pbThumbnail,DWORD dwThumbnailBytes,PBYTE pbImage,DWORD dwImageBytes, PBYTE pbTextData ,DWORD dwTextDataBytes)
{
	(void)pbImage; (void)dwImageBytes; (void)pbTextData; (void)dwTextDataBytes;
	Win64StorageLock lock;
	delete[] s_storageThumbnailBuffer;
	s_storageThumbnailBuffer = NULL;
	s_storageThumbnailBytes = 0;
	if(pbThumbnail != NULL && dwThumbnailBytes > 0)
	{
		s_storageThumbnailBuffer = new (std::nothrow) BYTE[dwThumbnailBytes];
		if(s_storageThumbnailBuffer != NULL)
		{
			memcpy(s_storageThumbnailBuffer, pbThumbnail, dwThumbnailBytes);
			s_storageThumbnailBytes = dwThumbnailBytes;
		}
	}
}
C4JStorage::ESaveGameState			C4JStorage::SaveSaveData(int( *Func)(LPVOID ,const bool),LPVOID lpParam)
{
	bool ok = false;
	{
		Win64StorageLock lock;
		if(!s_storageSaveDisabled && s_storageSaveBuffer != NULL && s_storageSaveBufferBytes > 0)
		{
			if(s_storageSaveUniqueFilename[0] == 0)
			{
				sprintf_s(s_storageSaveUniqueFilename, MAX_SAVEFILENAME_LENGTH, "world%08lx", (unsigned long)GetTickCount());
			}

			std::wstring saveDir;
			if(EnsureWin64GameHddSaveDir(s_storageSaveUniqueFilename, saveDir))
			{
				unsigned int writeSize = ComputeWin64CompressedSaveBytes(s_storageSaveBuffer, s_storageSaveBufferBytes);
				std::wstring saveDataPath = saveDir + L"\\saveData.ms";
				ok = WriteFileAtomicW(saveDataPath, s_storageSaveBuffer, (DWORD)writeSize);
				if(ok && s_storageThumbnailBuffer != NULL && s_storageThumbnailBytes > 0)
				{
					WriteFileAtomicW(saveDir + L"\\thumbnail.png", s_storageThumbnailBuffer, s_storageThumbnailBytes);
				}
			}
		}
	}

	if(Func)
	{
		Func(lpParam, ok);
	}
	return C4JStorage::ESaveGame_Idle;
}
void								C4JStorage::CopySaveDataToNewSave(PBYTE pbThumbnail,DWORD cbThumbnail,WCHAR *wchNewName,int ( *Func)(LPVOID lpParam, bool), LPVOID lpParam)
{
	(void)pbThumbnail; (void)cbThumbnail; (void)wchNewName;
	if(Func) Func(lpParam, false);
}
void								C4JStorage::SetSaveDeviceSelected(unsigned int uiPad,bool bSelected)
{
	if(uiPad < XUSER_MAX_COUNT) s_storageSaveDeviceSelected[uiPad] = bSelected;
}
bool								C4JStorage::GetSaveDeviceSelected(unsigned int iPad)
{
	return (iPad < XUSER_MAX_COUNT) ? s_storageSaveDeviceSelected[iPad] : true;
}
C4JStorage::ESaveGameState			C4JStorage::DoesSaveExist(bool *pbExists)
{
	bool exists = false;
	{
		Win64StorageLock lock;
		if(s_storageSaveUniqueFilename[0] != 0)
		{
			std::wstring hddRoot;
			GetWin64GameHddRoot(hddRoot);
			int wideLen = MultiByteToWideChar(CP_UTF8, 0, s_storageSaveUniqueFilename, -1, NULL, 0);
			if(wideLen > 1)
			{
				std::wstring wideId((size_t)(wideLen - 1), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, s_storageSaveUniqueFilename, -1, &wideId[0], wideLen);
				std::wstring saveDataPath = hddRoot + L"\\" + wideId + L"\\saveData.ms";
				DWORD attrs = GetFileAttributesW(saveDataPath.c_str());
				exists = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
			}
		}
	}
	if(pbExists != NULL)
	{
		*pbExists = exists;
	}
	return C4JStorage::ESaveGame_Idle;
}
bool								C4JStorage::EnoughSpaceForAMinSaveGame() { return true; }
void								C4JStorage::SetSaveMessageVPosition(float fY) { (void)fY; }
C4JStorage::ESaveGameState			C4JStorage::GetSavesInfo(int iPad,int ( *Func)(LPVOID lpParam,SAVE_DETAILS *pSaveDetails,const bool),LPVOID lpParam,char *pszSavePackName)
{
	(void)iPad; (void)pszSavePackName;
	SAVE_DETAILS *pDetails = NULL;
	{
		Win64StorageLock lock;
		FreeStorageSaveDetailsLocked();

		std::wstring hddRoot;
		GetWin64GameHddRoot(hddRoot);
		std::wstring pattern = hddRoot + L"\\*";
		WIN32_FIND_DATAW fd;
		HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
		if(hFind != INVALID_HANDLE_VALUE)
		{
			do
			{
				if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
				if(wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

				std::wstring saveDir = hddRoot + L"\\" + fd.cFileName;
				std::wstring saveDataPath = saveDir + L"\\saveData.ms";
				WIN32_FILE_ATTRIBUTE_DATA dataAttr;
				if(!GetFileAttributesExW(saveDataPath.c_str(), GetFileExInfoStandard, &dataAttr) || (dataAttr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					continue;
				}

				SAVE_INFO info;
				ZeroMemory(&info, sizeof(info));
				WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, info.UTF8SaveFilename, MAX_SAVEFILENAME_LENGTH, NULL, NULL);
				strncpy_s(info.UTF8SaveTitle, MAX_DISPLAYNAME_LENGTH, info.UTF8SaveFilename, _TRUNCATE);

				std::vector<BYTE> titleBytes;
				if(ReadWholeFileW(saveDir + L"\\display_name.txt", titleBytes) && !titleBytes.empty())
				{
					size_t copyLen = titleBytes.size();
					if(copyLen >= MAX_DISPLAYNAME_LENGTH) copyLen = MAX_DISPLAYNAME_LENGTH - 1;
					memcpy(info.UTF8SaveTitle, &titleBytes[0], copyLen);
					info.UTF8SaveTitle[copyLen] = 0;
				}

				ULARGE_INTEGER ft;
				ft.LowPart = dataAttr.ftLastWriteTime.dwLowDateTime;
				ft.HighPart = dataAttr.ftLastWriteTime.dwHighDateTime;
				info.metaData.modifiedTime = (ft.QuadPart >= 116444736000000000ULL) ? (time_t)((ft.QuadPart - 116444736000000000ULL) / 10000000ULL) : 0;
				info.metaData.dataSize = dataAttr.nFileSizeLow;

				std::vector<BYTE> thumbBytes;
				if(ReadWholeFileW(saveDir + L"\\thumbnail.png", thumbBytes) && !thumbBytes.empty())
				{
					BYTE *thumbCopy = new (std::nothrow) BYTE[thumbBytes.size()];
					if(thumbCopy != NULL)
					{
						memcpy(thumbCopy, &thumbBytes[0], thumbBytes.size());
						s_storageSaveInfoThumbs.push_back(thumbCopy);
						info.thumbnailData = thumbCopy;
						info.metaData.thumbnailSize = (unsigned int)thumbBytes.size();
					}
				}

				s_storageSaveInfos.push_back(info);
			}
			while(FindNextFileW(hFind, &fd));
			FindClose(hFind);
		}

		s_storageSaveDetails.iSaveC = (int)s_storageSaveInfos.size();
		s_storageSaveDetails.SaveInfoA = s_storageSaveInfos.empty() ? NULL : &s_storageSaveInfos[0];
		pDetails = &s_storageSaveDetails;
	}

	if(Func)
	{
		Func(lpParam, pDetails, true);
	}
	return C4JStorage::ESaveGame_Idle;
}

void								C4JStorage::GetSaveCacheFileInfo(DWORD dwFile,XCONTENT_DATA &xContentData) { (void)dwFile; (void)xContentData; }
void								C4JStorage::GetSaveCacheFileInfo(DWORD dwFile,	PBYTE *ppbImageData, DWORD *pdwImageBytes) { (void)dwFile; if(ppbImageData) *ppbImageData = NULL; if(pdwImageBytes) *pdwImageBytes = 0; }
C4JStorage::ESaveGameState			C4JStorage::LoadSaveData(PSAVE_INFO pSaveInfo,int( *Func)(LPVOID lpParam,const bool, const bool), LPVOID lpParam)
{
	bool isCorrupt = true;
	if(pSaveInfo != NULL && pSaveInfo->UTF8SaveFilename[0] != 0)
	{
		std::wstring hddRoot;
		GetWin64GameHddRoot(hddRoot);
		int wideLen = MultiByteToWideChar(CP_UTF8, 0, pSaveInfo->UTF8SaveFilename, -1, NULL, 0);
		if(wideLen > 1)
		{
			std::wstring wideId((size_t)(wideLen - 1), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, pSaveInfo->UTF8SaveFilename, -1, &wideId[0], wideLen);
			std::vector<BYTE> fileBytes;
			if(ReadWholeFileW(hddRoot + L"\\" + wideId + L"\\saveData.ms", fileBytes) && !fileBytes.empty())
			{
				Win64StorageLock lock;
				delete[] s_storageSaveBuffer;
				s_storageSaveBuffer = new (std::nothrow) BYTE[fileBytes.size()];
				if(s_storageSaveBuffer != NULL)
				{
					memcpy(s_storageSaveBuffer, &fileBytes[0], fileBytes.size());
					s_storageSaveBufferBytes = (unsigned int)fileBytes.size();
					strncpy_s(s_storageSaveUniqueFilename, MAX_SAVEFILENAME_LENGTH, pSaveInfo->UTF8SaveFilename, _TRUNCATE);
					isCorrupt = false;
				}
				else
				{
					s_storageSaveBufferBytes = 0;
				}
			}
		}
	}

	if(Func)
	{
		Func(lpParam, isCorrupt, true);
	}
	return C4JStorage::ESaveGame_Idle;
}
C4JStorage::ESaveGameState			C4JStorage::DeleteSaveData(PSAVE_INFO pSaveInfo,int( *Func)(LPVOID lpParam,const bool), LPVOID lpParam)
{
	bool ok = false;
	if(pSaveInfo != NULL && pSaveInfo->UTF8SaveFilename[0] != 0)
	{
		bool safe = true;
		for(const char *p = pSaveInfo->UTF8SaveFilename; *p != 0; ++p)
		{
			if(*p == '/' || *p == '\\' || *p == ':') { safe = false; break; }
		}
		if(safe)
		{
			std::wstring hddRoot;
			GetWin64GameHddRoot(hddRoot);
			int wideLen = MultiByteToWideChar(CP_UTF8, 0, pSaveInfo->UTF8SaveFilename, -1, NULL, 0);
			if(wideLen > 1)
			{
				std::wstring wideId((size_t)(wideLen - 1), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, pSaveInfo->UTF8SaveFilename, -1, &wideId[0], wideLen);
				std::wstring targetDir = hddRoot + L"\\" + wideId;
				WIN32_FIND_DATAW fd;
				HANDLE hFind = FindFirstFileW((targetDir + L"\\*").c_str(), &fd);
				if(hFind != INVALID_HANDLE_VALUE)
				{
					do
					{
						if(wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
						DeleteFileW((targetDir + L"\\" + fd.cFileName).c_str());
					}
					while(FindNextFileW(hFind, &fd));
					FindClose(hFind);
				}
				ok = (RemoveDirectoryW(targetDir.c_str()) != FALSE);
			}
		}
	}
	if(Func)
	{
		Func(lpParam, ok);
	}
	return C4JStorage::ESaveGame_Idle;
}
void								C4JStorage::ClearSavesInfo()
{
	Win64StorageLock lock;
	FreeStorageSaveDetailsLocked();
}
C4JStorage::ESaveGameState			C4JStorage::LoadSaveDataThumbnail(PSAVE_INFO pSaveInfo,int( *Func)(LPVOID lpParam,PBYTE pbThumbnail,DWORD dwThumbnailBytes), LPVOID lpParam)
{
	std::vector<BYTE> thumbBytes;
	if(pSaveInfo != NULL && pSaveInfo->UTF8SaveFilename[0] != 0)
	{
		std::wstring hddRoot;
		GetWin64GameHddRoot(hddRoot);
		int wideLen = MultiByteToWideChar(CP_UTF8, 0, pSaveInfo->UTF8SaveFilename, -1, NULL, 0);
		if(wideLen > 1)
		{
			std::wstring wideId((size_t)(wideLen - 1), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, pSaveInfo->UTF8SaveFilename, -1, &wideId[0], wideLen);
			ReadWholeFileW(hddRoot + L"\\" + wideId + L"\\thumbnail.png", thumbBytes);
		}
	}
	if(Func)
	{
		Func(lpParam, thumbBytes.empty() ? NULL : &thumbBytes[0], (DWORD)thumbBytes.size());
	}
	return thumbBytes.empty() ? C4JStorage::ESaveGame_Idle : C4JStorage::ESaveGame_GetSaveThumbnail;
}
PSAVE_DETAILS						C4JStorage::ReturnSavesInfo()
{
	Win64StorageLock lock;
	return &s_storageSaveDetails;
}

void								C4JStorage::RegisterMarketplaceCountsCallback(int ( *Func)(LPVOID lpParam, C4JStorage::DLC_TMS_DETAILS *, int), LPVOID lpParam ) {}
void								C4JStorage::SetDLCPackageRoot(char *pszDLCRoot) {}
C4JStorage::EDLCStatus				C4JStorage::GetDLCOffers(int iPad,int( *Func)(LPVOID, int, DWORD, int),LPVOID lpParam, DWORD dwOfferTypesBitmaskT) { return C4JStorage::EDLC_Idle; }
DWORD								C4JStorage::CancelGetDLCOffers() { return 0; }
void								C4JStorage::ClearDLCOffers() {}
XMARKETPLACE_CONTENTOFFER_INFO&		C4JStorage::GetOffer(DWORD dw) { static XMARKETPLACE_CONTENTOFFER_INFO retval = {0}; return retval; }
int									C4JStorage::GetOfferCount() { return 0; }
DWORD								C4JStorage::InstallOffer(int iOfferIDC,__uint64 *ullOfferIDA,int( *Func)(LPVOID, int, int),LPVOID lpParam, bool bTrial) { return 0; }
DWORD								C4JStorage::GetAvailableDLCCount( int iPad) { return 0; }
XCONTENT_DATA&						C4JStorage::GetDLC(DWORD dw) { static XCONTENT_DATA retval = {0}; return retval; }
C4JStorage::EDLCStatus				C4JStorage::GetInstalledDLC(int iPad,int( *Func)(LPVOID, int, int),LPVOID lpParam)
{
	if(Func)
	{
		Func(lpParam, 0, iPad);
	}
	return C4JStorage::EDLC_NoInstalledDLC;
}
DWORD								C4JStorage::MountInstalledDLC(int iPad,DWORD dwDLC,int( *Func)(LPVOID, int, DWORD,DWORD),LPVOID lpParam,LPCSTR szMountDrive) { return 0; }
DWORD								C4JStorage::UnmountInstalledDLC(LPCSTR szMountDrive) { return 0; }
void								C4JStorage::GetMountedDLCFileList(const char* szMountDrive, std::vector<std::string>& fileList) {}
std::string							C4JStorage::GetMountedPath(std::string szMount) { return ""; }
C4JStorage::ETMSStatus				C4JStorage::ReadTMSFile(int iQuadrant,eGlobalStorage eStorageFacility,C4JStorage::eTMS_FileType eFileType, WCHAR *pwchFilename,BYTE **ppBuffer,DWORD *pdwBufferSize,int( *Func)(LPVOID, WCHAR *,int, bool, int),LPVOID lpParam, int iAction) { return C4JStorage::ETMSStatus_Idle; }
bool								C4JStorage::WriteTMSFile(int iQuadrant,eGlobalStorage eStorageFacility,WCHAR *pwchFilename,BYTE *pBuffer,DWORD dwBufferSize) { return true; }
bool								C4JStorage::DeleteTMSFile(int iQuadrant,eGlobalStorage eStorageFacility,WCHAR *pwchFilename) { return true; }
void								C4JStorage::StoreTMSPathName(WCHAR *pwchName) {}
unsigned int						C4JStorage::CRC(unsigned char *buf, int len) { return 0; }

struct PTMSPP_FILEDATA;
C4JStorage::ETMSStatus				C4JStorage::TMSPP_ReadFile(int iPad,C4JStorage::eGlobalStorage eStorageFacility,C4JStorage::eTMS_FILETYPEVAL eFileTypeVal,LPCSTR szFilename,int( *Func)(LPVOID,int,int,PTMSPP_FILEDATA, LPCSTR)/*=NULL*/,LPVOID lpParam/*=NULL*/, int iUserData/*=0*/) {return C4JStorage::ETMSStatus_Idle;}
#endif // _WINDOWS64

#endif // __PS3__

/////////////////////////////////////////////////////// Sentient manager

HRESULT CSentientManager::Init() { return S_OK; }
HRESULT CSentientManager::Tick() { return S_OK; }
HRESULT CSentientManager::Flush() { return S_OK; }
BOOL CSentientManager::RecordPlayerSessionStart(DWORD dwUserId) { return true; }
BOOL CSentientManager::RecordPlayerSessionExit(DWORD dwUserId, int exitStatus) { return true; }
BOOL CSentientManager::RecordHeartBeat(DWORD dwUserId) { return true; }
BOOL CSentientManager::RecordLevelStart(DWORD dwUserId, ESen_FriendOrMatch friendsOrMatch, ESen_CompeteOrCoop competeOrCoop, int difficulty, DWORD numberOfLocalPlayers, DWORD numberOfOnlinePlayers) { return true; }
BOOL CSentientManager::RecordLevelExit(DWORD dwUserId, ESen_LevelExitStatus levelExitStatus) { return true; }
BOOL CSentientManager::RecordLevelSaveOrCheckpoint(DWORD dwUserId, INT saveOrCheckPointID, INT saveSizeInBytes) { return true; }
BOOL CSentientManager::RecordLevelResume(DWORD dwUserId, ESen_FriendOrMatch friendsOrMatch, ESen_CompeteOrCoop competeOrCoop, int difficulty, DWORD numberOfLocalPlayers, DWORD numberOfOnlinePlayers, INT saveOrCheckPointID)  { return true; }
BOOL CSentientManager::RecordPauseOrInactive(DWORD dwUserId)  { return true; }
BOOL CSentientManager::RecordUnpauseOrActive(DWORD dwUserId) { return true; }
BOOL CSentientManager::RecordMenuShown(DWORD dwUserId, INT menuID, INT optionalMenuSubID) { return true; }
BOOL CSentientManager::RecordAchievementUnlocked(DWORD dwUserId, INT achievementID, INT achievementGamerscore) { return true; }
BOOL CSentientManager::RecordMediaShareUpload(DWORD dwUserId, ESen_MediaDestination mediaDestination, ESen_MediaType mediaType) { return true; }
BOOL CSentientManager::RecordUpsellPresented(DWORD dwUserId, ESen_UpsellID upsellId, INT marketplaceOfferID) { return true; }
BOOL CSentientManager::RecordUpsellResponded(DWORD dwUserId, ESen_UpsellID upsellId, INT marketplaceOfferID, ESen_UpsellOutcome upsellOutcome) { return true; }
BOOL CSentientManager::RecordPlayerDiedOrFailed(DWORD dwUserId, INT lowResMapX, INT lowResMapY, INT lowResMapZ, INT mapID, INT playerWeaponID, INT enemyWeaponID, ETelemetryChallenges enemyTypeID) { return true; }
BOOL CSentientManager::RecordEnemyKilledOrOvercome(DWORD dwUserId, INT lowResMapX, INT lowResMapY, INT lowResMapZ, INT mapID, INT playerWeaponID, INT enemyWeaponID, ETelemetryChallenges enemyTypeID) { return true; }
BOOL CSentientManager::RecordSkinChanged(DWORD dwUserId, DWORD dwSkinId) { return true; }
BOOL CSentientManager::RecordBanLevel(DWORD dwUserId) { return true; }
BOOL CSentientManager::RecordUnBanLevel(DWORD dwUserId) { return true; }
INT CSentientManager::GetMultiplayerInstanceID() { return 0; }
INT CSentientManager::GenerateMultiplayerInstanceId() { return 0; }
void CSentientManager::SetMultiplayerInstanceId(INT value) {}

////////////////////////////////////////////////////////  Stats counter

/*
StatsCounter::StatsCounter() {}
void StatsCounter::award(Stat *stat, unsigned int difficulty, unsigned int count) {}
bool StatsCounter::hasTaken(Achievement *ach) { return true; }
bool StatsCounter::canTake(Achievement *ach) { return true; }
unsigned int StatsCounter::getValue(Stat *stat, unsigned int difficulty) { return 0; }
unsigned int StatsCounter::getTotalValue(Stat *stat) { return 0; }
void StatsCounter::tick(int player) {}
void StatsCounter::parse(void* data) {}
void StatsCounter::clear() {}
void StatsCounter::save(int player, bool force) {}
void StatsCounter::flushLeaderboards() {}
void StatsCounter::saveLeaderboards() {}
void StatsCounter::setupStatBoards() {}
#ifdef _DEBUG
void StatsCounter::WipeLeaderboards() {}
#endif
*/
