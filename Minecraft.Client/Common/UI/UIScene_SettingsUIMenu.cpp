#include "stdafx.h"
#include "UI.h"
#include "UIScene_SettingsUIMenu.h"
#include "..\..\Minecraft.h"
#include "..\..\GameRenderer.h"
#include "..\..\Options.h"
#include "..\..\UserData_Info.h"

#if defined(_WINDOWS64)
#include "..\..\CustomGenericBackground.h"
#include "..\..\CustomSlider.h"

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
		eSettingsUICustomSlider_Count
	};

	CustomGenericBackground g_settingsUIPanel;
	CustomSlider g_settingsUISliders[eSettingsUICustomSlider_Count];
	bool g_settingsUIControlsInitialised = false;
	int g_settingsUIWidth = -1;
	int g_settingsUIHeight = -1;

	const float kSettingsUIPanelWidth = 920.0f;
	const float kSettingsUIPanelHeight = 470.0f;
	const float kSettingsUITitleScale = 3.0f;
	const float kSettingsUISliderXInset = 140.0f;
	const float kSettingsUISliderStartYOffset = 110.0f;
	const float kSettingsUISliderWidth = 640.0f;
	const float kSettingsUISliderHeight = 36.0f;
	const float kSettingsUISliderGap = 18.0f;

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

	int GetSettingsUIGameSetting(ESettingsUICustomSlider slider)
	{
		switch(slider)
		{
		case eSettingsUICustomSlider_Gamma:              return eGameSetting_Gamma;
		case eSettingsUICustomSlider_AnimatedCharacter:  return eGameSetting_AnimatedCharacter;
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
		case eSettingsUICustomSlider_AnimatedCharacter:  return IDS_CHECKBOX_ANIMATED_CHARACTER;
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
			return 3;
		case eSettingsUICustomSlider_UISize:
		case eSettingsUICustomSlider_UISizeSplitscreen:
			return 2;
		default:
			return 1;
		}
	}

	bool IsSettingsUIBooleanSlider(ESettingsUICustomSlider slider)
	{
		switch(slider)
		{
		case eSettingsUICustomSlider_AnimatedCharacter:
			return true;
		default:
			return false;
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
				return minecraft->options->viewDistance & 0x03;
			}
			return 0;
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
		if(y < 120.0f)
		{
			y = 120.0f;
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
			return std::wstring(baseLabel);
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
#ifdef __ORBIS__
	case ACTION_MENU_TOUCHPAD_PRESS:
#endif
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
