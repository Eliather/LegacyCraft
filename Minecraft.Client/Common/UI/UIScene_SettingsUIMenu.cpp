#include "stdafx.h"
#include "UI.h"
#include "UIScene_SettingsUIMenu.h"
#include "..\..\Minecraft.h"
#include "..\..\GameRenderer.h"
#include "..\..\LevelRenderer.h"
#include "..\..\MinecraftServer.h"
#include "..\..\PlayerList.h"
#include "..\..\Options.h"
#include "..\..\UserData_Info.h"
#include "..\..\ShaderManager.h"

#if defined(_WINDOWS64)
#include "..\..\CustomGenericBackground.h"
#include "..\..\CustomSlider.h"

extern bool Windows64ApplyResolutionModeRuntime(int resolutionMode);

#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif

namespace
{
	bool g_bEnableSettingsUISFW = false;

	enum ESettingsUICustomSlider
	{
		eSettingsUICustomSlider_FOV = 0,
		eSettingsUICustomSlider_Gamma,
		eSettingsUICustomSlider_RenderDistance,
		eSettingsUICustomSlider_AnimatedCharacter,
		eSettingsUICustomSlider_UISize,
		eSettingsUICustomSlider_UISizeSplitscreen,
		eSettingsUICustomSlider_Renderer,
		eSettingsUICustomSlider_Vsync,
		eSettingsUICustomSlider_Shaders,
		eSettingsUICustomSlider_Count
	};

	CustomGenericBackground g_settingsUIPanel;
	CustomSlider g_settingsUISliders[eSettingsUICustomSlider_Count];
	bool g_settingsUIControlsInitialised = false;
	int g_settingsUIWidth = -1;
	int g_settingsUIHeight = -1;
	bool g_settingsUIResolutionSliderWaitForRelease = false;
	int g_settingsUIResolutionSliderCooldownTicks = 0;
	int g_settingsUIResolutionSliderLockedValue = -1;

	const float kSettingsUIPanelWidth = 920.0f;
	const float kSettingsUIPanelHeight = 560.0f;
	const float kSettingsUITitleScale = 3.0f;
	const float kSettingsUISliderXInset = 140.0f;
	const float kSettingsUISliderStartYOffset = 84.0f;
	const float kSettingsUISliderWidth = 640.0f;
	const float kSettingsUISliderHeight = 35.0f;
	const float kSettingsUISliderGap = 11.0f;
	const int kSettingsUIRenderDistanceMin = 2;
	const int kSettingsUIRenderDistanceDefault = 18;
	const int kSettingsUIRenderDistanceMax = 32;

	int ClampSettingsUIFov(int fov)
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

	int ClampSettingsUIRenderDistance(int renderDistance)
	{
		if(renderDistance < kSettingsUIRenderDistanceMin)
		{
			return kSettingsUIRenderDistanceMin;
		}
		if(renderDistance > kSettingsUIRenderDistanceMax)
		{
			return kSettingsUIRenderDistanceMax;
		}
		return renderDistance;
	}

	int GetSettingsUIGameSetting(ESettingsUICustomSlider slider)
	{
		switch(slider)
		{
		case eSettingsUICustomSlider_Gamma:              return eGameSetting_Gamma;
		case eSettingsUICustomSlider_UISize:             return eGameSetting_UISize;
		case eSettingsUICustomSlider_UISizeSplitscreen:  return eGameSetting_UISizeSplitscreen;
		default:                                         return -1;
		}
	}

	int GetSettingsUIStringId(ESettingsUICustomSlider slider)
	{
		switch(slider)
		{
		case eSettingsUICustomSlider_Gamma:              return IDS_BRIGHTNESS;
		case eSettingsUICustomSlider_RenderDistance:     return IDS_RENDER_DISTANCE;
		case eSettingsUICustomSlider_UISize:             return IDS_SLIDER_UISIZE;
		case eSettingsUICustomSlider_UISizeSplitscreen:  return IDS_SLIDER_UISIZESPLITSCREEN;
		default:                                         return IDS_OPTIONS;
		}
	}

	int GetSettingsUISliderLimit(ESettingsUICustomSlider slider)
	{
		switch(slider)
		{
		case eSettingsUICustomSlider_FOV:
			return 90;
		case eSettingsUICustomSlider_Gamma:
			return 100;
		case eSettingsUICustomSlider_RenderDistance:
			return kSettingsUIRenderDistanceMax - kSettingsUIRenderDistanceMin;
		case eSettingsUICustomSlider_AnimatedCharacter:
			return 3;
		case eSettingsUICustomSlider_UISize:
		case eSettingsUICustomSlider_UISizeSplitscreen:
			return 2;
		case eSettingsUICustomSlider_Renderer:
			return 3;
		case eSettingsUICustomSlider_Vsync:
			return 1;
		case eSettingsUICustomSlider_Shaders:
		{
			const std::vector<ShaderManager::PackEntry> &packs = ShaderManager::GetAvailablePacks();
			return (packs.size() > 0) ? (int)packs.size() : 1;
		}
		default:
			return 1;
		}
	}

	bool IsSettingsUIBooleanSlider(ESettingsUICustomSlider slider)
	{
		switch(slider)
		{
		case eSettingsUICustomSlider_Vsync:
			return true;
		default:
			return false;
		}
	}

	const wchar_t *GetSettingsUIResolutionModeLabel(int mode)
	{
		switch(mode)
		{
		case 1: return L"1280x720";
		case 2: return L"1366x768";
		case 3: return L"1920x1080";
		case 4: return L"Pantalla completa";
		default: return L"1920x1080";
		}
	}

	const wchar_t *GetSettingsUIRendererBackendLabel(int backend)
	{
		switch(backend)
		{
		case 0: return L"DirectX 11";
		case 1: return L"OpenGL";
		case 2: return L"Vulkan";
		case 3: return L"DirectX 12";
		default: return L"DirectX 11";
		}
	}

	int GetSettingsUISliderValue(int iPad, ESettingsUICustomSlider slider)
	{
		if(slider == eSettingsUICustomSlider_FOV)
		{
			return ClampSettingsUIFov((int)UserData_Info::GetFov()) - 30;
		}
		if(slider == eSettingsUICustomSlider_RenderDistance)
		{
			Minecraft *minecraft = Minecraft::GetInstance();
			if(minecraft != NULL && minecraft->options != NULL)
			{
				return ClampSettingsUIRenderDistance(minecraft->options->viewDistance) - kSettingsUIRenderDistanceMin;
			}
			return ClampSettingsUIRenderDistance((int)UserData_Info::GetRenderDistance()) - kSettingsUIRenderDistanceMin;
		}
		if(slider == eSettingsUICustomSlider_AnimatedCharacter)
		{
			if(g_settingsUIResolutionSliderLockedValue >= 0)
			{
				return g_settingsUIResolutionSliderLockedValue;
			}

			const int resolutionMode = (int)UserData_Info::GetResolutionMode();
			if(resolutionMode < 1 || resolutionMode > 4)
			{
				return 2;
			}
			return resolutionMode - 1;
		}
		if(slider == eSettingsUICustomSlider_Renderer)
		{
			int b = (int)UserData_Info::GetRendererBackend();
			return (b >= 0 && b <= 3) ? b : 0;
		}
		if(slider == eSettingsUICustomSlider_Vsync)
		{
			return RenderManager.IsVsyncEnabled() ? 1 : 0;
		}
		if(slider == eSettingsUICustomSlider_Shaders)
		{
			if(!ShaderManager::IsEnabled())
			{
				return 0;
			}
			const std::vector<ShaderManager::PackEntry> &packs = ShaderManager::GetAvailablePacks();
			const std::wstring active = ShaderManager::GetActivePackId();
			for(size_t i = 0; i < packs.size(); ++i)
			{
				if(packs[i].id == active)
				{
					return (int)(i + 1);
				}
			}
			return 1;
		}

		const int settingId = GetSettingsUIGameSetting(slider);
		if(settingId < 0)
		{
			return 0;
		}

		const eGameSetting setting = (eGameSetting)settingId;
		return app.GetGameSettings(iPad, setting);
	}

	void InvalidateSettingsUILayout()
	{
		g_settingsUIControlsInitialised = false;
		g_settingsUIWidth = -1;
		g_settingsUIHeight = -1;
	}

	float GetSettingsUIPanelX(int width)
	{
		return ((float)width - kSettingsUIPanelWidth) * 0.5f;
	}

	float GetSettingsUIPanelY(int height)
	{
		float y = ((float)height - kSettingsUIPanelHeight) * 0.5f - 10.0f;
		if(y < 70.0f)
		{
			y = 70.0f;
		}
		return y;
	}

	void SetupSettingsUICustomControls(Minecraft *minecraft)
	{
		if(minecraft == NULL)
		{
			return;
		}

		const int width = minecraft->width_phys;
		const int height = minecraft->height_phys;
		if(g_settingsUIControlsInitialised && g_settingsUIWidth == width && g_settingsUIHeight == height)
		{
			return;
		}

		const float panelX = GetSettingsUIPanelX(width);
		const float panelY = GetSettingsUIPanelY(height);

		g_settingsUIPanel.Setup(panelX, panelY, kSettingsUIPanelWidth, kSettingsUIPanelHeight,
			CustomGenericBackground::eTextureSet_Standard);

		float sliderX = panelX + kSettingsUISliderXInset;
		float sliderY = panelY + kSettingsUISliderStartYOffset;
		for(int i = 0; i < eSettingsUICustomSlider_Count; ++i)
		{
			g_settingsUISliders[i].Setup(sliderX, sliderY, kSettingsUISliderWidth, kSettingsUISliderHeight,
				GetSettingsUISliderLimit((ESettingsUICustomSlider)i));
			sliderY += kSettingsUISliderHeight + kSettingsUISliderGap;
		}

		g_settingsUIControlsInitialised = true;
		g_settingsUIWidth = width;
		g_settingsUIHeight = height;
	}

	void SyncSettingsUICustomControls(int iPad)
	{
		for(int i = 0; i < eSettingsUICustomSlider_Count; ++i)
		{
			if(g_settingsUISliders[i].IsDragging())
			{
				continue;
			}

			g_settingsUISliders[i].SetValue(GetSettingsUISliderValue(iPad, (ESettingsUICustomSlider)i));
		}
	}

	void ApplySettingsUISliderValue(int iPad, ESettingsUICustomSlider slider)
	{
		const int value = g_settingsUISliders[slider].GetValue();

		if(slider == eSettingsUICustomSlider_FOV)
		{
			const int fov = value + 30;
			if((int)UserData_Info::GetFov() == fov)
			{
				return;
			}

			UserData_Info::SetFov((unsigned char)fov);

			Minecraft *minecraft = Minecraft::GetInstance();
			if(minecraft != NULL && minecraft->gameRenderer != NULL)
			{
				minecraft->gameRenderer->SetFovVal((float)fov);
			}
			return;
		}
		if(slider == eSettingsUICustomSlider_RenderDistance)
		{
			Minecraft *minecraft = Minecraft::GetInstance();
			if(minecraft == NULL || minecraft->options == NULL)
			{
				return;
			}

			const int renderDistance = value + kSettingsUIRenderDistanceMin;
			if(minecraft->options->viewDistance == renderDistance)
			{
				return;
			}

			UserData_Info::SetRenderDistance((unsigned char)renderDistance);
			minecraft->options->viewDistance = renderDistance;
			minecraft->options->save();
			MinecraftServer *server = MinecraftServer::getInstance();
			if(server != NULL && server->getPlayers() != NULL)
			{
				server->getPlayers()->setViewDistance(renderDistance);
			}
			if(minecraft->levelRenderer != NULL && minecraft->level != NULL)
			{
				minecraft->levelRenderer->allChanged();
			}
			return;
		}
		if(slider == eSettingsUICustomSlider_AnimatedCharacter)
		{
			const int resolutionMode = value + 1;
			const int previousResolutionMode = (int)UserData_Info::GetResolutionMode();
			if(previousResolutionMode == resolutionMode)
			{
				return;
			}

			g_settingsUIResolutionSliderLockedValue = resolutionMode - 1;
			g_settingsUIResolutionSliderWaitForRelease = true;
			g_settingsUIResolutionSliderCooldownTicks = 20;
			g_settingsUISliders[slider].SetValue(g_settingsUIResolutionSliderLockedValue);
			UserData_Info::SetResolutionMode((unsigned char)resolutionMode);
			Windows64ApplyResolutionModeRuntime(resolutionMode);
			InvalidateSettingsUILayout();
			return;
		}
		if(slider == eSettingsUICustomSlider_Renderer)
		{
			const unsigned char backend = (unsigned char)((value >= 0 && value <= 3) ? value : 0);
			if(UserData_Info::GetRendererBackend() != backend)
			{
				UserData_Info::SetRendererBackend(backend);
			}
			return;
		}
		if(slider == eSettingsUICustomSlider_Vsync)
		{
			const bool enabled = (value != 0);
			if(RenderManager.IsVsyncEnabled() != enabled)
			{
				RenderManager.SetVsyncEnabled(enabled);
			}
			if(UserData_Info::GetVsyncEnabled() != enabled)
			{
				UserData_Info::SetVsyncEnabled(enabled);
			}
			return;
		}
		if(slider == eSettingsUICustomSlider_Shaders)
		{
			if(value <= 0)
			{
				if(ShaderManager::IsEnabled())
				{
					ShaderManager::SetEnabled(false);
				}
				return;
			}
			const std::vector<ShaderManager::PackEntry> &packs = ShaderManager::GetAvailablePacks();
			const size_t idx = (size_t)(value - 1);
			const std::wstring targetPack = (idx < packs.size()) ? packs[idx].id : L"Default";
			if(!ShaderManager::IsEnabled())
			{
				ShaderManager::SetEnabled(true);
			}
			if(ShaderManager::GetActivePackId() != targetPack)
			{
				ShaderManager::SelectPack(targetPack);
			}
			return;
		}

		const int settingId = GetSettingsUIGameSetting(slider);
		if(settingId < 0)
		{
			return;
		}

		const eGameSetting setting = (eGameSetting)settingId;
		if(app.GetGameSettings(iPad, setting) == value)
		{
			return;
		}

		app.SetGameSettings(iPad, setting, value);
		if(slider == eSettingsUICustomSlider_UISize || slider == eSettingsUICustomSlider_UISizeSplitscreen)
		{
			ui.UpdateSelectedItemPos(iPad);
		}
	}

	std::wstring BuildSettingsUISliderLabel(int iPad, ESettingsUICustomSlider slider)
	{
		(void)iPad;

		wchar_t buffer[256] = {};
		const int value = g_settingsUISliders[slider].GetValue();

		if(slider == eSettingsUICustomSlider_FOV)
		{
			_snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"FOV: %d", value + 30);
			return std::wstring(buffer);
		}

		const wchar_t *baseLabel = app.GetString(GetSettingsUIStringId(slider));
		if(baseLabel == NULL)
		{
			baseLabel = L"";
		}

		if(slider == eSettingsUICustomSlider_RenderDistance)
		{
			_snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%ls: %d", baseLabel, value + kSettingsUIRenderDistanceMin);
			return std::wstring(buffer);
		}
		if(slider == eSettingsUICustomSlider_AnimatedCharacter)
		{
			const int resolutionMode = value + 1;
			_snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"Resolucion: %d - %ls", resolutionMode, GetSettingsUIResolutionModeLabel(resolutionMode));
			return std::wstring(buffer);
		}
		if(slider == eSettingsUICustomSlider_Renderer)
		{
			const bool pendingRestart = (value != RenderManager.GetBackendType());
			_snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"API Grafica: %ls%ls",
				GetSettingsUIRendererBackendLabel(value),
				pendingRestart ? L" (reiniciar)" : L"");
			return std::wstring(buffer);
		}
		if(slider == eSettingsUICustomSlider_Vsync)
		{
			_snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"VSync: %ls", (value != 0) ? L"Activado" : L"Desactivado");
			return std::wstring(buffer);
		}
		if(slider == eSettingsUICustomSlider_Shaders)
		{
			if(value <= 0)
			{
				return L"Shaders: Desactivado";
			}
			const std::vector<ShaderManager::PackEntry> &packs = ShaderManager::GetAvailablePacks();
			const size_t idx = (size_t)(value - 1);
			const std::wstring packLabel = (idx < packs.size()) ? packs[idx].displayName : L"Default (LegacyCraft)";
			_snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"Shaders: %ls", packLabel.c_str());
			return std::wstring(buffer);
		}

		if(IsSettingsUIBooleanSlider(slider))
		{
			const wchar_t *stateLabel = app.GetString(value != 0 ? IDS_ON : IDS_OFF);
			if(stateLabel == NULL)
			{
				stateLabel = (value != 0) ? L"On" : L"Off";
			}
			_snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%ls: %ls", baseLabel, stateLabel);
		}
		else
		{
			if(slider == eSettingsUICustomSlider_Gamma)
			{
				_snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%ls: %d%%", baseLabel, value);
			}
			else
			{
				_snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%ls: %d", baseLabel, value + 1);
			}
		}

		return std::wstring(buffer);
	}

	void DrawSettingsUITitle(Minecraft *minecraft, Font *font, int viewport)
	{
		if(minecraft == NULL || font == NULL)
		{
			return;
		}

		const wchar_t *titleText = app.GetString(IDS_INTERFACE);
		std::wstring title = (titleText != NULL) ? titleText : L"Interface";

		ui.setupRenderPosition((C4JRender::eViewportType)viewport);

		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_LIGHTING);
		glDisable(GL_FOG);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.0f);
		glEnable(GL_TEXTURE_2D);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0.0, (double)minecraft->width_phys, (double)minecraft->height_phys, 0.0, -1.0, 1.0);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();

		const float textWidth = (float)font->width(title) * kSettingsUITitleScale;
		const float textX = g_settingsUIPanel.GetX() + (g_settingsUIPanel.GetWidth() - textWidth) * 0.5f;
		const float textY = g_settingsUIPanel.GetY() + 42.0f;

		glPushMatrix();
		glTranslatef(textX, textY, 0.0f);
		glScalef(kSettingsUITitleScale, kSettingsUITitleScale, 1.0f);
		font->draw(title, 1.0f, 1.0f, 0xFF000000);
		font->draw(title, 0.0f, 0.0f, 0xFFFFFF00);
		glPopMatrix();

		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	}

	void DrawSettingsUICustomControls(Minecraft *minecraft, Font *font, int viewport, int iPad)
	{
		g_settingsUIPanel.Render(minecraft, viewport);
		DrawSettingsUITitle(minecraft, font, viewport);

		for(int i = 0; i < eSettingsUICustomSlider_Count; ++i)
		{
			g_settingsUISliders[i].RenderLabel(minecraft, font,
				BuildSettingsUISliderLabel(iPad, (ESettingsUICustomSlider)i), viewport);
		}
	}
}
#endif

UIScene_SettingsUIMenu::UIScene_SettingsUIMenu(int iPad, void *initData, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	// Setup all the Iggy references we need for this scene
	initialiseMovie();

	m_bNotInGame=(Minecraft::GetInstance()->level==NULL);

	m_checkboxDisplayHUD.init(app.GetString(IDS_CHECKBOX_DISPLAY_HUD),eControl_DisplayHUD,(app.GetGameSettings(m_iPad,eGameSetting_DisplayHUD)!=0));
	m_checkboxDisplayHand.init(app.GetString(IDS_CHECKBOX_DISPLAY_HAND),eControl_DisplayHand,(app.GetGameSettings(m_iPad,eGameSetting_DisplayHand)!=0));
	m_checkboxDisplayDeathMessages.init(app.GetString(IDS_CHECKBOX_DEATH_MESSAGES),eControl_DisplayDeathMessages,(app.GetGameSettings(m_iPad,eGameSetting_DeathMessages)!=0));
	m_checkboxDisplayAnimatedCharacter.init(app.GetString(IDS_CHECKBOX_ANIMATED_CHARACTER),eControl_DisplayAnimatedCharacter,(app.GetGameSettings(m_iPad,eGameSetting_AnimatedCharacter)!=0));
	m_checkboxSplitscreen.init(app.GetString(IDS_CHECKBOX_VERTICAL_SPLIT_SCREEN),eControl_Splitscreen,(app.GetGameSettings(m_iPad,eGameSetting_SplitScreenVertical)!=0));
	m_checkboxShowSplitscreenGamertags.init(app.GetString(IDS_CHECKBOX_DISPLAY_SPLITSCREENGAMERTAGS),eControl_ShowSplitscreenGamertags,(app.GetGameSettings(m_iPad,eGameSetting_DisplaySplitscreenGamertags)!=0));

	WCHAR TempString[256];

	swprintf( (WCHAR *)TempString, 256, L"%ls: %d", app.GetString( IDS_SLIDER_UISIZE ),app.GetGameSettings(m_iPad,eGameSetting_UISize)+1);	
	m_sliderUISize.init(TempString,eControl_UISize,1,3,app.GetGameSettings(m_iPad,eGameSetting_UISize)+1);

	swprintf( (WCHAR *)TempString, 256, L"%ls: %d", app.GetString( IDS_SLIDER_UISIZESPLITSCREEN ),app.GetGameSettings(m_iPad,eGameSetting_UISizeSplitscreen)+1);	
	m_sliderUISizeSplitscreen.init(TempString,eControl_UISizeSplitscreen,1,3,app.GetGameSettings(m_iPad,eGameSetting_UISizeSplitscreen)+1);

	doHorizontalResizeCheck();

	bool bInGame=(Minecraft::GetInstance()->level!=NULL);
	bool bPrimaryPlayer = ProfileManager.GetPrimaryPad()==m_iPad;

	// if we're not in the game, we need to use basescene 0 
	if(bInGame)
	{
		// If the game has started, then you need to be the host to change the in-game gamertags
		if(!bPrimaryPlayer)
		{	
			// hide things we don't want the splitscreen player changing
			removeControl(&m_checkboxSplitscreen, true);
			removeControl(&m_checkboxShowSplitscreenGamertags, true);
		}
	}


	if(app.GetLocalPlayerCount()>1)
	{
#if TO_BE_IMPLEMENTED
		app.AdjustSplitscreenScene(m_hObj,&m_OriginalPosition,m_iPad);
#endif
	}

#if defined(_WINDOWS64)
	if(!g_bEnableSettingsUISFW)
	{
		removeControl(&m_checkboxDisplayHUD, false);
		removeControl(&m_checkboxDisplayHand, false);
		removeControl(&m_checkboxDisplayDeathMessages, false);
		removeControl(&m_checkboxDisplayAnimatedCharacter, false);
		removeControl(&m_checkboxSplitscreen, false);
		removeControl(&m_checkboxShowSplitscreenGamertags, false);
		removeControl(&m_sliderUISize, false);
		removeControl(&m_sliderUISizeSplitscreen, false);
		ShaderManager::RefreshPackList();
		InvalidateSettingsUILayout();
	}
#endif
}

void UIScene_SettingsUIMenu::updateTooltips()
{
	ui.SetTooltips( m_iPad, IDS_TOOLTIPS_SELECT,IDS_TOOLTIPS_BACK);
}

void UIScene_SettingsUIMenu::updateComponents()
{
	bool bNotInGame=(Minecraft::GetInstance()->level==NULL);
	if(bNotInGame)
	{
		m_parentLayer->showComponent(m_iPad,eUIComponent_Panorama,true);
		m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,true);
	}
	else
	{
		m_parentLayer->showComponent(m_iPad,eUIComponent_Panorama,false);

		if( app.GetLocalPlayerCount() == 1 ) m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,true);
		else m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,false);

	}
}

UIScene_SettingsUIMenu::~UIScene_SettingsUIMenu()
{
}

void UIScene_SettingsUIMenu::handleReload()
{
#if defined(_WINDOWS64)
	if(!g_bEnableSettingsUISFW)
	{
		removeControl(&m_checkboxDisplayHUD, false);
		removeControl(&m_checkboxDisplayHand, false);
		removeControl(&m_checkboxDisplayDeathMessages, false);
		removeControl(&m_checkboxDisplayAnimatedCharacter, false);
		removeControl(&m_checkboxSplitscreen, false);
		removeControl(&m_checkboxShowSplitscreenGamertags, false);
		removeControl(&m_sliderUISize, false);
		removeControl(&m_sliderUISizeSplitscreen, false);
		ShaderManager::RefreshPackList();
		InvalidateSettingsUILayout();
	}
#endif

	doHorizontalResizeCheck();
}

wstring UIScene_SettingsUIMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		return L"SettingsUIMenuSplit";
	}
	else
	{
		return L"SettingsUIMenu";
	}
}

void UIScene_SettingsUIMenu::tick()
{
	UIScene::tick();

#if defined(_WINDOWS64)
	if(g_bEnableSettingsUISFW)
	{
		return;
	}

	Minecraft *minecraft = Minecraft::GetInstance();
	if(minecraft != NULL && hasFocus(m_iPad))
	{
		SetupSettingsUICustomControls(minecraft);
		SyncSettingsUICustomControls(m_iPad);

		for(int i = 0; i < eSettingsUICustomSlider_Count; ++i)
		{
			if(i == eSettingsUICustomSlider_AnimatedCharacter && g_settingsUIResolutionSliderWaitForRelease)
			{
				g_settingsUISliders[i].SetValue(GetSettingsUISliderValue(m_iPad, (ESettingsUICustomSlider)i));
				if(g_settingsUIResolutionSliderCooldownTicks > 0)
				{
					--g_settingsUIResolutionSliderCooldownTicks;
				}

				if(!Mouse::isButtonDown(0) && g_settingsUIResolutionSliderCooldownTicks <= 0)
				{
					g_settingsUIResolutionSliderWaitForRelease = false;
					g_settingsUIResolutionSliderLockedValue = -1;
				}
				continue;
			}

			if(g_settingsUISliders[i].Update(minecraft))
			{
				ApplySettingsUISliderValue(m_iPad, (ESettingsUICustomSlider)i);
			}
		}
	}
#endif
}

void UIScene_SettingsUIMenu::render(S32 width, S32 height, C4JRender::eViewportType viewport)
{
#if defined(_WINDOWS64)
	if(!g_bEnableSettingsUISFW)
	{
		Minecraft *minecraft = Minecraft::GetInstance();
		if(minecraft == NULL || minecraft->textures == NULL || minecraft->font == NULL)
		{
			return;
		}

		SetupSettingsUICustomControls(minecraft);
		SyncSettingsUICustomControls(m_iPad);

		ui.setupCustomDrawGameState();
		ui.setupRenderPosition(viewport);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_LIGHTING);
		glDisable(GL_FOG);
		glDisable(GL_SCISSOR_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.0f);
		glEnable(GL_TEXTURE_2D);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		DrawSettingsUICustomControls(minecraft, minecraft->font, (int)viewport, m_iPad);
		return;
	}
#endif

	UIScene::render(width, height, viewport);
}

void UIScene_SettingsUIMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	ui.AnimateKeyPress(iPad, key, repeat, pressed, released);

#if defined(_WINDOWS64)
	if(!g_bEnableSettingsUISFW)
	{
		if(key == ACTION_MENU_CANCEL && pressed)
		{
			app.CheckGameSettingsChanged(true, iPad);
			navigateBack();
			handled = true;
		}
		return;
	}
#endif

	switch(key)
	{
	case ACTION_MENU_CANCEL:
		if(pressed)
		{
			// check the checkboxes
			app.SetGameSettings(m_iPad,eGameSetting_DisplayHUD,m_checkboxDisplayHUD.IsChecked()?1:0);
			app.SetGameSettings(m_iPad,eGameSetting_DisplayHand,m_checkboxDisplayHand.IsChecked()?1:0);
			app.SetGameSettings(m_iPad,eGameSetting_DisplaySplitscreenGamertags,m_checkboxShowSplitscreenGamertags.IsChecked()?1:0);
			app.SetGameSettings(m_iPad,eGameSetting_DeathMessages,m_checkboxDisplayDeathMessages.IsChecked()?1:0);
			app.SetGameSettings(m_iPad,eGameSetting_AnimatedCharacter,m_checkboxDisplayAnimatedCharacter.IsChecked()?1:0);

			// if the splitscreen vertical/horizontal has changed, need to update the scenes
			if(app.GetGameSettings(m_iPad,eGameSetting_SplitScreenVertical)!=(m_checkboxSplitscreen.IsChecked()?1:0))
			{
				// changed
				app.SetGameSettings(m_iPad,eGameSetting_SplitScreenVertical,m_checkboxSplitscreen.IsChecked()?1:0);

				// close the xui scenes, so we don't have the navigate backed to menu at the wrong place
				if(app.GetLocalPlayerCount()==2)
				{
					ui.CloseAllPlayersScenes();
				}
				else
				{
					navigateBack();
				}
			}
			else
			{
				navigateBack();
			}
			handled = true;
		}
		break;
	case ACTION_MENU_OK:
		sendInputToMovie(key, repeat, pressed, released);
		break;
	case ACTION_MENU_UP:
	case ACTION_MENU_DOWN:
	case ACTION_MENU_LEFT:
	case ACTION_MENU_RIGHT:
		sendInputToMovie(key, repeat, pressed, released);
		break;
	}
}

void UIScene_SettingsUIMenu::handleSliderMove(F64 sliderId, F64 currentValue)
{
	WCHAR TempString[256];
	int value = (int)currentValue;
	switch((int)sliderId)
	{
	case eControl_UISize:
		m_sliderUISize.handleSliderMove(value);

		swprintf( (WCHAR *)TempString, 256, L"%ls: %d", app.GetString( IDS_SLIDER_UISIZE ),value);		
		m_sliderUISize.setLabel(TempString);

		// is this different from the current value?
		if(value != app.GetGameSettings(m_iPad,eGameSetting_UISize)+1)
		{
			app.SetGameSettings(m_iPad,eGameSetting_UISize,value-1);
			// Apply the changes to the selected text position
			ui.UpdateSelectedItemPos(m_iPad);
		}

		break;
	case eControl_UISizeSplitscreen:
		m_sliderUISizeSplitscreen.handleSliderMove(value);

		swprintf( (WCHAR *)TempString, 256, L"%ls: %d", app.GetString( IDS_SLIDER_UISIZESPLITSCREEN ),value);			
		m_sliderUISizeSplitscreen.setLabel(TempString);

		if(value != app.GetGameSettings(m_iPad,eGameSetting_UISizeSplitscreen)+1)
		{
			// slider is 1 to 3
			app.SetGameSettings(m_iPad,eGameSetting_UISizeSplitscreen,value-1);
			// Apply the changes to the selected text position
			ui.UpdateSelectedItemPos(m_iPad);
		}

		break;
	}
}
