#include "stdafx.h"
#include "UI.h"
#include "UIScene_LaunchMoreOptionsMenu.h"
#ifdef _WINDOWS64
#include "..\..\CustomTextInputModal.h"
#endif

#define GAME_CREATE_ONLINE_TIMER_ID 0
#define GAME_CREATE_ONLINE_TIMER_TIME 100

#ifdef _LARGE_WORLDS
int m_iWorldSizeTitleA[4] = 
{
	IDS_WORLD_SIZE_TITLE_CLASSIC,
	IDS_WORLD_SIZE_TITLE_SMALL,
	IDS_WORLD_SIZE_TITLE_MEDIUM,
	IDS_WORLD_SIZE_TITLE_LARGE,
};
#endif

UIScene_LaunchMoreOptionsMenu::UIScene_LaunchMoreOptionsMenu(int iPad, void *initData, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	// Setup all the Iggy references we need for this scene
	initialiseMovie();

	m_params = (LaunchMoreOptionsMenuInitData *)initData;

	m_labelWorldOptions.init(app.GetString(IDS_WORLD_OPTIONS));

	IggyDataValue result;
	IggyDataValue value[1];
	value[0].type = IGGY_DATATYPE_number;
	value[0].number = m_params->bGenerateOptions?0:1;
	IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcSetMenuType , 1 , value );

	m_bMultiplayerAllowed = ProfileManager.IsSignedInLive( m_params->iPad ) && ProfileManager.AllowedToPlayMultiplayer(m_params->iPad);
	
	bool bOnlineGame, bInviteOnly, bAllowFriendsOfFriends;
	bOnlineGame				= m_params->bOnlineGame;
	bInviteOnly				= m_params->bInviteOnly;
	bAllowFriendsOfFriends	= m_params->bAllowFriendsOfFriends;

	// 4J-PB - to stop an offline game being able to select the online flag
	if(ProfileManager.IsSignedInLive(m_params->iPad) == false)
	{
		m_checkboxes[eLaunchCheckbox_Online].SetEnable(false);
	}

	if ( m_params->bOnlineSettingChangedBySystem && !m_bMultiplayerAllowed )
	{
		// 4J-JEV: Disable and uncheck these boxes if they can't play multiplayer.
		m_checkboxes[eLaunchCheckbox_Online].SetEnable(false);
		m_checkboxes[eLaunchCheckbox_InviteOnly].SetEnable(false);
		m_checkboxes[eLaunchCheckbox_AllowFoF].SetEnable(false);

		bOnlineGame	= bInviteOnly = bAllowFriendsOfFriends = false;	
	}
	else if(!m_params->bOnlineGame)
	{

		m_checkboxes[eLaunchCheckbox_InviteOnly].SetEnable(false);
		m_checkboxes[eLaunchCheckbox_AllowFoF].SetEnable(false);
	}

	m_checkboxes[eLaunchCheckbox_Online].init(app.GetString(IDS_ONLINE_GAME),eLaunchCheckbox_Online,bOnlineGame);
	m_checkboxes[eLaunchCheckbox_InviteOnly].init(app.GetString(IDS_INVITE_ONLY),eLaunchCheckbox_InviteOnly,bInviteOnly);
	m_checkboxes[eLaunchCheckbox_AllowFoF].init(app.GetString(IDS_ALLOWFRIENDSOFFRIENDS),eLaunchCheckbox_AllowFoF,bAllowFriendsOfFriends);
	m_checkboxes[eLaunchCheckbox_PVP].init(app.GetString(IDS_PLAYER_VS_PLAYER),eLaunchCheckbox_PVP,m_params->bPVP);
	m_checkboxes[eLaunchCheckbox_TrustSystem].init(app.GetString(IDS_TRUST_PLAYERS),eLaunchCheckbox_TrustSystem,m_params->bTrust);
	m_checkboxes[eLaunchCheckbox_FireSpreads].init(app.GetString(IDS_FIRE_SPREADS),eLaunchCheckbox_FireSpreads,m_params->bFireSpreads);
	m_checkboxes[eLaunchCheckbox_TNT].init(app.GetString(IDS_TNT_EXPLODES),eLaunchCheckbox_TNT,m_params->bTNT);
	m_checkboxes[eLaunchCheckbox_HostPrivileges].init(app.GetString(IDS_HOST_PRIVILEGES),eLaunchCheckbox_HostPrivileges,m_params->bHostPrivileges);
	m_checkboxes[eLaunchCheckbox_ResetNether].init(app.GetString(IDS_RESET_NETHER),eLaunchCheckbox_ResetNether,m_params->bResetNether);
	m_checkboxes[eLaunchCheckbox_Structures].init(app.GetString(IDS_GENERATE_STRUCTURES),eLaunchCheckbox_Structures,m_params->bStructures);
	m_checkboxes[eLaunchCheckbox_FlatWorld].init(app.GetString(IDS_SUPERFLAT_WORLD),eLaunchCheckbox_FlatWorld,m_params->bFlatWorld);
	m_checkboxes[eLaunchCheckbox_BonusChest].init(app.GetString(IDS_BONUS_CHEST),eLaunchCheckbox_BonusChest,m_params->bBonusChest);

	if(m_loadedResolution == eSceneResolution_1080)
	{
#ifdef _LARGE_WORLDS
		m_labelGameOptions.init( app.GetString(IDS_GAME_OPTIONS) );
		m_labelSeed.init(app.GetString(IDS_CREATE_NEW_WORLD_SEED));
		m_labelRandomSeed.init(app.GetString(IDS_CREATE_NEW_WORLD_RANDOM_SEED));
		m_editSeed.init(m_params->seed, eControl_EditSeed);
		m_labelWorldSize.init(app.GetString(IDS_WORLD_SIZE));
		m_sliderWorldSize.init(app.GetString(m_iWorldSizeTitleA[m_params->worldSize]),eControl_WorldSize,0,3,m_params->worldSize);

		m_checkboxes[eLaunchCheckbox_DisableSaving].init( app.GetString(IDS_DISABLE_SAVING), eLaunchCheckbox_DisableSaving, m_params->bDisableSaving );
#endif
	}

	// Only the Xbox 360 needs a reset nether
	// 4J-PB - PS3 needs it now
	// #ifndef _XBOX
	// 	if(!m_params->bGenerateOptions) removeControl( &m_checkboxes[eLaunchCheckbox_ResetNether], false );
	// #endif

	// set the default text
#ifdef _LARGE_WORLDS
	wstring wsText=L"";
	if(m_params->bGenerateOptions)
	{
		wsText = app.GetString(IDS_GAMEOPTION_SEED);
	}
	else
	{
		wsText = app.GetString(IDS_GAMEOPTION_ONLINE);
	}
#else
	wstring wsText=app.GetString(IDS_GAMEOPTION_ONLINE);
#endif
	EHTMLFontSize size = eHTMLSize_Normal;
	if(!RenderManager.IsHiDef() && !RenderManager.IsWidescreen())
	{
		size = eHTMLSize_Splitscreen;
	}
	wchar_t startTags[64];
	swprintf(startTags,64,L"<font color=\"#%08x\">",app.GetHTMLColour(eHTMLColor_White));
	wsText= startTags + wsText;
	m_labelDescription.init(wsText);

	addTimer(GAME_CREATE_ONLINE_TIMER_ID,GAME_CREATE_ONLINE_TIMER_TIME);

#ifdef __PSVITA__
	ui.TouchBoxRebuild(this);
#endif

	m_bIgnoreInput = false;
	m_tabIndex = 0;
#ifdef _WINDOWS64
	m_wasMouseLeftDown = false;
	updateTabMouseVisibility();
#endif
}

void UIScene_LaunchMoreOptionsMenu::updateTooltips()
{
	int changeTabTooltip = -1;

#ifdef _LARGE_WORLDS
	if (m_loadedResolution == eSceneResolution_1080 && m_params->bGenerateOptions)
	{
		// Set tooltip for change tab (only two tabs)
		if (m_tabIndex == 0)
		{
			changeTabTooltip = IDS_GAME_OPTIONS;
		}
		else 
		{
			changeTabTooltip = IDS_WORLD_OPTIONS;
		}
	}
#endif

	// If there's a change tab tooltip, left bumper symbol should show but not the text (-2)
	int lb = changeTabTooltip == -1 ? -1 : -2;

	ui.SetTooltips( DEFAULT_XUI_MENU_USER, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK, -1, -1, -1, -1, lb, changeTabTooltip);
}

void UIScene_LaunchMoreOptionsMenu::updateComponents()
{
	m_parentLayer->showComponent(m_iPad,eUIComponent_Panorama,true);
#ifdef _LARGE_WORLDS
	m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,true);
#else
	m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,false);
#endif
}

wstring UIScene_LaunchMoreOptionsMenu::getMoviePath()
{
	return L"LaunchMoreOptionsMenu";
}

void UIScene_LaunchMoreOptionsMenu::tick()
{
	UIScene::tick();

	bool bMultiplayerAllowed = ProfileManager.IsSignedInLive(m_params->iPad) && ProfileManager.AllowedToPlayMultiplayer(m_params->iPad);

	if (bMultiplayerAllowed != m_bMultiplayerAllowed)
	{
		m_checkboxes[    eLaunchCheckbox_Online].SetEnable(bMultiplayerAllowed);
		m_checkboxes[eLaunchCheckbox_InviteOnly].SetEnable(bMultiplayerAllowed);
		m_checkboxes[  eLaunchCheckbox_AllowFoF].SetEnable(bMultiplayerAllowed);

		if (bMultiplayerAllowed)
		{
			m_checkboxes[  eLaunchCheckbox_Online].setChecked(true);
			m_checkboxes[eLaunchCheckbox_AllowFoF].setChecked(true);
		}

		m_bMultiplayerAllowed = bMultiplayerAllowed;
	}

#ifdef _WINDOWS64
	if(hasFocus(m_iPad) && !m_bIgnoreInput && m_loadedResolution == eSceneResolution_1080 &&
	   m_params != NULL && m_params->bGenerateOptions && !Mouse::isGrabbed())
	{
		const bool isLeftDown = Mouse::isButtonDown(0) || Mouse::isButtonPressed(0);
		const bool clicked = isLeftDown && !m_wasMouseLeftDown;
		m_wasMouseLeftDown = isLeftDown;

		if(clicked)
		{
			const float screenW = ui.getScreenWidth();
			const float screenH = ui.getScreenHeight();
			const S32 sceneW = getRenderWidth();
			const S32 sceneH = getRenderHeight();
			if(screenW > 0.0f && screenH > 0.0f && sceneW > 0 && sceneH > 0)
			{
				const S32 localX = (S32)(((float)Mouse::getX() * (float)sceneW) / screenW);
				const S32 localY = (S32)(((float)Mouse::getY() * (float)sceneH) / screenH);

				auto hitTabLabel = [&](UIControl_Label &label) -> bool
				{
					label.UpdateControl();
					const S32 lx1 = label.getXPos() - 28;
					const S32 ly1 = label.getYPos() - 16;
					const S32 lx2 = label.getXPos() + label.getWidth() + 28;
					const S32 ly2 = label.getYPos() + label.getHeight() + 16;
					return (localX >= lx1 && localX <= lx2 && localY >= ly1 && localY <= ly2);
				};

				bool switchTab = false;
				if(hitTabLabel(m_labelWorldOptions) && m_tabIndex != 0)
				{
					m_tabIndex = 0;
					switchTab = true;
				}
				else if(hitTabLabel(m_labelGameOptions) && m_tabIndex != 1)
				{
					m_tabIndex = 1;
					switchTab = true;
				}

				if(switchTab)
				{
					ui.PlayUISFX(eSFX_Scroll);
					updateTooltips();
					updateTabMouseVisibility();
					IggyDataValue result;
					IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcChangeTab, 0, NULL);
				}
			}
		}
	}
	else
	{
		m_wasMouseLeftDown = Mouse::isButtonDown(0) || Mouse::isButtonPressed(0);
	}
#endif
}

UIControl *UIScene_LaunchMoreOptionsMenu::GetMainPanel()
{
	if(m_loadedResolution == eSceneResolution_1080)
	{
		const bool showWorldOptions = (m_params != NULL && m_params->bGenerateOptions) && (m_tabIndex == 0);
		return showWorldOptions ? &m_worldOptions : &m_gameOptions;
	}
	return NULL;
}

#ifdef _WINDOWS64
void UIScene_LaunchMoreOptionsMenu::updateTabMouseVisibility()
{
	if(m_loadedResolution != eSceneResolution_1080) return;

	const bool showWorldOptions = (m_params != NULL && m_params->bGenerateOptions) && (m_tabIndex == 0);

	m_editSeed.setHidden(!showWorldOptions);
	m_sliderWorldSize.setHidden(!showWorldOptions);
	m_checkboxes[eLaunchCheckbox_Structures].setHidden(!showWorldOptions);
	m_checkboxes[eLaunchCheckbox_BonusChest].setHidden(!showWorldOptions);
	m_checkboxes[eLaunchCheckbox_FlatWorld].setHidden(!showWorldOptions);

	m_checkboxes[eLaunchCheckbox_Online].setHidden(showWorldOptions);
	m_checkboxes[eLaunchCheckbox_InviteOnly].setHidden(showWorldOptions);
	m_checkboxes[eLaunchCheckbox_AllowFoF].setHidden(showWorldOptions);
	m_checkboxes[eLaunchCheckbox_PVP].setHidden(showWorldOptions);
	m_checkboxes[eLaunchCheckbox_TrustSystem].setHidden(showWorldOptions);
	m_checkboxes[eLaunchCheckbox_FireSpreads].setHidden(showWorldOptions);
	m_checkboxes[eLaunchCheckbox_TNT].setHidden(showWorldOptions);
	m_checkboxes[eLaunchCheckbox_HostPrivileges].setHidden(showWorldOptions);
	m_checkboxes[eLaunchCheckbox_ResetNether].setHidden(showWorldOptions);
	m_checkboxes[eLaunchCheckbox_DisableSaving].setHidden(showWorldOptions);
}
#endif

void UIScene_LaunchMoreOptionsMenu::handleDestroy()
{
#ifdef _WINDOWS64
	CustomTextInputModal::ForceCloseForParam(this);
#endif
#ifdef __PSVITA__
	app.DebugPrintf("missing InputManager.DestroyKeyboard on Vita !!!!!!\n");
#endif

	// so shut down the keyboard if it is displayed
#if defined(_DURANGO)
	InputManager.DestroyKeyboard();
#endif
}

void UIScene_LaunchMoreOptionsMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	if(m_bIgnoreInput) return;

	//app.DebugPrintf("UIScene_DebugOverlay handling input for pad %d, key %d, down- %s, pressed- %s, released- %s\n", iPad, key, down?"TRUE":"FALSE", pressed?"TRUE":"FALSE", released?"TRUE":"FALSE");
	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	switch(key)
	{
	case ACTION_MENU_CANCEL:
		if(pressed)
		{
			navigateBack();
			handled = true;
		}
		break;
	case ACTION_MENU_OK:
#ifdef _WINDOWS64
		if(pressed && !Mouse::isGrabbed() && Mouse::isButtonPressed(0))
		{
			const int focusId = getControlFocus();
			if(focusId >= 0 && focusId < eLaunchCheckboxes_Count)
			{
				if(m_checkboxes[focusId].IsEnabled())
				{
					const bool newChecked = !m_checkboxes[focusId].IsChecked();
					m_checkboxes[focusId].TouchSetCheckbox(newChecked);
					handleCheckboxToggled((F64)focusId, newChecked);
				}
				handled = true;
				break;
			}
			else if(focusId == eControl_EditSeed)
			{
				handlePress(eControl_EditSeed, 0);
				handled = true;
				break;
			}
		}
#endif
		// 4J-JEV: Inform user why their game must be offline.
#if defined _XBOX_ONE
		{
			UIControl_CheckBox *checkboxOnline = &m_checkboxes[eLaunchCheckbox_Online];
			if ( pressed && controlHasFocus( checkboxOnline->getId()) && !checkboxOnline->IsEnabled() )
			{
				UINT uiIDA[1] = { IDS_CONFIRM_OK };
				ui.RequestMessageBox(IDS_PRO_NOTONLINE_TITLE, IDS_PRO_XBOXLIVE_NOTIFICATION, uiIDA, 1, iPad, NULL, NULL, app.GetStringTable()); 
			}
		}
#endif

	case ACTION_MENU_UP:
	case ACTION_MENU_DOWN:
	case ACTION_MENU_LEFT:
	case ACTION_MENU_RIGHT:
	case ACTION_MENU_PAGEUP:
	case ACTION_MENU_PAGEDOWN:
	case ACTION_MENU_OTHER_STICK_UP:
	case ACTION_MENU_OTHER_STICK_DOWN:
		sendInputToMovie(key, repeat, pressed, released);
		handled = true;
		break;
	case ACTION_MENU_LEFT_SCROLL:
	case ACTION_MENU_RIGHT_SCROLL:
		if(pressed && m_loadedResolution == eSceneResolution_1080)
		{
			// Toggle tab index
			m_tabIndex = m_tabIndex == 0 ? 1 : 0;
			updateTooltips();
#ifdef _WINDOWS64
			updateTabMouseVisibility();
#endif
			IggyDataValue result;
			IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcChangeTab , 0 , NULL );
		}
		break;
	}
}

void UIScene_LaunchMoreOptionsMenu::handleCheckboxToggled(F64 controlId, bool selected)
{
	//CD - Added for audio
	ui.PlayUISFX(eSFX_Press);

	switch((EControls)((int)controlId))
	{
	case eLaunchCheckbox_Online:
		m_params->bOnlineGame = selected;
		break;
	case eLaunchCheckbox_InviteOnly:
		m_params->bInviteOnly = selected;
		break;
	case eLaunchCheckbox_AllowFoF:
		m_params->bAllowFriendsOfFriends = selected;
		break;
	case eLaunchCheckbox_PVP:
		m_params->bPVP = selected;
		break;
	case eLaunchCheckbox_TrustSystem:
		m_params->bTrust = selected;
		break;
	case eLaunchCheckbox_FireSpreads:
		m_params->bFireSpreads = selected;
		break;
	case eLaunchCheckbox_TNT:
		m_params->bTNT = selected;
		break;
	case eLaunchCheckbox_HostPrivileges:
		m_params->bHostPrivileges = selected;
		break;
	case eLaunchCheckbox_ResetNether:
		m_params->bResetNether = selected;
		break;
	case eLaunchCheckbox_Structures:
		m_params->bStructures = selected;
		break;
	case eLaunchCheckbox_FlatWorld:
		m_params->bFlatWorld = selected;
		break;
	case eLaunchCheckbox_BonusChest:
		m_params->bBonusChest = selected;
		break;
	case eLaunchCheckbox_DisableSaving:
		m_params->bDisableSaving = selected;
		break;
	};
}

void UIScene_LaunchMoreOptionsMenu::handleFocusChange(F64 controlId, F64 childId)
{
	int stringId = 0;
	switch((int)controlId)
	{
	case eLaunchCheckbox_Online:
		stringId = IDS_GAMEOPTION_ONLINE;
		break;
	case eLaunchCheckbox_InviteOnly:
		stringId = IDS_GAMEOPTION_INVITEONLY;
		break;
	case eLaunchCheckbox_AllowFoF:
		stringId = IDS_GAMEOPTION_ALLOWFOF;
		break;
	case eLaunchCheckbox_PVP:
		stringId = IDS_GAMEOPTION_PVP;
		break;
	case eLaunchCheckbox_TrustSystem:
		stringId = IDS_GAMEOPTION_TRUST;
		break;
	case eLaunchCheckbox_FireSpreads:
		stringId = IDS_GAMEOPTION_FIRE_SPREADS;
		break;
	case eLaunchCheckbox_TNT:
		stringId = IDS_GAMEOPTION_TNT_EXPLODES;
		break;
	case eLaunchCheckbox_HostPrivileges:
		stringId = IDS_GAMEOPTION_HOST_PRIVILEGES;
		break;
	case eLaunchCheckbox_ResetNether:
		stringId = IDS_GAMEOPTION_RESET_NETHER;
		break;
	case eLaunchCheckbox_Structures:
		stringId = IDS_GAMEOPTION_STRUCTURES;
		break;
	case eLaunchCheckbox_FlatWorld:
		stringId = IDS_GAMEOPTION_SUPERFLAT;
		break;
	case eLaunchCheckbox_BonusChest:
		stringId = IDS_GAMEOPTION_BONUS_CHEST;
		break;
#ifdef _LARGE_WORLDS
	case eControl_EditSeed:
		stringId = IDS_GAMEOPTION_SEED;
		break;
	case eControl_WorldSize:
		stringId = IDS_GAMEOPTION_WORLD_SIZE;
		break;
	case eLaunchCheckbox_DisableSaving:
		stringId = IDS_GAMEOPTION_DISABLE_SAVING;
		break;
#endif
	};

	wstring wsText=app.GetString(stringId);	
	EHTMLFontSize size = eHTMLSize_Normal;
	if(!RenderManager.IsHiDef() && !RenderManager.IsWidescreen())
	{
		size = eHTMLSize_Splitscreen;
	}
	wchar_t startTags[64];
	swprintf(startTags,64,L"<font color=\"#%08x\">",app.GetHTMLColour(eHTMLColor_White));
	wsText= startTags + wsText;

	m_labelDescription.setLabel(wsText);
}

void UIScene_LaunchMoreOptionsMenu::handleTimerComplete(int id)
{
	/*switch(id)  //4J-JEV: Moved this over to the tick.
	{
	case GAME_CREATE_ONLINE_TIMER_ID:
		{
			bool bMultiplayerAllowed 
				=	ProfileManager.IsSignedInLive(m_params->iPad) 
				&&	ProfileManager.AllowedToPlayMultiplayer(m_params->iPad);

			if (bMultiplayerAllowed != m_bMultiplayerAllowed)
			{
				m_checkboxes[    eLaunchCheckbox_Online].SetEnable(bMultiplayerAllowed);
				m_checkboxes[eLaunchCheckbox_InviteOnly].SetEnable(bMultiplayerAllowed);
				m_checkboxes[  eLaunchCheckbox_AllowFoF].SetEnable(bMultiplayerAllowed);

				m_checkboxes[eLaunchCheckbox_Online].setChecked(bMultiplayerAllowed);

				m_bMultiplayerAllowed = bMultiplayerAllowed;
			}
		}
		break;
	};*/
}

int UIScene_LaunchMoreOptionsMenu::KeyboardCompleteSeedCallback(LPVOID lpParam,bool bRes)
{
	UIScene_LaunchMoreOptionsMenu *pClass=(UIScene_LaunchMoreOptionsMenu *)lpParam;
	pClass->m_bIgnoreInput=false;
	// 4J HEG - No reason to set value if keyboard was cancelled
	if (bRes)
	{
		uint16_t pchText[128];
		ZeroMemory(pchText, 128 * sizeof(uint16_t) );
		InputManager.GetText(pchText);
		pClass->m_editSeed.setLabel((wchar_t *)pchText);
		pClass->m_params->seed = (wchar_t *)pchText;
	}
	return 0;
}

#ifdef _WINDOWS64
namespace
{
	bool IsAcceptedMoreOptionsSeedChar(wchar_t ch)
	{
		return (ch >= 32 && ch <= 126);
	}

	const wchar_t *GetMoreOptionsSeedHintText()
	{
		switch(XGetLanguage())
		{
		case XC_LANGUAGE_SPANISH:
			return L"Opcional (dejar vacio para aleatoria)";
		case XC_LANGUAGE_FRENCH:
			return L"Optionnel (vide = aleatoire)";
		case XC_LANGUAGE_GERMAN:
			return L"Optional (leer = zufallig)";
		case XC_LANGUAGE_ITALIAN:
			return L"Opzionale (vuoto = casuale)";
		case XC_LANGUAGE_PORTUGUESE:
		case MINECRAFT_LANGUAGE_BRAZILIAN:
			return L"Opcional (vazio = aleatoria)";
		default:
			return L"Optional (leave blank for random)";
		}
	}
}

void UIScene_LaunchMoreOptionsMenu::ModalCompleteSeedCallback(void *lpParam, bool confirmed, const std::wstring &text)
{
	UIScene_LaunchMoreOptionsMenu *pClass = (UIScene_LaunchMoreOptionsMenu *)lpParam;
	if(pClass == NULL) return;
	pClass->m_bIgnoreInput = false;
	if(confirmed)
	{
		pClass->m_editSeed.setLabel(text);
		if(pClass->m_params != NULL)
		{
			pClass->m_params->seed = text;
		}
	}
}
#endif

void UIScene_LaunchMoreOptionsMenu::handlePress(F64 controlId, F64 childId)
{
	if(m_bIgnoreInput) return;

	switch((int)controlId)
	{
	case eControl_EditSeed:
		{
#ifdef _WINDOWS64
			CustomTextInputModal::Params modalParams;
			const wchar_t *seedTitle = app.GetString(IDS_CREATE_NEW_WORLD_SEED);
			modalParams.title = (seedTitle != NULL && seedTitle[0] != 0) ? seedTitle : L"Seed";
			modalParams.initialText = (m_params != NULL) ? m_params->seed : m_editSeed.getLabel();
			modalParams.hintText = GetMoreOptionsSeedHintText();
			modalParams.minLength = 0;
			modalParams.maxLength = 60;
			modalParams.charFilter = &IsAcceptedMoreOptionsSeedChar;
			modalParams.completeFunc = &UIScene_LaunchMoreOptionsMenu::ModalCompleteSeedCallback;
			modalParams.completeParam = this;

			m_bIgnoreInput = true;
			CustomTextInputModal::Open(modalParams);
#else
			m_bIgnoreInput=true;
			InputManager.RequestKeyboard(app.GetString(IDS_CREATE_NEW_WORLD_SEED),m_editSeed.getLabel(),(DWORD)0,60,&UIScene_LaunchMoreOptionsMenu::KeyboardCompleteSeedCallback,this,C_4JInput::EKeyboardMode_Default);
#endif
		}
		break;
	}
}


void UIScene_LaunchMoreOptionsMenu::handleSliderMove(F64 sliderId, F64 currentValue)
{
	int value = (int)currentValue;
	switch((int)sliderId)
	{
	case eControl_WorldSize:
#ifdef _LARGE_WORLDS
		m_sliderWorldSize.handleSliderMove(value);
		m_params->worldSize = value;
		m_sliderWorldSize.setLabel(app.GetString(m_iWorldSizeTitleA[value]));
#endif
		break;
	}
}
