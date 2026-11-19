#include "stdafx.h"
#include "UI.h"
#include "UIScene_AbstractContainerMenu.h"

#include "..\..\..\Minecraft.World\net.minecraft.world.inventory.h"
#include "..\..\..\Minecraft.World\net.minecraft.world.item.h"
#include "..\..\MultiplayerLocalPlayer.h"

#ifdef _WINDOWS64
#include <math.h>

namespace
{
	struct ContainerMouseState
	{
		bool initialized;
		bool mouseControlActive;
		int lastMouseX;
		int lastMouseY;
		UIVec2D lastMoviePos;

		ContainerMouseState()
			: initialized(false)
			, mouseControlActive(false)
			, lastMouseX(0)
			, lastMouseY(0)
		{
			lastMoviePos.x = 0.0f;
			lastMoviePos.y = 0.0f;
		}
	};

	static unordered_map<const UIScene_AbstractContainerMenu *, ContainerMouseState> s_containerMouseStates;

	static bool GetContainerMouseMoviePos(UILayer *parentLayer, int movieWidth, int movieHeight, UIControl &mainPanel, UIVec2D &outMoviePos)
	{
		if(parentLayer == NULL || movieWidth <= 0 || movieHeight <= 0)
		{
			return false;
		}

		S32 renderW = 0;
		S32 renderH = 0;
		parentLayer->getRenderDimensions(renderW, renderH);
		if(renderW <= 0 || renderH <= 0)
		{
			return false;
		}

		const S32 screenW = (S32)ui.getScreenWidth();
		const S32 screenH = (S32)ui.getScreenHeight();
		S32 originX = 0;
		S32 originY = 0;

		switch(parentLayer->getViewport())
		{
		case C4JRender::VIEWPORT_TYPE_SPLIT_TOP:
			originX = screenW / 4;
			break;
		case C4JRender::VIEWPORT_TYPE_SPLIT_BOTTOM:
			originX = screenW / 4;
			originY = screenH / 2;
			break;
		case C4JRender::VIEWPORT_TYPE_SPLIT_LEFT:
			originY = screenH / 4;
			break;
		case C4JRender::VIEWPORT_TYPE_SPLIT_RIGHT:
			originX = screenW / 2;
			originY = screenH / 4;
			break;
		case C4JRender::VIEWPORT_TYPE_QUADRANT_TOP_LEFT:
			break;
		case C4JRender::VIEWPORT_TYPE_QUADRANT_TOP_RIGHT:
			originX = screenW / 2;
			break;
		case C4JRender::VIEWPORT_TYPE_QUADRANT_BOTTOM_LEFT:
			originY = screenH / 2;
			break;
		case C4JRender::VIEWPORT_TYPE_QUADRANT_BOTTOM_RIGHT:
			originX = screenW / 2;
			originY = screenH / 2;
			break;
		case C4JRender::VIEWPORT_TYPE_FULLSCREEN:
		default:
			break;
		}

		mainPanel.UpdateControl();
		const float localX = (float)((S32)Mouse::getX() - originX);
		const float localY = (float)((S32)Mouse::getY() - originY);
		outMoviePos.x = localX * ((float)movieWidth / (float)renderW) - (float)mainPanel.getXPos();
		outMoviePos.y = localY * ((float)movieHeight / (float)renderH) - (float)mainPanel.getYPos();
		return true;
	}

	static bool SyncContainerPointerFromWindowsMouse(
		UIScene_AbstractContainerMenu *scene,
		int iPad,
		UILayer *parentLayer,
		int movieWidth,
		int movieHeight,
		UIControl &mainPanel,
		float minX,
		float maxX,
		float minY,
		float maxY,
		float offsetX,
		float offsetY,
		UIVec2D &pointerPos,
		bool allowButtonActivation,
		bool &outResetTapState)
	{
		outResetTapState = false;
		if(iPad != ProfileManager.GetPrimaryPad())
		{
			return false;
		}

		ContainerMouseState &state = s_containerMouseStates[scene];
		if(Mouse::isGrabbed())
		{
			state.initialized = false;
			state.mouseControlActive = false;
			return false;
		}

		const int mouseX = Mouse::getX();
		const int mouseY = Mouse::getY();
		const bool buttonClicked = allowButtonActivation && (Mouse::isButtonPressed(0) || Mouse::isButtonPressed(1));

		if(!state.initialized)
		{
			state.initialized = true;
			state.lastMouseX = mouseX;
			state.lastMouseY = mouseY;
			state.mouseControlActive = buttonClicked;
			if(!state.mouseControlActive)
			{
				return false;
			}
		}

		const float sens = (float)app.GetGameSettings(iPad, eGameSetting_Sensitivity_InMenu) / 100.0f;
		const float stickX = InputManager.GetJoypadStick_LX(iPad, false) * sens;
		const float stickY = InputManager.GetJoypadStick_LY(iPad, false) * sens;
		if(fabsf(stickX) >= 0.01f || fabsf(stickY) >= 0.01f)
		{
			state.mouseControlActive = false;
			state.lastMouseX = mouseX;
			state.lastMouseY = mouseY;
			return false;
		}

		if(mouseX != state.lastMouseX || mouseY != state.lastMouseY || buttonClicked)
		{
			state.lastMouseX = mouseX;
			state.lastMouseY = mouseY;
			state.mouseControlActive = true;
		}

		if(!state.mouseControlActive)
		{
			return false;
		}

		UIVec2D moviePos;
		if(!GetContainerMouseMoviePos(parentLayer, movieWidth, movieHeight, mainPanel, moviePos))
		{
			return false;
		}

		const float clampMinX = minX - offsetX;
		const float clampMaxX = maxX - offsetX;
		const float clampMinY = minY - offsetY;
		const float clampMaxY = maxY - offsetY;
		if(moviePos.x < clampMinX) moviePos.x = clampMinX;
		else if(moviePos.x > clampMaxX) moviePos.x = clampMaxX;
		if(moviePos.y < clampMinY) moviePos.y = clampMinY;
		else if(moviePos.y > clampMaxY) moviePos.y = clampMaxY;

		moviePos.x = floorf(moviePos.x);
		moviePos.y = floorf(moviePos.y);

		pointerPos = moviePos;
		state.lastMoviePos = moviePos;
		outResetTapState = true;
		return true;
	}
}
#endif

UIScene_AbstractContainerMenu::UIScene_AbstractContainerMenu(int iPad, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	m_focusSection = eSectionNone;
	// in this scene, we override the press sound with our own for crafting success or fail
	ui.OverrideSFX(m_iPad,ACTION_MENU_A,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_X,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_Y,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,true);

	m_bIgnoreInput=false;
}

UIScene_AbstractContainerMenu::~UIScene_AbstractContainerMenu()
{
#ifdef _WINDOWS64
	s_containerMouseStates.erase(this);
#endif
	app.DebugPrintf("UIScene_AbstractContainerMenu::~UIScene_AbstractContainerMenu\n");
}

void UIScene_AbstractContainerMenu::handleDestroy()
{
	app.DebugPrintf("UIScene_AbstractContainerMenu::handleDestroy\n");
	Minecraft *pMinecraft = Minecraft::GetInstance();
	if( pMinecraft->localgameModes[m_iPad] != NULL )
	{
		TutorialMode *gameMode = (TutorialMode *)pMinecraft->localgameModes[m_iPad];
		if(gameMode != NULL) gameMode->getTutorial()->changeTutorialState(m_previousTutorialState);
	}

	// 4J Stu - Fix for #11302 - TCR 001: Network Connectivity: Host crashed after being killed by the client while accessing a chest during burst packet loss.
	// We need to make sure that we call closeContainer() anytime this menu is closed, even if it is forced to close by some other reason (like the player dying)	
	if(pMinecraft->localplayers[m_iPad] != NULL) pMinecraft->localplayers[m_iPad]->closeContainer();

	ui.OverrideSFX(m_iPad,ACTION_MENU_A,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_X,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_Y,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,false);
#ifdef _WINDOWS64
	s_containerMouseStates.erase(this);
#endif
}

void UIScene_AbstractContainerMenu::InitDataAssociations(int iPad, AbstractContainerMenu *menu, int startIndex)
{
}

void UIScene_AbstractContainerMenu::PlatformInitialize(int iPad, int startIndex)
{

	m_labelInventory.init( app.GetString(IDS_INVENTORY) );

	if(startIndex >= 0)
	{
		m_slotListInventory.addSlots(startIndex, 27);
		m_slotListHotbar.addSlots(startIndex + 27, 9);
	}

	// Determine min and max extents for pointer, it needs to be able to move off the container to drop items.
	float fPanelWidth, fPanelHeight;
	float fPanelX, fPanelY;
	float fPointerWidth, fPointerHeight;

	// We may have varying depths of controls here, so base off the pointers parent
#if TO_BE_IMPLEMENTED
	HXUIOBJ parent;
	XuiElementGetBounds( m_pointerControl->m_hObj, &fPointerWidth, &fPointerHeight );	
#else
	fPointerWidth = 50;
	fPointerHeight = 50;
#endif

	fPanelWidth = m_controlBackgroundPanel.getWidth();
	fPanelHeight = m_controlBackgroundPanel.getHeight();
	fPanelX = m_controlBackgroundPanel.getXPos();
	fPanelY = m_controlBackgroundPanel.getYPos();
	// Get size of pointer
	m_fPointerImageOffsetX = 0; //floor(fPointerWidth/2.0f);
	m_fPointerImageOffsetY = 0; //floor(fPointerHeight/2.0f);
	
	m_fPanelMinX = fPanelX;
	m_fPanelMaxX = fPanelX + fPanelWidth;
	m_fPanelMinY = fPanelY;
	m_fPanelMaxY = fPanelY + fPanelHeight;


	// 4J-PB - need to limit this in splitscreen
	if(app.GetLocalPlayerCount()>1)
	{
		// don't let the pointer go into someone's screen
		m_fPointerMinY = floor(fPointerHeight/2.0f);
	}
	else
	{
		m_fPointerMinY = fPanelY -fPointerHeight;
	}
	m_fPointerMinX = fPanelX - fPointerWidth;
	m_fPointerMaxX = m_fPanelMaxX + fPointerWidth;
	m_fPointerMaxY = m_fPanelMaxY + (fPointerHeight/2);

// 	m_hPointerText=NULL;
// 	m_hPointerTextBkg=NULL;

	// Put the pointer over first item in use row to start with.
	UIVec2D itemPos;
	UIVec2D itemSize;
	GetItemScreenData( m_eCurrSection, 0, &( itemPos ), &( itemSize ) );

	UIVec2D sectionPos;
	GetPositionOfSection( m_eCurrSection, &( sectionPos ) );

	UIVec2D vPointerPos = sectionPos;
	vPointerPos += itemPos;
	vPointerPos.x += ( itemSize.x / 2.0f );
	vPointerPos.y += ( itemSize.y / 2.0f );

	vPointerPos.x -= m_fPointerImageOffsetX;
	vPointerPos.y -= m_fPointerImageOffsetY;

	//m_pointerControl->SetPosition( &vPointerPos );
	m_pointerPos = vPointerPos;

	IggyEvent mouseEvent;
	S32 width, height;
	m_parentLayer->getRenderDimensions(width, height);
	S32 x = m_pointerPos.x*((float)width/m_movieWidth);
	S32 y = m_pointerPos.y*((float)height/m_movieHeight); 
	IggyMakeEventMouseMove( &mouseEvent, x, y);

	IggyEventResult result;
	IggyPlayerDispatchEventRS ( getMovie() , &mouseEvent , &result );

#ifdef _WINDOWS64
	s_containerMouseStates.erase(this);
#endif

#ifdef USE_POINTER_ACCEL	
	m_fPointerVelX = 0.0f;
	m_fPointerVelY = 0.0f;
	m_fPointerAccelX = 0.0f;
	m_fPointerAccelY = 0.0f;
#endif
}

void UIScene_AbstractContainerMenu::tick()
{
	UIScene::tick();

#ifdef _WINDOWS64
	bool resetTapState = false;
	const bool mouseControlled = SyncContainerPointerFromWindowsMouse(
		this, m_iPad, m_parentLayer, m_movieWidth, m_movieHeight, m_controlMainPanel,
		m_fPointerMinX, m_fPointerMaxX, m_fPointerMinY, m_fPointerMaxY,
		m_fPointerImageOffsetX, m_fPointerImageOffsetY,
		m_pointerPos, false, resetTapState);
	if(resetTapState)
	{
		m_eCurrTapState = eTapStateNoInput;
	}
#endif

	onMouseTick();

#ifdef _WINDOWS64
	if(mouseControlled)
	{
		m_pointerPos = s_containerMouseStates[this].lastMoviePos;
	}
#endif

	IggyEvent mouseEvent;
	S32 width, height;
	m_parentLayer->getRenderDimensions(width, height);
	S32 x = m_pointerPos.x*((float)width/m_movieWidth);
	S32 y = m_pointerPos.y*((float)height/m_movieHeight);
	IggyMakeEventMouseMove( &mouseEvent, x, y);

	// 4J Stu - This seems to be broken on Durango, so do it ourself
#ifdef _DURANGO
	//mouseEvent.x = x;
	//mouseEvent.y = y;
#endif

	IggyEventResult result;
	IggyPlayerDispatchEventRS ( getMovie() , &mouseEvent , &result );
}

void UIScene_AbstractContainerMenu::render(S32 width, S32 height, C4JRender::eViewportType viewpBort)
{
	m_cacheSlotRenders = true;

	m_needsCacheRendered = m_needsCacheRendered || m_menu->needsRendered();

	if(m_needsCacheRendered)
	{
		m_expectedCachedSlotCount = 0;
		unsigned int count = m_menu->getSize();
		for(unsigned int i = 0; i < count; ++i)
		{
			if(m_menu->getSlot(i)->hasItem())
			{
				++m_expectedCachedSlotCount;
			}
		}
	}

	UIScene::render(width, height, viewpBort);

	m_needsCacheRendered = false;
}

void UIScene_AbstractContainerMenu::customDraw(IggyCustomDrawCallbackRegion *region)
{
	Minecraft *pMinecraft = Minecraft::GetInstance();
	if(pMinecraft->localplayers[m_iPad] == NULL || pMinecraft->localgameModes[m_iPad] == NULL) return;

	shared_ptr<ItemInstance> item = nullptr;
	if(wcscmp((wchar_t *)region->name,L"pointerIcon")==0)
	{		
		m_cacheSlotRenders = false;
		item = pMinecraft->localplayers[m_iPad]->inventory->getCarried();
	}
	else
	{
		int slotId = -1;
		swscanf((wchar_t*)region->name,L"slot_%d",&slotId);
		if (slotId == -1)
		{
			app.DebugPrintf("This is not the control we are looking for\n");
		}
		else
		{			
			m_cacheSlotRenders = true;
			Slot *slot = m_menu->getSlot(slotId);
			item = slot->getItem();
		}
	}

	if(item != NULL) customDrawSlotControl(region,m_iPad,item,1.0f,item->isFoil(),true);
}

void UIScene_AbstractContainerMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	if(m_bIgnoreInput) return;

	//app.DebugPrintf("UIScene_InventoryMenu handling input for pad %d, key %d, down- %s, pressed- %s, released- %s\n", iPad, key, down?"TRUE":"FALSE", pressed?"TRUE":"FALSE", released?"TRUE":"FALSE");
	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	if(pressed)
	{
#ifdef _WINDOWS64
		if(key == ACTION_MENU_LEFT || key == ACTION_MENU_RIGHT || key == ACTION_MENU_UP || key == ACTION_MENU_DOWN)
		{
			ContainerMouseState &state = s_containerMouseStates[this];
			state.mouseControlActive = false;
			state.lastMouseX = Mouse::getX();
			state.lastMouseY = Mouse::getY();
		}
		else if((key == ACTION_MENU_OK || key == ACTION_MENU_X) && !Mouse::isGrabbed() &&
		        (Mouse::isButtonPressed(0) || Mouse::isButtonPressed(1)))
		{
			bool resetTapState = false;
			if(SyncContainerPointerFromWindowsMouse(
				this, m_iPad, m_parentLayer, m_movieWidth, m_movieHeight, m_controlMainPanel,
				m_fPointerMinX, m_fPointerMaxX, m_fPointerMinY, m_fPointerMaxY,
				m_fPointerImageOffsetX, m_fPointerImageOffsetY,
				m_pointerPos, true, resetTapState))
			{
				if(resetTapState)
				{
					m_eCurrTapState = eTapStateNoInput;
				}
				onMouseTick();
				m_pointerPos = s_containerMouseStates[this].lastMoviePos;
			}
		}
#endif
		handled = handleKeyDown(m_iPad, key, repeat);
	}
}

void UIScene_AbstractContainerMenu::SetPointerText(const wstring &description, vector<wstring> &unformattedStrings, bool newSlot)
{
	//app.DebugPrintf("Setting pointer text\n");
	m_cursorPath.setLabel(description,false,newSlot);
}

void UIScene_AbstractContainerMenu::setSectionFocus(ESceneSection eSection, int iPad)
{
	if(m_focusSection != eSectionNone)
	{
		UIControl *currentFocus = getSection(m_focusSection);
		if(currentFocus) currentFocus->setFocus(false);
	}
	UIControl *newFocus = getSection(eSection);
	if(newFocus) newFocus->setFocus(true);
	m_focusSection = eSection;
}

void UIScene_AbstractContainerMenu::setFocusToPointer(int iPad)
{
	if(m_focusSection != eSectionNone)
	{
		UIControl *currentFocus = getSection(m_focusSection);
		if(currentFocus) currentFocus->setFocus(false);
	}
	m_focusSection = eSectionNone;
}

shared_ptr<ItemInstance> UIScene_AbstractContainerMenu::getSlotItem(ESceneSection eSection, int iSlot)
{
	Slot *slot = m_menu->getSlot( getSectionStartOffset(eSection) + iSlot );
	if(slot) return slot->getItem();
	else return nullptr;
}

bool UIScene_AbstractContainerMenu::isSlotEmpty(ESceneSection eSection, int iSlot)
{
	Slot *slot = m_menu->getSlot( getSectionStartOffset(eSection) + iSlot );
	if(slot) return !slot->hasItem();
	else return false;
}

void UIScene_AbstractContainerMenu::adjustPointerForSafeZone()
{
	// Handled by AS
}
