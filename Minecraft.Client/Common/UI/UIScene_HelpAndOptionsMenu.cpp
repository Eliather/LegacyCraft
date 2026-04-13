#include "stdafx.h"
#include "UI.h"
#include "UIScene_HelpAndOptionsMenu.h"
#include "..\..\Minecraft.h"

#if defined(_WINDOWS64)
#include "..\..\CustomGenericButton.h"
#include "..\..\CustomSlider.h"
#include "..\..\UserData_Info.h"
#include "..\App_Defines.h"

#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif

namespace
{
	bool g_bEnableHelpAndOptionsSFW = false;

	const float kHAOControlX = 400.0f;
	const float kHAOControlWidth = 480.0f;
	const float kHAOControlStartY = 260.0f;
	const float kHAOControlHeight = 40.0f;
	const float kHAOControlGap = 15.0f;

	struct HAOLanguageEntry
	{
		unsigned char languageId;
		const wchar_t *label;
	};

	const HAOLanguageEntry kHAOLanguageEntries[] =
	{
		{ MINECRAFT_LANGUAGE_ENGLISH,   L"English" },
		{ MINECRAFT_LANGUAGE_GERMAN,    L"Deutsch" },
		{ MINECRAFT_LANGUAGE_FRENCH,    L"Fran\u00E7ais" },
		{ MINECRAFT_LANGUAGE_SPANISH,   L"Espa\u00F1ol" },
		{ MINECRAFT_LANGUAGE_ITALIAN,   L"Italiano" },
		{ MINECRAFT_LANGUAGE_JAPANESE,  L"\u65E5\u672C\u8A9E" },
		{ MINECRAFT_LANGUAGE_KOREAN,    L"\uD55C\uAD6D\uC5B4" },
		{ MINECRAFT_LANGUAGE_BRAZILIAN, L"Portugu\u00EAs (BR)" },
		{ MINECRAFT_LANGUAGE_TCHINESE,  L"\u4E2D\u6587(\u7E41\u9AD4)" },
	};

	const int kHAOLanguageCount = sizeof(kHAOLanguageEntries) / sizeof(kHAOLanguageEntries[0]);

	enum EHAOButton
	{
		eHAO_HowToPlay = 0,
		eHAO_Controls,
		eHAO_Settings,
		eHAO_Credits,
		eHAO_Debug,
		eHAO_ButtonCount
	};

	CustomSlider g_haoLanguageSlider;
	bool g_haoLanguageSliderInitialised = false;
	bool g_bHAOLanguageApplyPending = false;
	CustomGenericButton g_haoButtons[eHAO_ButtonCount];
	bool g_haoButtonsInitialised = false;
	bool g_bHAOCreditsVisible = false;
	bool g_bHAODebugVisible = false;

	bool ShouldShowHAOCredits()
	{
		return app.GetLocalPlayerCount() <= 1;
	}

	bool ShouldShowHAODebug()
	{
#ifdef _FINAL_BUILD
		return false;
#else
		return app.DebugSettingsOn();
#endif
	}

	const wchar_t *GetHAOCustomButtonLabel(EHAOButton button)
	{
		switch(button)
		{
		case eHAO_HowToPlay: return app.GetString(IDS_HOW_TO_PLAY);
		case eHAO_Controls:  return app.GetString(IDS_CONTROLS);
		case eHAO_Settings:  return app.GetString(IDS_SETTINGS);
		case eHAO_Credits:   return app.GetString(IDS_CREDITS);
		case eHAO_Debug:     return app.GetString(IDS_DEBUG_SETTINGS);
		default:             return L"";
		}
	}

	int GetHAOLanguageIndex(unsigned char languageId)
	{
		for(int i = 0; i < kHAOLanguageCount; ++i)
		{
			if(kHAOLanguageEntries[i].languageId == languageId)
			{
				return i;
			}
		}

		return 3;
	}

	const wchar_t *GetHAOLanguagePrefix()
	{
		switch(XGetLanguage())
		{
		case XC_LANGUAGE_GERMAN:
			return L"Sprache";
		case XC_LANGUAGE_FRENCH:
			return L"Langue";
		case XC_LANGUAGE_SPANISH:
			return L"Idioma";
		case XC_LANGUAGE_ITALIAN:
			return L"Lingua";
		case XC_LANGUAGE_PORTUGUESE:
		case MINECRAFT_LANGUAGE_BRAZILIAN:
			return L"Idioma";
		case XC_LANGUAGE_JAPANESE:
			return L"\u8A00\u8A9E";
		case XC_LANGUAGE_KOREAN:
			return L"\uC5B8\uC5B4";
		case XC_LANGUAGE_TCHINESE:
			return L"\u8A9E\u8A00";
		default:
			return L"Language";
		}
	}

	unsigned char GetCurrentHAOLanguage(int iPad)
	{
		unsigned char languageId = app.GetMinecraftLanguage(iPad);
		if(languageId == MINECRAFT_LANGUAGE_DEFAULT)
		{
			languageId = (unsigned char)XGetLanguage();
		}
		return languageId;
	}

	int GetHAOLanguageFontGroup(unsigned char languageId)
	{
		switch(languageId)
		{
		case MINECRAFT_LANGUAGE_JAPANESE:
			return 1;
		case MINECRAFT_LANGUAGE_TCHINESE:
			return 2;
		case MINECRAFT_LANGUAGE_KOREAN:
			return 3;
		default:
			return 0;
		}
	}

	void SetupHAOCustomControls(bool creditsVisible, bool debugVisible)
	{
		if(!g_haoLanguageSliderInitialised)
		{
			g_haoLanguageSlider.Setup(kHAOControlX, kHAOControlStartY, kHAOControlWidth, kHAOControlHeight, kHAOLanguageCount - 1);
			g_haoLanguageSliderInitialised = true;
		}

		if(g_haoButtonsInitialised && g_bHAOCreditsVisible == creditsVisible && g_bHAODebugVisible == debugVisible)
		{
			return;
		}

		g_haoButtonsInitialised = true;
		g_bHAOCreditsVisible = creditsVisible;
		g_bHAODebugVisible = debugVisible;

		float y = kHAOControlStartY + kHAOControlHeight + kHAOControlGap;
		g_haoButtons[eHAO_HowToPlay].SetupMenuButton(kHAOControlX, y);
		y += kHAOControlHeight + kHAOControlGap;
		g_haoButtons[eHAO_Controls].SetupMenuButton(kHAOControlX, y);
		y += kHAOControlHeight + kHAOControlGap;
		g_haoButtons[eHAO_Settings].SetupMenuButton(kHAOControlX, y);
		y += kHAOControlHeight + kHAOControlGap;

		if(creditsVisible)
		{
			g_haoButtons[eHAO_Credits].SetupMenuButton(kHAOControlX, y);
			y += kHAOControlHeight + kHAOControlGap;
		}

		if(debugVisible)
		{
			g_haoButtons[eHAO_Debug].SetupMenuButton(kHAOControlX, y);
		}
	}

	void SyncHAOLanguageSlider(int iPad)
	{
		if(!g_haoLanguageSliderInitialised || g_haoLanguageSlider.IsDragging())
		{
			return;
		}

		g_haoLanguageSlider.SetValue(GetHAOLanguageIndex(GetCurrentHAOLanguage(iPad)));
	}

	std::wstring GetHAOLanguageSliderLabel()
	{
		int value = g_haoLanguageSlider.GetValue();
		if(value < 0)
		{
			value = 0;
		}
		if(value >= kHAOLanguageCount)
		{
			value = kHAOLanguageCount - 1;
		}

		return std::wstring(GetHAOLanguagePrefix()) + L": " + kHAOLanguageEntries[value].label;
	}

	bool ApplyHAOLanguageSelection(int iPad)
	{
		int value = g_haoLanguageSlider.GetValue();
		if(value < 0)
		{
			value = 0;
		}
		if(value >= kHAOLanguageCount)
		{
			value = kHAOLanguageCount - 1;
		}

		const unsigned char newLanguage = kHAOLanguageEntries[value].languageId;
		int primaryPad = ProfileManager.GetPrimaryPad();
		const unsigned char oldLanguage = (primaryPad >= 0) ? GetCurrentHAOLanguage(primaryPad) : GetCurrentHAOLanguage(iPad);
		if(primaryPad < 0)
		{
			primaryPad = iPad;
		}

		if(primaryPad >= 0 && GetCurrentHAOLanguage(primaryPad) == newLanguage && UserData_Info::GetLanguageId() == newLanguage)
		{
			return false;
		}

		if(primaryPad >= 0)
		{
			app.SetMinecraftLanguage(primaryPad, newLanguage);
			app.CheckGameSettingsChanged(true, primaryPad);
		}

		if(iPad >= 0 && iPad != primaryPad)
		{
			app.SetMinecraftLanguage(iPad, newLanguage);
			app.CheckGameSettingsChanged(true, iPad);
		}

		UserData_Info::SetLanguageId(newLanguage);
		app.loadStringTable();
		if(GetHAOLanguageFontGroup(oldLanguage) != GetHAOLanguageFontGroup(newLanguage))
		{
			ui.SetupFont();
		}
		return true;
	}

	void DrawHAOCustomControls(Minecraft *minecraft, C4JRender::eViewportType viewport, bool creditsVisible, bool debugVisible)
	{
		if(minecraft == NULL || minecraft->textures == NULL || minecraft->font == NULL)
		{
			return;
		}

		SetupHAOCustomControls(creditsVisible, debugVisible);

		g_haoLanguageSlider.RenderLabel(minecraft, minecraft->font, GetHAOLanguageSliderLabel(), (int)viewport);
		g_haoButtons[eHAO_HowToPlay].Render(minecraft, minecraft->font, GetHAOCustomButtonLabel(eHAO_HowToPlay), (int)viewport);
		g_haoButtons[eHAO_Controls].Render(minecraft, minecraft->font, GetHAOCustomButtonLabel(eHAO_Controls), (int)viewport);
		g_haoButtons[eHAO_Settings].Render(minecraft, minecraft->font, GetHAOCustomButtonLabel(eHAO_Settings), (int)viewport);
		if(creditsVisible)
		{
			g_haoButtons[eHAO_Credits].Render(minecraft, minecraft->font, GetHAOCustomButtonLabel(eHAO_Credits), (int)viewport);
		}
		if(debugVisible)
		{
			g_haoButtons[eHAO_Debug].Render(minecraft, minecraft->font, GetHAOCustomButtonLabel(eHAO_Debug), (int)viewport);
		}
	}
}
#endif

UIScene_HelpAndOptionsMenu::UIScene_HelpAndOptionsMenu(int iPad, void *initData, UILayer *parentLayer)
	: UIScene(iPad, parentLayer)
{
	initialiseMovie();

	m_bNotInGame = (Minecraft::GetInstance()->level == NULL);

	m_buttons[BUTTON_HAO_CHANGESKIN].init(app.GetString(IDS_CHANGE_SKIN), BUTTON_HAO_CHANGESKIN);
	m_buttons[BUTTON_HAO_HOWTOPLAY].init(app.GetString(IDS_HOW_TO_PLAY), BUTTON_HAO_HOWTOPLAY);
	m_buttons[BUTTON_HAO_CONTROLS].init(app.GetString(IDS_CONTROLS), BUTTON_HAO_CONTROLS);
	m_buttons[BUTTON_HAO_SETTINGS].init(app.GetString(IDS_SETTINGS), BUTTON_HAO_SETTINGS);
	m_buttons[BUTTON_HAO_CREDITS].init(app.GetString(IDS_CREDITS), BUTTON_HAO_CREDITS);
	m_buttons[BUTTON_HAO_DEBUG].init(app.GetString(IDS_DEBUG_SETTINGS), BUTTON_HAO_DEBUG);

	removeControl(&m_buttons[BUTTON_HAO_REINSTALL], false);

	doHorizontalResizeCheck();

#ifdef _FINAL_BUILD
	removeControl(&m_buttons[BUTTON_HAO_DEBUG], false);
#else
	if(!app.DebugSettingsOn()) removeControl(&m_buttons[BUTTON_HAO_DEBUG], false);
#endif

#ifdef _XBOX_ONE
	app.AddDLCRequest(e_Marketplace_Content);
	app.StartInstallDLCProcess(iPad);
#endif

	bool bNotInGame = (Minecraft::GetInstance()->level == NULL);

	if(m_iPad == ProfileManager.GetPrimaryPad() && bNotInGame)
	{
		app.DebugPrintf("Reinstall Menu required...\n");
	}
	else
	{
		removeControl(&m_buttons[BUTTON_HAO_REINSTALL], false);
	}

	if(app.GetLocalPlayerCount() > 1)
	{
		removeControl(&m_buttons[BUTTON_HAO_CREDITS], false);

#if TO_BE_IMPLEMENTED
		app.AdjustSplitscreenScene(m_hObj, &m_OriginalPosition, m_iPad, false);
#endif
		if(ProfileManager.GetPrimaryPad() != m_iPad)
		{
			removeControl(&m_buttons[BUTTON_HAO_REINSTALL], false);
		}
	}

	if(!ProfileManager.IsFullVersion())
	{
		removeControl(&m_buttons[BUTTON_HAO_CHANGESKIN], false);
	}

#if defined(_WINDOWS64)
	if(!g_bEnableHelpAndOptionsSFW)
	{
		removeControl(&m_buttons[BUTTON_HAO_CHANGESKIN], false);
		removeControl(&m_buttons[BUTTON_HAO_HOWTOPLAY], false);
		removeControl(&m_buttons[BUTTON_HAO_CONTROLS], false);
		removeControl(&m_buttons[BUTTON_HAO_SETTINGS], false);
		removeControl(&m_buttons[BUTTON_HAO_CREDITS], false);
		removeControl(&m_buttons[BUTTON_HAO_REINSTALL], false);
		removeControl(&m_buttons[BUTTON_HAO_DEBUG], false);

		SetupHAOCustomControls(ShouldShowHAOCredits(), ShouldShowHAODebug());
		SyncHAOLanguageSlider(m_iPad);
	}
#endif
}

UIScene_HelpAndOptionsMenu::~UIScene_HelpAndOptionsMenu()
{
}

wstring UIScene_HelpAndOptionsMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		return L"HelpAndOptionsMenuSplit";
	}
	else
	{
		return L"HelpAndOptionsMenu";
	}
}

void UIScene_HelpAndOptionsMenu::updateTooltips()
{
	ui.SetTooltips(m_iPad, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK);
}

void UIScene_HelpAndOptionsMenu::updateComponents()
{
	bool bNotInGame = (Minecraft::GetInstance()->level == NULL);
	if(bNotInGame)
	{
		m_parentLayer->showComponent(m_iPad, eUIComponent_Panorama, true);
#if !defined(_WINDOWS64)
		m_parentLayer->showComponent(m_iPad, eUIComponent_Logo, true);
#endif
	}
	else
	{
		m_parentLayer->showComponent(m_iPad, eUIComponent_Panorama, false);
#if !defined(_WINDOWS64)
		if(app.GetLocalPlayerCount() == 1) m_parentLayer->showComponent(m_iPad, eUIComponent_Logo, true);
		else m_parentLayer->showComponent(m_iPad, eUIComponent_Logo, false);
#endif
	}
}

void UIScene_HelpAndOptionsMenu::handleReload()
{
#ifdef _FINAL_BUILD
	removeControl(&m_buttons[BUTTON_HAO_DEBUG], false);
#else
	if(!app.DebugSettingsOn()) removeControl(&m_buttons[BUTTON_HAO_DEBUG], false);
#endif

	bool bNotInGame = (Minecraft::GetInstance()->level == NULL);

	if(m_iPad == ProfileManager.GetPrimaryPad() && bNotInGame)
	{
		app.DebugPrintf("Reinstall Menu required...\n");
	}
	else
	{
		removeControl(&m_buttons[BUTTON_HAO_REINSTALL], false);
	}

	if(app.GetLocalPlayerCount() > 1)
	{
		removeControl(&m_buttons[BUTTON_HAO_CREDITS], false);

#if TO_BE_IMPLEMENTED
		app.AdjustSplitscreenScene(m_hObj, &m_OriginalPosition, m_iPad, false);
#endif
		if(ProfileManager.GetPrimaryPad() != m_iPad)
		{
			removeControl(&m_buttons[BUTTON_HAO_REINSTALL], false);
		}
	}

	if(!ProfileManager.IsFullVersion())
	{
#if TO_BE_IMPLEMENTED
		m_Buttons[BUTTON_HAO_CHANGESKIN].SetEnable(FALSE);
		m_Buttons[BUTTON_HAO_CHANGESKIN].EnableInput(FALSE);
		XuiElementSetUserFocus(m_Buttons[BUTTON_HAO_HOWTOPLAY].m_hObj, m_iPad);
#endif
	}

	if(!ProfileManager.IsFullVersion())
	{
		removeControl(&m_buttons[BUTTON_HAO_CHANGESKIN], false);
	}

#if defined(_WINDOWS64)
	if(!g_bEnableHelpAndOptionsSFW)
	{
		removeControl(&m_buttons[BUTTON_HAO_CHANGESKIN], false);
		removeControl(&m_buttons[BUTTON_HAO_HOWTOPLAY], false);
		removeControl(&m_buttons[BUTTON_HAO_CONTROLS], false);
		removeControl(&m_buttons[BUTTON_HAO_SETTINGS], false);
		removeControl(&m_buttons[BUTTON_HAO_CREDITS], false);
		removeControl(&m_buttons[BUTTON_HAO_REINSTALL], false);
		removeControl(&m_buttons[BUTTON_HAO_DEBUG], false);

		SetupHAOCustomControls(ShouldShowHAOCredits(), ShouldShowHAODebug());
		SyncHAOLanguageSlider(m_iPad);
	}
#endif

	doHorizontalResizeCheck();
}

void UIScene_HelpAndOptionsMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	switch(key)
	{
	case ACTION_MENU_CANCEL:
		if(pressed && !repeat)
		{
			navigateBack();
		}
		break;
	case ACTION_MENU_OK:
		if(pressed)
		{
#if defined(_WINDOWS64)
			if(g_bEnableHelpAndOptionsSFW)
#endif
			{
				ui.PlayUISFX(eSFX_Press);
			}
		}
		// fall through
	case ACTION_MENU_UP:
	case ACTION_MENU_DOWN:
#if defined(_WINDOWS64)
		if(g_bEnableHelpAndOptionsSFW)
#endif
		{
			sendInputToMovie(key, repeat, pressed, released);
		}
		break;
	}
}

void UIScene_HelpAndOptionsMenu::handlePress(F64 controlId, F64 childId)
{
#if defined(_WINDOWS64)
	if(!g_bEnableHelpAndOptionsSFW) return;
#endif

	switch((int)controlId)
	{
	case BUTTON_HAO_CHANGESKIN:
		ui.NavigateToScene(m_iPad, eUIScene_SkinSelectMenu);
		break;
	case BUTTON_HAO_HOWTOPLAY:
		ui.NavigateToScene(m_iPad, eUIScene_HowToPlayMenu);
		break;
	case BUTTON_HAO_CONTROLS:
		ui.NavigateToScene(m_iPad, eUIScene_ControlsMenu);
		break;
	case BUTTON_HAO_SETTINGS:
		ui.NavigateToScene(m_iPad, eUIScene_SettingsMenu);
		break;
	case BUTTON_HAO_CREDITS:
		ui.NavigateToScene(m_iPad, eUIScene_Credits);
		break;
	case BUTTON_HAO_REINSTALL:
		ui.NavigateToScene(m_iPad, eUIScene_ReinstallMenu);
		break;
	case BUTTON_HAO_DEBUG:
		ui.NavigateToScene(m_iPad, eUIScene_DebugOptions);
		break;
	}
}

void UIScene_HelpAndOptionsMenu::tick()
{
	UIScene::tick();

#if defined(_WINDOWS64)
	if(g_bEnableHelpAndOptionsSFW) return;

	Minecraft *minecraft = Minecraft::GetInstance();
	if(minecraft != NULL && hasFocus(m_iPad))
	{
		const bool creditsVisible = ShouldShowHAOCredits();
		const bool debugVisible = ShouldShowHAODebug();

		SetupHAOCustomControls(creditsVisible, debugVisible);
		SyncHAOLanguageSlider(m_iPad);

		const bool wasDraggingLanguageSlider = g_haoLanguageSlider.IsDragging();
		if(g_haoLanguageSlider.Update(minecraft))
		{
			g_bHAOLanguageApplyPending = true;
		}

		const bool isDraggingLanguageSlider = g_haoLanguageSlider.IsDragging();
		if(g_bHAOLanguageApplyPending && wasDraggingLanguageSlider && !isDraggingLanguageSlider)
		{
			g_bHAOLanguageApplyPending = false;
			if(ApplyHAOLanguageSelection(m_iPad))
			{
				updateTooltips();
				handleReload();
				return;
			}
		}

		if(g_haoButtons[eHAO_HowToPlay].Update(minecraft))
		{
			ui.NavigateToScene(m_iPad, eUIScene_HowToPlayMenu);
		}
		if(g_haoButtons[eHAO_Controls].Update(minecraft))
		{
			ui.NavigateToScene(m_iPad, eUIScene_ControlsMenu);
		}
		if(g_haoButtons[eHAO_Settings].Update(minecraft))
		{
			ui.NavigateToScene(m_iPad, eUIScene_SettingsMenu);
		}
		if(creditsVisible && g_haoButtons[eHAO_Credits].Update(minecraft))
		{
			ui.NavigateToScene(m_iPad, eUIScene_Credits);
		}
		if(debugVisible && g_haoButtons[eHAO_Debug].Update(minecraft))
		{
			ui.NavigateToScene(m_iPad, eUIScene_DebugOptions);
		}
	}
#endif
}

void UIScene_HelpAndOptionsMenu::render(S32 width, S32 height, C4JRender::eViewportType viewport)
{
	UIScene::render(width, height, viewport);

#if defined(_WINDOWS64)
	if(g_bEnableHelpAndOptionsSFW) return;

	Minecraft *minecraft = Minecraft::GetInstance();
	if(minecraft == NULL || minecraft->textures == NULL || minecraft->font == NULL) return;

	const bool creditsVisible = ShouldShowHAOCredits();
	const bool debugVisible = ShouldShowHAODebug();

	SetupHAOCustomControls(creditsVisible, debugVisible);
	SyncHAOLanguageSlider(m_iPad);

	// HelpAndOptions has no iggy custom-draw region like MainMenu's "Splash",
	// so prepare the game render state manually before drawing code-driven widgets.
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

	DrawHAOCustomControls(minecraft, viewport, creditsVisible, debugVisible);
#endif
}

void UIScene_HelpAndOptionsMenu::customDraw(IggyCustomDrawCallbackRegion *region)
{
}
