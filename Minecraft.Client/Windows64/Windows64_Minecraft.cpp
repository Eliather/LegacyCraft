// Minecraft.cpp : Defines the entry point for the application.
//

#include "stdafx.h"

#include <assert.h>
#include <mmsystem.h>
#include "GameConfig\Minecraft.spa.h"
#include "..\MinecraftServer.h"
#include "..\LocalPlayer.h"
#include "..\..\Minecraft.World\ItemInstance.h"
#include "..\..\Minecraft.World\MapItem.h"
#include "..\..\Minecraft.World\Recipes.h"
#include "..\..\Minecraft.World\Recipy.h"
#include "..\..\Minecraft.World\Language.h"
#include "..\..\Minecraft.World\StringHelpers.h"
#include "..\..\Minecraft.World\AABB.h"
#include "..\..\Minecraft.World\Vec3.h"
#include "..\..\Minecraft.World\Level.h"
#include "..\..\Minecraft.World\net.minecraft.world.level.tile.h"
#include "..\UserData_Info.h"
#include "..\ShaderManager.h"

#include "..\ClientConnection.h"
#include "..\User.h"
#include "..\..\Minecraft.World\Socket.h"
#include "..\..\Minecraft.World\ThreadName.h"
#include "..\..\Minecraft.Client\StatsCounter.h"
#include "..\..\Minecraft.Client\GameRenderer.h"
#include "..\ConnectScreen.h"
//#include "Social\SocialManager.h"
//#include "Leaderboards\LeaderboardManager.h"
//#include "XUI\XUI_Scene_Container.h"
//#include "NetworkManager.h"
#include "..\..\Minecraft.Client\Tesselator.h"
#include "..\..\Minecraft.Client\Options.h"
#include "Sentient\SentientManager.h"
#include "..\..\Minecraft.World\IntCache.h"
#include "..\Textures.h"
#include "Resource.h"
#include "..\..\Minecraft.World\compression.h"
#include "..\..\Minecraft.World\OldChunkStorage.h"

// Force dedicated GPU on NVIDIA Optimus and AMD Dual Graphics systems.
// When pAdapter is NULL in D3D11CreateDeviceAndSwapChain, Windows picks the
// default adapter which is usually the integrated GPU on laptops. These exports
// are read by the NVIDIA and AMD drivers at load time and override that default.
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }
extern "C" { __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1; }

HINSTANCE hMyInst;
LRESULT CALLBACK DlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
char chGlobalText[256];
uint16_t ui16GlobalText[256];

#define THEME_NAME		"584111F70AAAAAAA"
#define THEME_FILESIZE	2797568

#define FIFTY_ONE_MB (1000000*51)

#define NUM_PROFILE_VALUES	5
#define NUM_PROFILE_SETTINGS 4
DWORD dwProfileSettingsA[NUM_PROFILE_VALUES]=
{
#ifdef _XBOX
	XPROFILE_OPTION_CONTROLLER_VIBRATION,
	XPROFILE_GAMER_YAXIS_INVERSION,
	XPROFILE_GAMER_CONTROL_SENSITIVITY,
	XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
	XPROFILE_TITLE_SPECIFIC1,
#else
	0,0,0,0,0
#endif
};

BOOL g_bWidescreen = TRUE;

int g_iScreenWidth = 1920;
int g_iScreenHeight = 1080;
bool g_bWindows64Fullscreen = false;

extern HWND g_hWnd;

static bool g_bRenderInitialized = false;
static bool g_bInRuntimeResize = false;
static int  g_iCliRendererOverride = -1;
static int  g_iCliVsyncOverride = -1;
static int  g_iSmokeFrames = 0;

static void UpdateMouseLockCenter();

static void GetWindows64ResolutionModeSize(int resolutionMode, int &width, int &height, bool &fullscreen)
{
	fullscreen = false;

	switch(resolutionMode)
	{
	case 1:
		width = 1280;
		height = 720;
		break;
	case 2:
		width = 1366;
		height = 768;
		break;
	case 3:
		width = 1920;
		height = 1080;
		break;
	case 4:
		width = GetSystemMetrics(SM_CXSCREEN);
		height = GetSystemMetrics(SM_CYSCREEN);
		fullscreen = true;
		break;
	default:
		width = 1920;
		height = 1080;
		break;
	}

	if(width <= 0 || height <= 0)
	{
		width = GetSystemMetrics(SM_CXSCREEN);
		height = GetSystemMetrics(SM_CYSCREEN);
		fullscreen = false;
	}
}

static void ComputeFittedWindowPlacement(HWND referenceWnd, int reqClientW, int reqClientH, bool fullscreen,
	DWORD style, int &outX, int &outY, int &outWinW, int &outWinH, int &outClientW, int &outClientH)
{
	RECT targetRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
	if (fullscreen)
	{
		HMONITOR hMon = referenceWnd ? MonitorFromWindow(referenceWnd, MONITOR_DEFAULTTONEAREST)
		                             : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		if (hMon && GetMonitorInfo(hMon, &mi))
		{
			targetRect = mi.rcMonitor;
		}
		outClientW = targetRect.right - targetRect.left;
		outClientH = targetRect.bottom - targetRect.top;
		outX = targetRect.left;
		outY = targetRect.top;
		outWinW = outClientW;
		outWinH = outClientH;
		return;
	}

	RECT workRect = targetRect;
	HMONITOR hMon = referenceWnd ? MonitorFromWindow(referenceWnd, MONITOR_DEFAULTTONEAREST)
	                             : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	if (hMon && GetMonitorInfo(hMon, &mi))
	{
		workRect = mi.rcWork;
	}
	else
	{
		SystemParametersInfo(SPI_GETWORKAREA, 0, &workRect, 0);
	}

	const int workW = workRect.right - workRect.left;
	const int workH = workRect.bottom - workRect.top;

	RECT frameRect = { 0, 0, 0, 0 };
	AdjustWindowRect(&frameRect, style, FALSE);
	const int nonClientW = frameRect.right - frameRect.left;
	const int nonClientH = frameRect.bottom - frameRect.top;

	const int maxClientW = (workW > nonClientW + 320) ? (workW - nonClientW) : workW;
	const int maxClientH = (workH > nonClientH + 240) ? (workH - nonClientH) : workH;

	outClientW = reqClientW;
	outClientH = reqClientH;
	if (outClientW > maxClientW || outClientH > maxClientH)
	{
		const float scaleW = (float)maxClientW / (float)reqClientW;
		const float scaleH = (float)maxClientH / (float)reqClientH;
		const float scale = (scaleW < scaleH) ? scaleW : scaleH;
		outClientW = (int)((float)reqClientW * scale);
		outClientH = (int)((float)reqClientH * scale);
		if (outClientW < 320) outClientW = 320;
		if (outClientH < 240) outClientH = 240;
	}

	outWinW = outClientW + nonClientW;
	outWinH = outClientH + nonClientH;
	outX = workRect.left + (workW - outWinW) / 2;
	outY = workRect.top + (workH - outWinH) / 2;
	if (outX < workRect.left) outX = workRect.left;
	if (outY < workRect.top) outY = workRect.top;
}

static void ApplyWindows64ResolutionMode(int resolutionMode)
{
	GetWindows64ResolutionModeSize(resolutionMode, g_iScreenWidth, g_iScreenHeight, g_bWindows64Fullscreen);
}

static void SyncEngineViewportDimensions(int width, int height)
{
	if (width <= 0 || height <= 0) return;
	g_iScreenWidth = width;
	g_iScreenHeight = height;
	UpdateMouseLockCenter();

	Minecraft *minecraft = Minecraft::GetInstance();
	if (minecraft != NULL)
	{
		minecraft->width_phys = width;
		minecraft->height_phys = height;
		minecraft->width = RenderManager.IsWidescreen() ? width : ((width * 3) / 4);
		minecraft->height = height;
	}
}

bool Windows64ApplyResolutionModeRuntime(int resolutionMode)
{
	int reqW = 1920;
	int reqH = 1080;
	bool fullscreen = false;
	GetWindows64ResolutionModeSize(resolutionMode, reqW, reqH, fullscreen);

	const DWORD style = fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
	int windowX = 0, windowY = 0, windowWidth = reqW, windowHeight = reqH, clientW = reqW, clientH = reqH;
	ComputeFittedWindowPlacement(g_hWnd, reqW, reqH, fullscreen, style,
		windowX, windowY, windowWidth, windowHeight, clientW, clientH);

	if (g_hWnd == NULL)
	{
		g_iScreenWidth = clientW;
		g_iScreenHeight = clientH;
		g_bWindows64Fullscreen = fullscreen;
		return true;
	}

	g_bInRuntimeResize = true;
	g_bWindows64Fullscreen = fullscreen;
	SetWindowLongPtr(g_hWnd, GWL_STYLE, (LONG_PTR)style);
	SetWindowPos(g_hWnd, NULL, windowX, windowY, windowWidth, windowHeight,
		SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

	RECT actualRc = {};
	if (GetClientRect(g_hWnd, &actualRc))
	{
		if (actualRc.right - actualRc.left > 0) clientW = actualRc.right - actualRc.left;
		if (actualRc.bottom - actualRc.top > 0) clientH = actualRc.bottom - actualRc.top;
	}

	bool resized = true;
	if (g_bRenderInitialized)
	{
		resized = RenderManager.ResizeWindow(clientW, clientH, fullscreen);
		if (resized)
		{
			ui.updateRenderTargets(clientW, clientH);
		}
	}

	SyncEngineViewportDimensions(clientW, clientH);
	g_bInRuntimeResize = false;
	return resized;
}

void DefineActions(void)
{
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_A,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_B,							_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_X,							_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_Y,							_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OK,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_CANCEL,						_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_UP,							_360_JOY_BUTTON_DPAD_UP | _360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_DOWN,						_360_JOY_BUTTON_DPAD_DOWN | _360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_LEFT,						_360_JOY_BUTTON_DPAD_LEFT | _360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_RIGHT,						_360_JOY_BUTTON_DPAD_RIGHT | _360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_PAGEUP,						_360_JOY_BUTTON_LT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_PAGEDOWN,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_RIGHT_SCROLL,				_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_LEFT_SCROLL,					_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_PAUSEMENU,					_360_JOY_BUTTON_START);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_STICK_PRESS,					_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_PRESS,			_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_DOWN,			_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_LEFT,			_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,ACTION_MENU_OTHER_STICK_RIGHT,			_360_JOY_BUTTON_RSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_JUMP,					_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_FORWARD,				_360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_BACKWARD,				_360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LEFT,					_360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_RIGHT,					_360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LOOK_LEFT,				_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LOOK_RIGHT,				_360_JOY_BUTTON_RSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LOOK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LOOK_DOWN,				_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_USE,					_360_JOY_BUTTON_LT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_ACTION,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_RIGHT_SCROLL,			_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_LEFT_SCROLL,			_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_INVENTORY,				_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_PAUSEMENU,				_360_JOY_BUTTON_START);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DROP,					_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_SNEAK_TOGGLE,			_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_CRAFTING,				_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_RENDER_THIRD_PERSON,	_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_GAME_INFO,				_360_JOY_BUTTON_BACK);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DPAD_LEFT,				_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DPAD_RIGHT,				_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DPAD_UP,				_360_JOY_BUTTON_DPAD_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_0,MINECRAFT_ACTION_DPAD_DOWN,				_360_JOY_BUTTON_DPAD_DOWN);

	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_A,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_B,							_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_X,							_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_Y,							_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OK,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_CANCEL,						_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_UP,							_360_JOY_BUTTON_DPAD_UP | _360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_DOWN,						_360_JOY_BUTTON_DPAD_DOWN | _360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_LEFT,						_360_JOY_BUTTON_DPAD_LEFT | _360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_RIGHT,						_360_JOY_BUTTON_DPAD_RIGHT | _360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_PAGEUP,						_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_PAGEDOWN,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_RIGHT_SCROLL,				_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_LEFT_SCROLL,					_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_PAUSEMENU,					_360_JOY_BUTTON_START);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_STICK_PRESS,					_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_PRESS,			_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_DOWN,			_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_LEFT,			_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,ACTION_MENU_OTHER_STICK_RIGHT,			_360_JOY_BUTTON_RSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_JUMP,					_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_FORWARD,				_360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_BACKWARD,				_360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LEFT,					_360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_RIGHT,					_360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LOOK_LEFT,				_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LOOK_RIGHT,				_360_JOY_BUTTON_RSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LOOK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LOOK_DOWN,				_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_USE,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_ACTION,					_360_JOY_BUTTON_LT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_RIGHT_SCROLL,			_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_LEFT_SCROLL,			_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_INVENTORY,				_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_PAUSEMENU,				_360_JOY_BUTTON_START);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DROP,					_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_SNEAK_TOGGLE,			_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_CRAFTING,				_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_RENDER_THIRD_PERSON,	_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_GAME_INFO,				_360_JOY_BUTTON_BACK);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DPAD_LEFT,				_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DPAD_RIGHT,				_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DPAD_UP,				_360_JOY_BUTTON_DPAD_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_1,MINECRAFT_ACTION_DPAD_DOWN,				_360_JOY_BUTTON_DPAD_DOWN);

	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_A,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_B,							_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_X,							_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_Y,							_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OK,							_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_CANCEL,						_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_UP,							_360_JOY_BUTTON_DPAD_UP | _360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_DOWN,						_360_JOY_BUTTON_DPAD_DOWN | _360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_LEFT,						_360_JOY_BUTTON_DPAD_LEFT | _360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_RIGHT,						_360_JOY_BUTTON_DPAD_RIGHT | _360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_PAGEUP,						_360_JOY_BUTTON_DPAD_UP | _360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_PAGEDOWN,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_RIGHT_SCROLL,				_360_JOY_BUTTON_RB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_LEFT_SCROLL,					_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_JUMP,					_360_JOY_BUTTON_LT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_FORWARD,				_360_JOY_BUTTON_LSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_BACKWARD,				_360_JOY_BUTTON_LSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LEFT,					_360_JOY_BUTTON_LSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_RIGHT,					_360_JOY_BUTTON_LSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LOOK_LEFT,				_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LOOK_RIGHT,				_360_JOY_BUTTON_RSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LOOK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LOOK_DOWN,				_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_USE,					_360_JOY_BUTTON_RT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_ACTION,					_360_JOY_BUTTON_A);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_RIGHT_SCROLL,			_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_LEFT_SCROLL,			_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_INVENTORY,				_360_JOY_BUTTON_Y);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_PAUSEMENU,				_360_JOY_BUTTON_START);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DROP,					_360_JOY_BUTTON_B);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_SNEAK_TOGGLE,			_360_JOY_BUTTON_LB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_CRAFTING,				_360_JOY_BUTTON_X);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_RENDER_THIRD_PERSON,	_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_GAME_INFO,				_360_JOY_BUTTON_BACK);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_PAUSEMENU,					_360_JOY_BUTTON_START);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_STICK_PRESS,					_360_JOY_BUTTON_LTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_PRESS,			_360_JOY_BUTTON_RTHUMB);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_UP,				_360_JOY_BUTTON_RSTICK_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_DOWN,			_360_JOY_BUTTON_RSTICK_DOWN);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_LEFT,			_360_JOY_BUTTON_RSTICK_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,ACTION_MENU_OTHER_STICK_RIGHT,			_360_JOY_BUTTON_RSTICK_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DPAD_LEFT,				_360_JOY_BUTTON_DPAD_LEFT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DPAD_RIGHT,				_360_JOY_BUTTON_DPAD_RIGHT);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DPAD_UP,				_360_JOY_BUTTON_DPAD_UP);
	InputManager.SetGameJoypadMaps(MAP_STYLE_2,MINECRAFT_ACTION_DPAD_DOWN,				_360_JOY_BUTTON_DPAD_DOWN);
}

#if 0
HRESULT InitD3D( IDirect3DDevice9 **ppDevice, D3DPRESENT_PARAMETERS *pd3dPP ) { return S_OK; }
#endif

//#define MEMORY_TRACKING

#ifdef MEMORY_TRACKING
void ResetMem();
void DumpMem();
void MemPixStuff();
#else
void MemSect(int sect) {}
#endif

HINSTANCE               g_hInst = NULL;
HWND                    g_hWnd = NULL;
static bool             g_bMouseLocked = false;
static int              g_iMouseLockCenterX = 0;
static int              g_iMouseLockCenterY = 0;
static float            g_fMouseDeltaRemainderX = 0.0f;
static float            g_fMouseDeltaRemainderY = 0.0f;
static bool             g_bIgnoreNextCenterWarp = false;
static DWORD            g_dwLastRawInputTick = 0;
static bool             g_bHasRawInput = false;
static bool             g_bRawInputRegistered = false;

static bool IsGameWindowFocused(HWND hWnd)
{
	if (hWnd == NULL || !IsWindow(hWnd) || IsIconic(hWnd))
	{
		return false;
	}
	HWND fg = GetForegroundWindow();
	return (fg == hWnd) || (fg == NULL && GetFocus() == hWnd);
}

static void UpdateMouseLockCenter()
{
	if (g_hWnd == NULL) return;
	RECT clientRect;
	GetClientRect(g_hWnd, &clientRect);
	g_iMouseLockCenterX = (clientRect.right - clientRect.left) / 2;
	g_iMouseLockCenterY = (clientRect.bottom - clientRect.top) / 2;
}

static void ConvertClientToRenderCoords(HWND hWnd, int &x, int &y)
{
	RECT clientRect;
	GetClientRect(hWnd, &clientRect);
	const int clientW = clientRect.right - clientRect.left;
	const int clientH = clientRect.bottom - clientRect.top;
	if (clientW <= 0 || clientH <= 0) return;
	if (x < 0) x = 0; if (y < 0) y = 0;
	if (x > clientW) x = clientW; if (y > clientH) y = clientH;
	x = (x * g_iScreenWidth) / clientW;
	y = (y * g_iScreenHeight) / clientH;
	if (x < 0) x = 0; if (y < 0) y = 0;
	if (x >= g_iScreenWidth) x = g_iScreenWidth - 1;
	if (y >= g_iScreenHeight) y = g_iScreenHeight - 1;
}

static void GetMouseLockCenterRenderCoords(int &outRenderX, int &outRenderY)
{
	outRenderX = g_iMouseLockCenterX;
	outRenderY = g_iMouseLockCenterY;
	if (g_hWnd != NULL)
	{
		ConvertClientToRenderCoords(g_hWnd, outRenderX, outRenderY);
	}
}

static void WarpCursorToLockCenter(bool ignoreNextMove)
{
	if (g_hWnd == NULL) return;
	UpdateMouseLockCenter();
	POINT center = { g_iMouseLockCenterX, g_iMouseLockCenterY };
	ClientToScreen(g_hWnd, &center);
	POINT current = {};
	if (GetCursorPos(&current) && current.x == center.x && current.y == center.y)
	{
		return;
	}
	if (SetCursorPos(center.x, center.y) && ignoreNextMove)
	{
		g_bIgnoreNextCenterWarp = true;
	}
}

static void SetCursorVisible(bool visible)
{
	if (visible) { while (ShowCursor(TRUE) < 0) {} }
	else         { while (ShowCursor(FALSE) >= 0) {} }
}

static void SetMouseClip(bool locked)
{
	if (!locked) { ClipCursor(NULL); return; }
	if (g_hWnd == NULL) return;
	RECT clientRect;
	if (!GetClientRect(g_hWnd, &clientRect)) return;
	if (clientRect.right <= clientRect.left || clientRect.bottom <= clientRect.top) return;
	POINT topLeft = { clientRect.left, clientRect.top };
	POINT bottomRight = { clientRect.right, clientRect.bottom };
	ClientToScreen(g_hWnd, &topLeft);
	ClientToScreen(g_hWnd, &bottomRight);
	RECT clipRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
	RECT currentClip = {};
	if (!GetClipCursor(&currentClip) || !EqualRect(&currentClip, &clipRect))
	{
		ClipCursor(&clipRect);
	}
}

static void SetMouseLocked(bool locked)
{
	const bool wasLocked = g_bMouseLocked;

	if (g_hWnd == NULL)
	{
		g_bMouseLocked = locked;
		Mouse::setGrabbed(locked);
		if (!locked && wasLocked)
		{
			Mouse::resetDeltas();
			g_fMouseDeltaRemainderX = 0.0f;
			g_fMouseDeltaRemainderY = 0.0f;
		}
		return;
	}

	if (!locked)
	{
		if (wasLocked)
		{
			SetMouseClip(false);
			if (GetCapture() == g_hWnd)
			{
				ReleaseCapture();
			}
			SetCursorVisible(true);
			g_bMouseLocked = false;
			g_bIgnoreNextCenterWarp = false;
			Mouse::setGrabbed(false);
			Mouse::resetDeltas();
			g_fMouseDeltaRemainderX = 0.0f;
			g_fMouseDeltaRemainderY = 0.0f;

			if (Display::isActive() && IsGameWindowFocused(g_hWnd))
			{
				WarpCursorToLockCenter(false);
				int centerRenderX = 0;
				int centerRenderY = 0;
				GetMouseLockCenterRenderCoords(centerRenderX, centerRenderY);
				Mouse::syncPosition(centerRenderX, centerRenderY);
			}
		}
		return;
	}

	if (!wasLocked)
	{
		UpdateMouseLockCenter();
		g_bMouseLocked = true;
		SetMouseClip(true);
		SetCapture(g_hWnd);
		SetCursorVisible(false);
		WarpCursorToLockCenter(true);
		int centerRenderX = 0;
		int centerRenderY = 0;
		GetMouseLockCenterRenderCoords(centerRenderX, centerRenderY);
		Mouse::syncPosition(centerRenderX, centerRenderY);
		Mouse::resetDeltas();
		g_fMouseDeltaRemainderX = 0.0f;
		g_fMouseDeltaRemainderY = 0.0f;
		Mouse::setGrabbed(true);
	}
	else
	{
		SetMouseClip(true);
	}
}

static void ReleaseAllHeldInput()
{
	for (int key = 0; key < Keyboard::MAX_KEYS; ++key)
	{
		if (Keyboard::isKeyDown(key))
		{
			Keyboard::onKeyUp(key);
		}
	}
	for (int button = 0; button < Mouse::MAX_BUTTONS; ++button)
	{
		if (Mouse::isButtonDown(button))
		{
			Mouse::onMouseButton(button, false, Mouse::getX(), Mouse::getY());
		}
	}
	Mouse::clearPressedButtons();
}

static int TranslateWinKey(WPARAM wParam, LPARAM lParam)
{
	int key = (int)wParam;
	if (key == VK_SHIFT)
	{
		UINT scanCode = (UINT)((lParam >> 16) & 0xFF);
		int translated = (int)MapVirtualKey(scanCode, MAPVK_VSC_TO_VK_EX);
		if (translated != 0) key = translated;
	}
	else if (key == VK_CONTROL)
	{
		key = (lParam & 0x01000000) ? VK_RCONTROL : VK_LCONTROL;
	}
	return key;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		int key = TranslateWinKey(wParam, lParam);
		bool repeat = (lParam & 0x40000000) != 0;
		Keyboard::onKeyDown(key, repeat, 0);
		return 0;
	}
	case WM_KEYUP:
	case WM_SYSKEYUP:
	{
		int key = TranslateWinKey(wParam, lParam);
		Keyboard::onKeyUp(key);
		return 0;
	}
	case WM_CHAR:
		Keyboard::onChar((wchar_t)wParam);
		return 0;
	case WM_ACTIVATE:
	{
		const WORD activeState = LOWORD(wParam);
		const BOOL minimized = (HIWORD(wParam) != 0);
		if (activeState == WA_INACTIVE || minimized)
		{
			Display::setActive(false);
			SetMouseLocked(false);
			SetMouseClip(false);
			if (GetCapture() == hWnd) ReleaseCapture();
			ReleaseAllHeldInput();
		}
		else
		{
			Display::setActive(!IsIconic(hWnd));
		}
		return 0;
	}
	case WM_SETFOCUS:
		Display::setActive(!IsIconic(hWnd));
		return 0;
	case WM_KILLFOCUS:
		Display::setActive(false);
		SetMouseLocked(false);
		SetMouseClip(false);
		if (GetCapture() == hWnd) ReleaseCapture();
		ReleaseAllHeldInput();
		return 0;
	case WM_MOVE:
	case WM_EXITSIZEMOVE:
		UpdateMouseLockCenter();
		if (g_bMouseLocked)
		{
			SetMouseClip(true);
			WarpCursorToLockCenter(true);
		}
		return 0;
	case WM_INPUT:
	{
		UINT dwSize = 0;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
		if (dwSize > 0)
		{
			BYTE stackBuf[128];
			std::vector<BYTE> heapBuf;
			BYTE *pBuf = stackBuf;
			if (dwSize > sizeof(stackBuf))
			{
				heapBuf.resize(dwSize);
				pBuf = heapBuf.data();
			}
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, pBuf, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize)
			{
				const RAWINPUT *raw = reinterpret_cast<const RAWINPUT *>(pBuf);
				if (g_bMouseLocked && Display::isActive() && raw->header.dwType == RIM_TYPEMOUSE)
				{
					if ((raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
					{
						const int dx = (int)raw->data.mouse.lLastX;
						const int dy = (int)raw->data.mouse.lLastY;
						if (dx != 0 || dy != 0)
						{
							Mouse::onMouseDelta(dx, dy);
							g_dwLastRawInputTick = GetTickCount();
							g_bHasRawInput = true;
						}
					}
				}
			}
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	case WM_MOUSEMOVE:
	{
		int x = (short)LOWORD(lParam);
		int y = (short)HIWORD(lParam);
		if (g_bMouseLocked)
		{
			if (x == g_iMouseLockCenterX && y == g_iMouseLockCenterY)
			{
				g_bIgnoreNextCenterWarp = false;
				return 0;
			}
			if (g_bIgnoreNextCenterWarp)
			{
				g_bIgnoreNextCenterWarp = false;
				WarpCursorToLockCenter(false);
				return 0;
			}

			const bool rawInputRecent = g_bHasRawInput && ((GetTickCount() - g_dwLastRawInputTick) < 250);
			if (!rawInputRecent)
			{
				int dx = x - g_iMouseLockCenterX;
				int dy = y - g_iMouseLockCenterY;
				if (dx != 0 || dy != 0)
				{
					RECT clientRect;
					GetClientRect(hWnd, &clientRect);
					const int clientW = clientRect.right - clientRect.left;
					const int clientH = clientRect.bottom - clientRect.top;
					float scaledDX = (float)dx;
					float scaledDY = (float)dy;
					if (clientW > 0 && clientH > 0)
					{
						scaledDX = ((float)dx * (float)g_iScreenWidth) / (float)clientW;
						scaledDY = ((float)dy * (float)g_iScreenHeight) / (float)clientH;
					}
					scaledDX += g_fMouseDeltaRemainderX;
					scaledDY += g_fMouseDeltaRemainderY;
					int outDX = (scaledDX >= 0.0f) ? (int)(scaledDX + 0.5f) : (int)(scaledDX - 0.5f);
					int outDY = (scaledDY >= 0.0f) ? (int)(scaledDY + 0.5f) : (int)(scaledDY - 0.5f);
					g_fMouseDeltaRemainderX = scaledDX - (float)outDX;
					g_fMouseDeltaRemainderY = scaledDY - (float)outDY;
					if (outDX != 0 || outDY != 0) Mouse::onMouseDelta(outDX, outDY);
				}
			}

			WarpCursorToLockCenter(false);
			return 0;
		}
		ConvertClientToRenderCoords(hWnd, x, y);
		Mouse::onMouseMove(x, y);
		g_fMouseDeltaRemainderX = 0.0f;
		g_fMouseDeltaRemainderY = 0.0f;
		return 0;
	}
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	{
		int x = (short)LOWORD(lParam);
		int y = (short)HIWORD(lParam);
		bool isDown = (message == WM_LBUTTONDOWN);
		// FIX: No tocar SetCapture/ReleaseCapture cuando el mouse está bloqueado.
		// SetMouseLocked ya gestiona su propio capture. Interferir con él provoca
		// un WM_MOUSEMOVE sintético al soltar que genera un spike de delta enorme.
		if (!g_bMouseLocked)
		{
			if (isDown) SetCapture(hWnd);
			else if (GetCapture() == hWnd) ReleaseCapture();
		}
		ConvertClientToRenderCoords(hWnd, x, y);
		Mouse::onMouseButton(0, isDown, x, y);
		return 0;
	}
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	{
		int x = (short)LOWORD(lParam);
		int y = (short)HIWORD(lParam);
		bool isDown = (message == WM_RBUTTONDOWN);
		// FIX: igual que LMB — no tocar capture cuando el mouse está bloqueado.
		if (!g_bMouseLocked)
		{
			if (isDown) SetCapture(hWnd);
			else if (GetCapture() == hWnd) ReleaseCapture();
		}
		ConvertClientToRenderCoords(hWnd, x, y);
		Mouse::onMouseButton(1, isDown, x, y);
		return 0;
	}
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	{
		int x = (short)LOWORD(lParam);
		int y = (short)HIWORD(lParam);
		bool isDown = (message == WM_MBUTTONDOWN);
		// FIX: igual.
		if (!g_bMouseLocked)
		{
			if (isDown) SetCapture(hWnd);
			else if (GetCapture() == hWnd) ReleaseCapture();
		}
		ConvertClientToRenderCoords(hWnd, x, y);
		Mouse::onMouseButton(2, isDown, x, y);
		return 0;
	}
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	{
		int x = (short)LOWORD(lParam);
		int y = (short)HIWORD(lParam);
		bool isDown = (message == WM_XBUTTONDOWN);
		int button = (HIWORD(wParam) == XBUTTON1) ? 3 : 4;
		// FIX: igual.
		if (!g_bMouseLocked)
		{
			if (isDown) SetCapture(hWnd);
			else if (GetCapture() == hWnd) ReleaseCapture();
		}
		ConvertClientToRenderCoords(hWnd, x, y);
		Mouse::onMouseButton(button, isDown, x, y);
		return 0;
	}
	case WM_MOUSEWHEEL:
	{
		POINT p;
		p.x = (short)LOWORD(lParam);
		p.y = (short)HIWORD(lParam);
		ScreenToClient(hWnd, &p);
		int delta = (short)HIWORD(wParam);
		int x = p.x; int y = p.y;
		ConvertClientToRenderCoords(hWnd, x, y);
		Mouse::onMouseWheel(delta, x, y);
		return 0;
	}
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		switch (wmId)
		{
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	case WM_SIZE:
	{
		if (wParam == SIZE_MINIMIZED)
		{
			Display::setActive(false);
			SetMouseLocked(false);
			SetMouseClip(false);
			if (GetCapture() == hWnd) ReleaseCapture();
			ReleaseAllHeldInput();
			return 0;
		}
		if (g_bRenderInitialized && !g_bInRuntimeResize)
		{
			int newW = (int)LOWORD(lParam);
			int newH = (int)HIWORD(lParam);
			if (newW > 0 && newH > 0 && (newW != g_iScreenWidth || newH != g_iScreenHeight))
			{
				if (RenderManager.ResizeWindow(newW, newH, g_bWindows64Fullscreen))
				{
					ui.updateRenderTargets(newW, newH);
				}
				SyncEngineViewportDimensions(newW, newH);
			}
		}
		UpdateMouseLockCenter();
		if (g_bMouseLocked)
		{
			SetMouseClip(true);
			WarpCursorToLockCenter(true);
		}
		return 0;
	}
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;
	wcex.cbSize         = sizeof(WNDCLASSEX);
	wcex.style          = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wcex.lpfnWndProc    = WndProc;
	wcex.cbClsExtra     = 0;
	wcex.cbWndExtra     = 0;
	wcex.hInstance      = hInstance;
	wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MINECRAFTWINDOWS));
	wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wcex.lpszMenuName   = NULL;
	wcex.lpszClassName  = "MinecraftClass";
	wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
	return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	g_hInst = hInstance;
	const DWORD windowStyle = g_bWindows64Fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
	int windowX = 0, windowY = 0, windowWidth = g_iScreenWidth, windowHeight = g_iScreenHeight;
	int fittedClientW = g_iScreenWidth, fittedClientH = g_iScreenHeight;
	ComputeFittedWindowPlacement(NULL, g_iScreenWidth, g_iScreenHeight, g_bWindows64Fullscreen,
		windowStyle, windowX, windowY, windowWidth, windowHeight, fittedClientW, fittedClientH);
	g_iScreenWidth = fittedClientW;
	g_iScreenHeight = fittedClientH;

	g_hWnd = CreateWindow("MinecraftClass", "Minecraft: Legacy Edition", windowStyle,
		windowX, windowY,
		windowWidth, windowHeight,
		NULL, NULL, hInstance, NULL);
	if (!g_hWnd) return FALSE;

	RAWINPUTDEVICE rid = {};
	rid.usUsagePage = 0x01;
	rid.usUsage = 0x02;
	rid.dwFlags = 0;
	rid.hwndTarget = g_hWnd;
	g_bRawInputRegistered = (RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE);

	RECT rcClient = {};
	if (GetClientRect(g_hWnd, &rcClient))
	{
		if (rcClient.right - rcClient.left > 0) g_iScreenWidth = rcClient.right - rcClient.left;
		if (rcClient.bottom - rcClient.top > 0) g_iScreenHeight = rcClient.bottom - rcClient.top;
	}
	ShowWindow(g_hWnd, nCmdShow);
	UpdateWindow(g_hWnd);
	return TRUE;
}

void ClearGlobalText()
{
	memset(chGlobalText, 0, 256);
	memset(ui16GlobalText, 0, 512);
}

uint16_t *GetGlobalText()
{
	char *pchBuffer = (char *)ui16GlobalText;
	for (int i = 0; i < 256; i++) pchBuffer[i*2] = chGlobalText[i];
	return ui16GlobalText;
}

void SeedEditBox()
{
	// Windows64 build no longer carries the old platform-specific dialog resource.
	// Leave the current seed text intact instead of trying to open a missing dialog.
}

LRESULT CALLBACK DlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
	case WM_INITDIALOG: return TRUE;
	case WM_COMMAND:
		switch(wParam)
		{
		case IDOK:
			EndDialog(hWndDlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static void ParseWin64CommandLine(LPCTSTR lpCmdLine)
{
	if (!lpCmdLine || !lpCmdLine[0]) return;

	if      (lpCmdLine[0] == '1') { ApplyWindows64ResolutionMode(1); }
	else if (lpCmdLine[0] == '2') { ApplyWindows64ResolutionMode(2); }
	else if (lpCmdLine[0] == '3') { ApplyWindows64ResolutionMode(3); }
	else if (lpCmdLine[0] == '4') { ApplyWindows64ResolutionMode(3); }
	else if (lpCmdLine[0] == '5') { ApplyWindows64ResolutionMode(4); }

	if (_tcsstr(lpCmdLine, _T("--renderer=dx12")) != NULL || _tcsstr(lpCmdLine, _T("--renderer=d3d12")) != NULL) { g_iCliRendererOverride = 3; }
	else if (_tcsstr(lpCmdLine, _T("--renderer=vulkan")) != NULL) { g_iCliRendererOverride = 2; }
	else if (_tcsstr(lpCmdLine, _T("--renderer=opengl")) != NULL || _tcsstr(lpCmdLine, _T("--renderer=gl")) != NULL) { g_iCliRendererOverride = 1; }
	else if (_tcsstr(lpCmdLine, _T("--renderer=dx11")) != NULL || _tcsstr(lpCmdLine, _T("--renderer=d3d11")) != NULL) { g_iCliRendererOverride = 0; }

	if (_tcsstr(lpCmdLine, _T("--vsync=1")) != NULL) { g_iCliVsyncOverride = 1; }
	else if (_tcsstr(lpCmdLine, _T("--vsync=0")) != NULL) { g_iCliVsyncOverride = 0; }

	LPCTSTR smokePtr = _tcsstr(lpCmdLine, _T("--smoke-frames="));
	if (smokePtr != NULL)
	{
		int val = _ttoi(smokePtr + 15);
		if (val > 0) g_iSmokeFrames = val;
	}
}

HRESULT InitDevice()
{
	const int preferredBackend = (g_iCliRendererOverride >= 0) ? g_iCliRendererOverride : (int)UserData_Info::GetRendererBackend();
	const bool vsyncEnabled = (g_iCliVsyncOverride >= 0) ? (g_iCliVsyncOverride != 0) : UserData_Info::GetVsyncEnabled();
	if (!RenderManager.InitialiseWindow(g_hWnd, g_iScreenWidth, g_iScreenHeight, g_bWindows64Fullscreen, preferredBackend, vsyncEnabled))
	{
		return E_FAIL;
	}
	g_bRenderInitialized = true;
	return S_OK;
}

void CleanupDevice()
{
	g_bRenderInitialized = false;
	RenderManager.Shutdown();
}

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
					   _In_opt_ HINSTANCE hPrevInstance,
					   _In_ LPTSTR    lpCmdLine,
					   _In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);

	SetProcessDPIAware();
	UserData_Info::EnsureLoaded();
	ApplyWindows64ResolutionMode((int)UserData_Info::GetResolutionMode());
	ParseWin64CommandLine(lpCmdLine);

	MyRegisterClass(hInstance);
	if (!InitInstance(hInstance, nCmdShow)) return FALSE;
	hMyInst = hInstance;
	if (FAILED(InitDevice())) { CleanupDevice(); return 0; }

	static bool bTrialTimerDisplayed = true;

#ifdef MEMORY_TRACKING
	ResetMem();
	MEMORYSTATUS memStat;
	GlobalMemoryStatus(&memStat);
	printf("RESETMEM start: Avail. phys %d\n", memStat.dwAvailPhys/(1024*1024));
#endif

	app.loadMediaArchive();
	app.loadStringTable();
	ui.init(g_iScreenWidth, g_iScreenHeight);

	InputManager.Initialise(1, 3, MINECRAFT_ACTION_MAX, ACTION_MAX_MENU);
	DefineActions();
	InputManager.SetJoypadMapVal(0, 0);
	InputManager.SetKeyRepeatRate(0.3f, 0.2f);

	ProfileManager.Initialise(TITLEID_MINECRAFT,
		app.m_dwOfferID, PROFILE_VERSION_10,
		NUM_PROFILE_VALUES, NUM_PROFILE_SETTINGS, dwProfileSettingsA,
		app.GAME_DEFINED_PROFILE_DATA_BYTES * XUSER_MAX_COUNT,
		&app.uiGameDefinedDataChangedBitmask);

	ProfileManager.SetDefaultOptionsCallback(&CConsoleMinecraftApp::DefaultOptionsCallback, (LPVOID)&app);
	g_NetworkManager.Initialise();
	ProfileManager.SetDebugFullOverride(true);

	Tesselator::CreateNewThreadStorage(1024*1024);
	AABB::CreateNewThreadStorage();
	Vec3::CreateNewThreadStorage();
	IntCache::CreateNewThreadStorage();
	Compression::CreateNewThreadStorage();
	OldChunkStorage::CreateNewThreadStorage();
	Level::enableLightingCache();
	Tile::CreateNewThreadStorage();

	Minecraft::main();
	SyncEngineViewportDimensions(g_iScreenWidth, g_iScreenHeight);
	Minecraft *pMinecraft = Minecraft::GetInstance();

	app.InitGameSettings();
	UserData_Info::EnsureLoaded();
	ShaderManager::Initialize();
	if(pMinecraft != NULL && pMinecraft->options != NULL)
	{
		const int renderDistance = (int)UserData_Info::GetRenderDistance();
		if(pMinecraft->options->viewDistance != renderDistance)
		{
			pMinecraft->options->viewDistance = renderDistance;
			pMinecraft->options->save();
		}
	}
	if(pMinecraft != NULL && pMinecraft->gameRenderer != NULL)
	{
		pMinecraft->gameRenderer->SetFovVal((float)UserData_Info::GetFov());
	}
	app.SetPlayerSkin(ProfileManager.GetPrimaryPad(), (DWORD)UserData_Info::GetSkinId());
	app.InitialiseTips();

	pMinecraft->options->set(Options::Option::MUSIC, 1.0f);
	pMinecraft->options->set(Options::Option::SOUND, 1.0f);

	MSG msg = {0};
	timeBeginPeriod(1);
	LARGE_INTEGER menuFrameFreq = {};
	LARGE_INTEGER menuFrameNext = {};
	QueryPerformanceFrequency(&menuFrameFreq);
	QueryPerformanceCounter(&menuFrameNext);
	const LONGLONG menuTargetInterval = (menuFrameFreq.QuadPart > 0) ? (menuFrameFreq.QuadPart / 60) : 0;

	int renderedSmokeFrames = 0;
	LARGE_INTEGER smokeFreq = {}, smokeStart = {};
	if (g_iSmokeFrames > 0)
	{
		QueryPerformanceFrequency(&smokeFreq);
		QueryPerformanceCounter(&smokeStart);
	}
	while (WM_QUIT != msg.message)
	{
		int msgBudget = 256;
		while (msgBudget-- > 0 && PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (WM_QUIT == msg.message) break;
		}
		if (WM_QUIT == msg.message) break;
		Display::setActive(IsGameWindowFocused(g_hWnd));
		RenderManager.StartFrame();
		if (!app.GetGameStarted())
		{
			static const float kMenuClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			RenderManager.SetClearColour(kMenuClear);
			RenderManager.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		}

		app.UpdateTime();
		PIXBeginNamedEvent(0,"Input manager tick");   InputManager.Tick();   PIXEndNamedEvent();
		PIXBeginNamedEvent(0,"Profile manager tick"); /*ProfileManager.Tick();*/ PIXEndNamedEvent();
		PIXBeginNamedEvent(0,"Storage manager tick"); StorageManager.Tick(); PIXEndNamedEvent();
		PIXBeginNamedEvent(0,"Render manager tick");  RenderManager.Tick();  PIXEndNamedEvent();
		PIXBeginNamedEvent(0,"Social network manager tick"); /*CSocialManager::Instance()->Tick();*/ PIXEndNamedEvent();
		PIXBeginNamedEvent(0,"Sentient tick"); MemSect(37); /*SentientManager.Tick();*/ MemSect(0); PIXEndNamedEvent();
		PIXBeginNamedEvent(0,"Network manager do work #1"); g_NetworkManager.DoWork(); PIXEndNamedEvent();

		if (app.GetGameStarted())
		{
			pMinecraft->run_middle();
			app.SetAppPaused(g_NetworkManager.IsLocalGame() && g_NetworkManager.GetPlayerCount() == 1 && ui.IsPauseMenuDisplayed(ProfileManager.GetPrimaryPad()));
		}
		else
		{
			MemSect(28);
			pMinecraft->soundEngine->tick(NULL, 0.0f);
			MemSect(0);
			pMinecraft->textures->tick(true, false);
			IntCache::Reset();
			if (app.GetReallyChangingSessionType()) pMinecraft->tickAllConnections();
		}

		pMinecraft->soundEngine->playMusicTick();

#ifdef MEMORY_TRACKING
		static bool bResetMemTrack = false;
		static bool bDumpMemTrack  = false;
		MemPixStuff();
		if (bResetMemTrack) { ResetMem(); MEMORYSTATUS ms; GlobalMemoryStatus(&ms); printf("RESETMEM: Avail. phys %d\n",ms.dwAvailPhys/(1024*1024)); bResetMemTrack=false; }
		if (bDumpMemTrack)  { DumpMem();  bDumpMemTrack=false;  MEMORYSTATUS ms; GlobalMemoryStatus(&ms); printf("DUMPMEM: Avail. phys %d\n",ms.dwAvailPhys/(1024*1024)); printf("Renderer used: %d\n",RenderManager.CBuffSize(-1)); }
#endif

		ui.tick();
		ui.render();
		RenderManager.Present();
		ui.CheckMenuDisplayed();

		const bool windowFocused = IsGameWindowFocused(g_hWnd);
		Display::setActive(windowFocused);
		bool shouldLockMouse = false;
		if (windowFocused && app.GetGameStarted() && (pMinecraft != NULL))
			shouldLockMouse = (pMinecraft->getScreen() == NULL) && !ui.GetMenuDisplayed(ProfileManager.GetPrimaryPad());
		SetMouseLocked(shouldLockMouse);

		app.HandleXuiActions();
		Keyboard::tick();
		Mouse::tick();

		if (!ProfileManager.IsFullVersion())
		{
			if (app.GetGameStarted())
			{
				if (app.IsAppPaused()) app.UpdateTrialPausedTimer();
				ui.UpdateTrialTimer(ProfileManager.GetPrimaryPad());
			}
		}
		else
		{
			if (bTrialTimerDisplayed) { ui.ShowTrialTimer(false); bTrialTimerDisplayed = false; }
		}

		Vec3::resetPool();

		if (g_iSmokeFrames > 0)
		{
			++renderedSmokeFrames;
			if (renderedSmokeFrames >= g_iSmokeFrames)
			{
				LARGE_INTEGER smokeEnd = {};
				QueryPerformanceCounter(&smokeEnd);
				double elapsedSec = (smokeFreq.QuadPart > 0) ? (double)(smokeEnd.QuadPart - smokeStart.QuadPart) / (double)smokeFreq.QuadPart : 1.0;
				double avgFps = (elapsedSec > 0.0) ? ((double)renderedSmokeFrames / elapsedSec) : 0.0;
				printf("[SmokeTest] backend=%s adapter=%s frames=%d elapsed=%.3fs fps=%.1f res=%dx%d\n",
					RenderManager.GetBackendName(), RenderManager.GetAdapterName(),
					renderedSmokeFrames, elapsedSec, avgFps, g_iScreenWidth, g_iScreenHeight);
				fflush(stdout);
				PostQuitMessage(0);
			}
		}
		else if (!app.GetGameStarted() && menuTargetInterval > 0)
		{
			/* Cap frontend menus cleanly to 60 FPS so skin/splash animations run at natural speed */
			menuFrameNext.QuadPart += menuTargetInterval;
			LARGE_INTEGER now = {};
			QueryPerformanceCounter(&now);
			if (now.QuadPart - menuFrameNext.QuadPart > menuTargetInterval * 3)
			{
				menuFrameNext.QuadPart = now.QuadPart;
			}
			while (now.QuadPart < menuFrameNext.QuadPart)
			{
				LONGLONG remTicks = menuFrameNext.QuadPart - now.QuadPart;
				double remMs = ((double)remTicks * 1000.0) / (double)menuFrameFreq.QuadPart;
				if (remMs > 2.0)
				{
					Sleep((DWORD)(remMs - 1.5));
				}
				else
				{
					Sleep(0);
				}
				QueryPerformanceCounter(&now);
			}
		}
		else if (app.GetGameStarted())
		{
			QueryPerformanceCounter(&menuFrameNext);
		}
	}

	timeEndPeriod(1);
	SetMouseLocked(false);
	ui.shutdown();
	CleanupDevice();
	return 0;
}

#ifdef MEMORY_TRACKING

int totalAllocGen = 0;
unordered_map<int,int> allocCounts;
bool trackEnable = false;
bool trackStarted = false;
volatile size_t sizeCheckMin = 1160;
volatile size_t sizeCheckMax = 1160;
volatile int sectCheck = 48;
CRITICAL_SECTION memCS;
DWORD tlsIdx;

LPVOID XMemAlloc(SIZE_T dwSize, DWORD dwAllocAttributes)
{
	if (!trackStarted)
	{
		void *p = XMemAllocDefault(dwSize, dwAllocAttributes);
		totalAllocGen += XMemSizeDefault(p, dwAllocAttributes);
		return p;
	}
	EnterCriticalSection(&memCS);
	void *p = XMemAllocDefault(dwSize + 16, dwAllocAttributes);
	size_t realSize = XMemSizeDefault(p, dwAllocAttributes) - 16;
	if (trackEnable)
	{
		int sect = ((int)TlsGetValue(tlsIdx)) & 0x3f;
		*(((unsigned char *)p)+realSize) = sect;
		if ((realSize >= sizeCheckMin) && (realSize <= sizeCheckMax) && ((sect == sectCheck) || (sectCheck == -1)))
			app.DebugPrintf("Found one\n");
		if (p) { totalAllocGen += realSize; trackEnable = false; int key = (sect<<26)|realSize; allocCounts[key]++; trackEnable = true; }
	}
	LeaveCriticalSection(&memCS);
	return p;
}

void* operator new (size_t size) { return (unsigned char *)XMemAlloc(size,MAKE_XALLOC_ATTRIBUTES(0,FALSE,TRUE,FALSE,0,XALLOC_PHYSICAL_ALIGNMENT_DEFAULT,XALLOC_MEMPROTECT_READWRITE,FALSE,XALLOC_MEMTYPE_HEAP)); }
void  operator delete (void *p)  { XMemFree(p,MAKE_XALLOC_ATTRIBUTES(0,FALSE,TRUE,FALSE,0,XALLOC_PHYSICAL_ALIGNMENT_DEFAULT,XALLOC_MEMPROTECT_READWRITE,FALSE,XALLOC_MEMTYPE_HEAP)); }

void WINAPI XMemFree(PVOID pAddress, DWORD dwAllocAttributes)
{
	if (dwAllocAttributes == 0) dwAllocAttributes = MAKE_XALLOC_ATTRIBUTES(0,FALSE,TRUE,FALSE,0,XALLOC_PHYSICAL_ALIGNMENT_DEFAULT,XALLOC_MEMPROTECT_READWRITE,FALSE,XALLOC_MEMTYPE_HEAP);
	if (!trackStarted) { totalAllocGen -= XMemSizeDefault(pAddress,dwAllocAttributes); XMemFreeDefault(pAddress,dwAllocAttributes); return; }
	EnterCriticalSection(&memCS);
	if (pAddress)
	{
		size_t realSize = XMemSizeDefault(pAddress,dwAllocAttributes) - 16;
		if (trackEnable) { int sect=*(((unsigned char*)pAddress)+realSize); totalAllocGen-=realSize; trackEnable=false; int key=(sect<<26)|realSize; allocCounts[key]--; trackEnable=true; }
		XMemFreeDefault(pAddress, dwAllocAttributes);
	}
	LeaveCriticalSection(&memCS);
}

SIZE_T WINAPI XMemSize(PVOID pAddress, DWORD dwAllocAttributes)
{
	return trackStarted ? XMemSizeDefault(pAddress,dwAllocAttributes)-16 : XMemSizeDefault(pAddress,dwAllocAttributes);
}

void DumpMem()
{
	int totalLeak = 0;
	for (AUTO_VAR(it,allocCounts.begin()); it!=allocCounts.end(); it++)
		if (it->second > 0) { app.DebugPrintf("%d %d %d %d\n",(it->first>>26)&0x3f,it->first&0x03ffffff,it->second,(it->first&0x03ffffff)*it->second); totalLeak+=(it->first&0x03ffffff)*it->second; }
	app.DebugPrintf("Total %d\n",totalLeak);
}

void ResetMem()
{
	if (!trackStarted) { trackEnable=true; trackStarted=true; totalAllocGen=0; InitializeCriticalSection(&memCS); tlsIdx=TlsAlloc(); }
	EnterCriticalSection(&memCS); trackEnable=false; allocCounts.clear(); trackEnable=true; LeaveCriticalSection(&memCS);
}

void MemSect(int section)
{
	unsigned int value = (unsigned int)TlsGetValue(tlsIdx);
	value = (section == 0) ? (value>>6)&0x03ffffff : (value<<6)|section;
	TlsSetValue(tlsIdx, (LPVOID)value);
}

void MemPixStuff()
{
	const int MAX_SECT = 46;
	int totals[MAX_SECT] = {0};
	for (AUTO_VAR(it,allocCounts.begin()); it!=allocCounts.end(); it++)
		if (it->second>0) { int sect=(it->first>>26)&0x3f; totals[sect]+=(it->first&0x03ffffff)*it->second; }
	unsigned int allSectsTotal = 0;
	for (int i=0; i<MAX_SECT; i++) { allSectsTotal+=totals[i]; PIXAddNamedCounter(((float)totals[i])/1024.0f,"MemSect%d",i); }
	PIXAddNamedCounter(((float)allSectsTotal)/4096.0f,"MemSect total pages");
}

#endif
