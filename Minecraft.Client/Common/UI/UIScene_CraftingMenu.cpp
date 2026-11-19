#include "stdafx.h"
#include "UI.h"
#include "..\..\Minecraft.h"
#include "..\..\MultiplayerLocalPlayer.h"
#include "..\..\..\Minecraft.World\net.minecraft.world.inventory.h"
#include "UIScene_CraftingMenu.h"

#ifdef __PSVITA__
#define GAME_CRAFTING_TOUCHUPDATE_TIMER_ID 0
#define GAME_CRAFTING_TOUCHUPDATE_TIMER_TIME 100
#endif

UIScene_CraftingMenu::UIScene_CraftingMenu(int iPad, void *_initData, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	m_bIgnoreKeyPresses = false;
#ifdef _WINDOWS64
	memset(m_hSlotRects, 0, sizeof(m_hSlotRects));
	memset(m_vSlotRects, 0, sizeof(m_vSlotRects));
	memset(&m_outputSlotRect, 0, sizeof(m_outputSlotRect));
	m_wasMouseLeftDown = Mouse::isButtonDown(0) || Mouse::isButtonPressed(0);
	m_wasMouseRightDown = Mouse::isButtonDown(1) || Mouse::isButtonPressed(1);
	m_lastMouseX = (S32)Mouse::getX();
	m_lastMouseY = (S32)Mouse::getY();
	m_ignoreCloseTicks = 2;
#endif

	CraftingPanelScreenInput* initData = (CraftingPanelScreenInput*)_initData;
	m_iContainerType=initData->iContainerType;
	m_pPlayer=initData->player;
	m_bSplitscreen=initData->bSplitscreen;

	// Setup all the Iggy references we need for this scene
	initialiseMovie();

	for(unsigned int i = 0; i < 4; ++i)	m_labelIngredientsDesc[i].init(L"");
	m_labelDescription.init(L"");
	m_labelGroupName.init(L"");
	m_labelItemName.init(L"");
	m_labelInventory.init( app.GetString(IDS_INVENTORY) );
	m_labelIngredients.init( app.GetString(IDS_INGREDIENTS) );

	if(m_iContainerType==RECIPE_TYPE_2x2)
	{
		m_menu = m_pPlayer->inventoryMenu;
		m_iMenuInventoryStart = InventoryMenu::INV_SLOT_START;
		m_iMenuHotBarStart = InventoryMenu::USE_ROW_SLOT_START;
	}
	else
	{
		CraftingMenu *menu = new CraftingMenu(m_pPlayer->inventory, m_pPlayer->level, initData->x, initData->y, initData->z);
		Minecraft::GetInstance()->localplayers[m_iPad]->containerMenu = menu;

		m_menu = menu;
		m_iMenuInventoryStart = CraftingMenu::INV_SLOT_START;
		m_iMenuHotBarStart = CraftingMenu::USE_ROW_SLOT_START;
	}
	m_slotListInventory.addSlots(CRAFTING_INVENTORY_SLOT_START,CRAFTING_INVENTORY_SLOT_END - CRAFTING_INVENTORY_SLOT_START);
	m_slotListHotBar.addSlots(CRAFTING_HOTBAR_SLOT_START, CRAFTING_HOTBAR_SLOT_END - CRAFTING_HOTBAR_SLOT_START);

#if TO_BE_IMPLEMENTED
	// if we are in splitscreen, then we need to figure out if we want to move this scene
	if(m_bSplitscreen)
	{
		app.AdjustSplitscreenScene(m_hObj,&m_OriginalPosition,m_iPad);	
	}

	XuiElementSetShow(m_hGrid,TRUE);
	XuiElementSetShow(m_hPanel,TRUE);
#endif

	if(m_iContainerType==RECIPE_TYPE_3x3)
	{
		m_iIngredientsMaxSlotC = m_iIngredients3x3SlotC;
		m_pGroupA=(Recipy::_eGroupType *)&m_GroupTypeMapping9GridA;
		m_pGroupTabA=(_eGroupTab *)&m_GroupTabBkgMapping3x3A;
		m_iCraftablesMaxHSlotC=m_iMaxHSlot3x3C;
	}
	else
	{
		m_iIngredientsMaxSlotC = m_iIngredients2x2SlotC;
		m_pGroupA=(Recipy::_eGroupType *)&m_GroupTypeMapping4GridA;
		m_pGroupTabA=(_eGroupTab *)&m_GroupTabBkgMapping2x2A;
		m_iCraftablesMaxHSlotC=m_iMaxHSlot2x2C;
	}

#if TO_BE_IMPLEMENTED


	// display the first group tab 
	m_hTabGroupA[m_iGroupIndex].SetShow(TRUE);

	// store the slot 0 position
	m_pHSlotsBrushImageControl[0]->GetPosition(&m_vSlot0Pos);
	m_pHSlotsBrushImageControl[1]->GetPosition(&vec);
	m_fSlotSize=vec.x-m_vSlot0Pos.x;

	// store the slot 0 highlight position
	m_hHighlight.GetPosition(&m_vSlot0HighlightPos);
	// Store the V slot position
	m_hScrollBar2.GetPosition(&m_vSlot0V2ScrollPos);
	m_hScrollBar3.GetPosition(&m_vSlot0V3ScrollPos);

	// get the position of the slot from the xui, and apply any offset needed
	for(int i=0;i<m_iCraftablesMaxHSlotC;i++)
	{
		m_pHSlotsBrushImageControl[i]->SetShow(FALSE);
	}

	XuiElementSetShow(m_hGridInventory,FALSE);

	m_hScrollBar2.SetShow(FALSE);
	m_hScrollBar3.SetShow(FALSE);

#endif

	app.SetRichPresenceContext(m_iPad,CONTEXT_GAME_STATE_CRAFTING);
	setGroupText(GetGroupNameText(m_pGroupA[m_iGroupIndex]));

	// Update the tutorial state
	Minecraft *pMinecraft = Minecraft::GetInstance();

	if( pMinecraft->localgameModes[m_iPad] != NULL )
	{
		TutorialMode *gameMode = (TutorialMode *)pMinecraft->localgameModes[m_iPad];
		m_previousTutorialState = gameMode->getTutorial()->getCurrentState();
		if(m_iContainerType==RECIPE_TYPE_2x2)
		{
			gameMode->getTutorial()->changeTutorialState(e_Tutorial_State_2x2Crafting_Menu, this);
		}
		else
		{
			gameMode->getTutorial()->changeTutorialState(e_Tutorial_State_3x3Crafting_Menu, this);
		}
	}

#ifdef _TO_BE_IMPLEMENTED
	XuiSetTimer(m_hObj,IGNORE_KEYPRESS_TIMERID,IGNORE_KEYPRESS_TIME);
#endif

	for(unsigned int i = 0; i < 4; ++i)
	{
		m_slotListIngredients[i].addSlot(CRAFTING_INGREDIENTS_DESCRIPTION_START + i);
	}
	m_slotListCraftingOutput.addSlot(CRAFTING_OUTPUT_SLOT_START);
	m_slotListIngredientsLayout.addSlots(CRAFTING_INGREDIENTS_LAYOUT_START, m_iIngredientsMaxSlotC);

	// 3 Slot vertical scroll
	m_slotListCrafting3VSlots[0].addSlot(CRAFTING_V_SLOT_START + 0);
	m_slotListCrafting3VSlots[1].addSlot(CRAFTING_V_SLOT_START + 1);
	m_slotListCrafting3VSlots[2].addSlot(CRAFTING_V_SLOT_START + 2);

	// 2 Slot vertical scroll
	// 2 slot scroll has swapped order
	m_slotListCrafting2VSlots[0].addSlot(CRAFTING_V_SLOT_START + 1);
	m_slotListCrafting2VSlots[1].addSlot(CRAFTING_V_SLOT_START + 0);

	// 1 Slot scroll (for 480 mainly)
	m_slotListCrafting1VSlots.addSlot(CRAFTING_V_SLOT_START);

	m_slotListCraftingHSlots.addSlots(CRAFTING_H_SLOT_START,m_iCraftablesMaxHSlotC);

	// Check which recipes are available with the resources we have
	CheckRecipesAvailable();
	// reset the vertical slots
	iVSlotIndexA[0]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
	iVSlotIndexA[1]=0;
	iVSlotIndexA[2]=1;
	UpdateVerticalSlots();
	UpdateHighlight();

	if(initData) delete initData;

	// in this scene, we override the press sound with our own for crafting success or fail
	ui.OverrideSFX(m_iPad,ACTION_MENU_A,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,true);

	// 4J-PB - Must be after the CanBeMade list has been set up with CheckRecipesAvailable
	UpdateTooltips();

#ifdef __PSVITA__
	// initialise vita touch controls with ids
	for(unsigned int i = 0; i < ETouchInput_Count; ++i)
	{
		m_TouchInput[i].init(i);
	}
	ui.TouchBoxRebuild(this);
#endif
}

void UIScene_CraftingMenu::handleDestroy()
{
	Minecraft *pMinecraft = Minecraft::GetInstance();

	if( pMinecraft->localgameModes[m_iPad] != NULL )
	{
		TutorialMode *gameMode = (TutorialMode *)pMinecraft->localgameModes[m_iPad];
		if(gameMode != NULL) gameMode->getTutorial()->changeTutorialState(m_previousTutorialState);
	}

	// We need to make sure that we call closeContainer() anytime this menu is closed, even if it is forced to close by some other reason (like the player dying)	
	if(Minecraft::GetInstance()->localplayers[m_iPad] != NULL) Minecraft::GetInstance()->localplayers[m_iPad]->closeContainer();

	ui.OverrideSFX(m_iPad,ACTION_MENU_A,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,false);
}

EUIScene UIScene_CraftingMenu::getSceneType()
{
	if(m_iContainerType==RECIPE_TYPE_3x3)
	{
		return eUIScene_Crafting3x3Menu;
	}
	else
	{
		return eUIScene_Crafting2x2Menu;
	}
}

wstring UIScene_CraftingMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		m_bSplitscreen = true;
		if(m_iContainerType==RECIPE_TYPE_3x3)
		{
			return L"Crafting3x3MenuSplit";
		}
		else
		{
			return L"Crafting2x2MenuSplit";
		}
	}
	else
	{
		if(m_iContainerType==RECIPE_TYPE_3x3)
		{
			return L"Crafting3x3Menu";
		}
		else
		{
			return L"Crafting2x2Menu";
		}
	}
}

#if defined(__PSVITA__) || defined(_WINDOWS64)
UIControl* UIScene_CraftingMenu::GetMainPanel()
{
	return &m_controlMainPanel;
}
#endif

void UIScene_CraftingMenu::tick()
{
	UIScene::tick();

#ifdef _WINDOWS64
	if (!hasFocus(m_iPad) || m_bIgnoreKeyPresses)
	{
		m_wasMouseLeftDown = Mouse::isButtonDown(0) || Mouse::isButtonPressed(0);
		m_wasMouseRightDown = Mouse::isButtonDown(1) || Mouse::isButtonPressed(1);
		return;
	}

	if (m_ignoreCloseTicks > 0)
	{
		--m_ignoreCloseTicks;
	}
	else if (Keyboard::isKeyPressed(Keyboard::KEY_C))
	{
		handleKeyDown(m_iPad, ACTION_MENU_B, false);
		return;
	}
	else if (Keyboard::isKeyPressed(Keyboard::KEY_F))
	{
		m_bFilterCraftableOnly = !m_bFilterCraftableOnly;
		m_iCurrentSlotHIndex = 0;
		m_iCurrentSlotVIndex = 1;
		CheckRecipesAvailable();
		iVSlotIndexA[0] = (CanBeMadeA[m_iCurrentSlotHIndex].iCount > 0) ? (CanBeMadeA[m_iCurrentSlotHIndex].iCount - 1) : 0;
		iVSlotIndexA[1] = 0;
		iVSlotIndexA[2] = (CanBeMadeA[m_iCurrentSlotHIndex].iCount > 1) ? 1 : 0;
		UpdateVerticalSlots();
		UpdateHighlight();
		ui.PlayUISFX(eSFX_Focus);
		return;
	}

	if (Mouse::isGrabbed()) return;

	/* Mouse wheel: scroll vertical sub-recipes if >1 variant, else scroll horizontal recipes */
	const int wheel = Mouse::getWheelDelta();
	if (wheel != 0)
	{
		if (CanBeMadeA[m_iCurrentSlotHIndex].iCount > 1)
		{
			handleKeyDown(m_iPad, (wheel > 0) ? ACTION_MENU_UP : ACTION_MENU_DOWN, false);
		}
		else
		{
			handleKeyDown(m_iPad, (wheel > 0) ? ACTION_MENU_LEFT : ACTION_MENU_RIGHT, false);
		}
	}

	const bool isLeftDown = Mouse::isButtonDown(0) || Mouse::isButtonPressed(0);
	const bool leftClicked = isLeftDown && !m_wasMouseLeftDown;
	m_wasMouseLeftDown = isLeftDown;

	const bool isRightDown = Mouse::isButtonDown(1) || Mouse::isButtonPressed(1);
	const bool rightClicked = isRightDown && !m_wasMouseRightDown;
	m_wasMouseRightDown = isRightDown;

	if (rightClicked)
	{
		handleKeyDown(m_iPad, ACTION_MENU_Y, false);
		return;
	}

	const S32 curMouseX = (S32)Mouse::getX();
	const S32 curMouseY = (S32)Mouse::getY();
	const bool mouseMoved = (curMouseX != m_lastMouseX || curMouseY != m_lastMouseY);
	m_lastMouseX = curMouseX;
	m_lastMouseY = curMouseY;

	if (!mouseMoved && !leftClicked) return;

	const float screenW = ui.getScreenWidth();
	const float screenH = ui.getScreenHeight();
	const S32 sceneW = getRenderWidth();
	const S32 sceneH = getRenderHeight();
	if (screenW <= 0.0f || screenH <= 0.0f || sceneW <= 0 || sceneH <= 0) return;

	const float localX = ((float)curMouseX * (float)sceneW) / screenW;
	const float localY = ((float)curMouseY * (float)sceneH) / screenH;

	/* Derive exact horizontal row bounds from pixel-accurate customDraw slot rects */
	float rowLeft = 0.0f, rowRight = 0.0f, rowTop = 0.0f, rowBottom = 0.0f;
	float slotW = 0.0f, slotH = 0.0f;
	int firstValidH = -1, lastValidH = -1;
	for (int i = 0; i < m_iCraftablesMaxHSlotC; ++i)
	{
		if (m_hSlotRects[i].valid && m_hSlotRects[i].x1 > m_hSlotRects[i].x0)
		{
			if (firstValidH < 0) firstValidH = i;
			lastValidH = i;
			slotW = m_hSlotRects[i].x1 - m_hSlotRects[i].x0;
			slotH = m_hSlotRects[i].y1 - m_hSlotRects[i].y0;
			rowTop = m_hSlotRects[i].y0;
			rowBottom = m_hSlotRects[i].y1;
		}
	}
	if (firstValidH >= 0 && lastValidH > firstValidH)
	{
		const float stepX = (m_hSlotRects[lastValidH].x0 - m_hSlotRects[firstValidH].x0) / (float)(lastValidH - firstValidH);
		rowLeft  = m_hSlotRects[firstValidH].x0 - (float)firstValidH * stepX;
		rowRight = rowLeft + (float)(m_iCraftablesMaxHSlotC - 1) * stepX + slotW;
	}
	else if (m_vSlotRects[1].valid && m_vSlotRects[1].x1 > m_vSlotRects[1].x0)
	{
		slotW = m_vSlotRects[1].x1 - m_vSlotRects[1].x0;
		slotH = m_vSlotRects[1].y1 - m_vSlotRects[1].y0;
		rowTop = m_vSlotRects[1].y0;
		rowBottom = m_vSlotRects[1].y1;
		const float stepX = slotW * 1.12f;
		rowLeft  = m_vSlotRects[1].x0 - (float)m_iCurrentSlotHIndex * stepX;
		rowRight = rowLeft + (float)(m_iCraftablesMaxHSlotC - 1) * stepX + slotW;
	}

	const bool haveRow = (rowRight > rowLeft && slotH > 1.0f && m_iCraftablesMaxHSlotC > 0);
	const int tabCount = (m_iContainerType == RECIPE_TYPE_3x3) ? m_iMaxGroup3x3 : m_iMaxGroup2x2;

	/* 1. Top category tabs (6 tabs in 2x2, 7 tabs in 3x3) */
	if (leftClicked && haveRow && tabCount > 0)
	{
		const float tabsLeft   = rowLeft  - slotW * 0.45f;
		const float tabsRight  = rowRight + slotW * 0.45f;
		const float tabsBottom = rowTop   - slotH * 1.48f;
		const float tabsTop    = tabsBottom - slotH * 1.65f;
		if (localX >= tabsLeft && localX <= tabsRight && localY >= tabsTop && localY <= tabsBottom)
		{
			int tabIdx = (int)(((localX - tabsLeft) * (float)tabCount) / (tabsRight - tabsLeft));
			if (tabIdx < 0) tabIdx = 0;
			if (tabIdx >= tabCount) tabIdx = tabCount - 1;
			if (tabIdx != m_iGroupIndex)
			{
				showTabHighlight(m_iGroupIndex, false);
				m_iGroupIndex = tabIdx;
				showTabHighlight(m_iGroupIndex, true);
				m_iCurrentSlotHIndex = 0;
				m_iCurrentSlotVIndex = 1;
				CheckRecipesAvailable();
				iVSlotIndexA[0] = CanBeMadeA[m_iCurrentSlotHIndex].iCount - 1;
				iVSlotIndexA[1] = 0;
				iVSlotIndexA[2] = 1;
				ui.PlayUISFX(eSFX_Focus);
				UpdateVerticalSlots();
				UpdateHighlight();
				setGroupText(GetGroupNameText(m_pGroupA[m_iGroupIndex]));
			}
			return;
		}
	}

	/* 2. Vertical selector slots & up/down arrows (top=UP, center=CRAFT, bottom=DOWN) */
	if (leftClicked && CanBeMadeA[m_iCurrentSlotHIndex].iCount > 1)
	{
		if (m_vSlotRects[0].valid)
		{
			const float vx0 = m_vSlotRects[0].x0 - 6.0f;
			const float vx1 = m_vSlotRects[0].x1 + 6.0f;
			const float vy0 = m_vSlotRects[0].y0 - slotH * 0.65f; /* includes up arrow */
			const float vy1 = m_vSlotRects[0].y1 + 4.0f;
			if (localX >= vx0 && localX <= vx1 && localY >= vy0 && localY <= vy1)
			{
				handleKeyDown(m_iPad, ACTION_MENU_UP, false);
				return;
			}
		}
		if (m_vSlotRects[2].valid)
		{
			const float vx0 = m_vSlotRects[2].x0 - 6.0f;
			const float vx1 = m_vSlotRects[2].x1 + 6.0f;
			const float vy0 = m_vSlotRects[2].y0 - 4.0f;
			const float vy1 = m_vSlotRects[2].y1 + slotH * 0.65f; /* includes down arrow */
			if (localX >= vx0 && localX <= vx1 && localY >= vy0 && localY <= vy1)
			{
				handleKeyDown(m_iPad, ACTION_MENU_DOWN, false);
				return;
			}
		}
	}

	/* 3. Center vertical slot (selected recipe) click -> craft */
	if (leftClicked && m_vSlotRects[1].valid)
	{
		if (localX >= (m_vSlotRects[1].x0 - 4.0f) && localX <= (m_vSlotRects[1].x1 + 4.0f) &&
			localY >= (m_vSlotRects[1].y0 - 4.0f) && localY <= (m_vSlotRects[1].y1 + 4.0f))
		{
			handleKeyDown(m_iPad, ACTION_MENU_A, false);
			return;
		}
	}

	/* 4. Horizontal recipe row (exact slot rects; hover selects, click selects or crafts) */
	if (haveRow && localX >= (rowLeft - 4.0f) && localX <= (rowRight + 4.0f) &&
		localY >= (rowTop - 4.0f) && localY <= (rowBottom + 4.0f))
	{
		int hitSlot = -1;
		for (int i = 0; i < m_iCraftablesMaxHSlotC; ++i)
		{
			if (m_hSlotRects[i].valid &&
				localX >= (m_hSlotRects[i].x0 - 4.0f) && localX <= (m_hSlotRects[i].x1 + 4.0f) &&
				localY >= (m_hSlotRects[i].y0 - 4.0f) && localY <= (m_hSlotRects[i].y1 + 4.0f))
			{
				hitSlot = i;
				break;
			}
		}
		if (hitSlot < 0)
		{
			hitSlot = (int)(((localX - rowLeft) * (float)m_iCraftablesMaxHSlotC) / (rowRight - rowLeft));
			if (hitSlot < 0) hitSlot = 0;
			if (hitSlot >= m_iCraftablesMaxHSlotC) hitSlot = m_iCraftablesMaxHSlotC - 1;
		}

		if (hitSlot != m_iCurrentSlotHIndex)
		{
			int iOldHSlot = m_iCurrentSlotHIndex;
			m_iCurrentSlotHIndex = hitSlot;
			m_iCurrentSlotVIndex = 1;
			iVSlotIndexA[0] = CanBeMadeA[m_iCurrentSlotHIndex].iCount - 1;
			iVSlotIndexA[1] = 0;
			iVSlotIndexA[2] = 1;
			UpdateVerticalSlots();
			UpdateHighlight();
			if (CanBeMadeA[iOldHSlot].iCount > 0)
			{
				setShowCraftHSlot(iOldHSlot, true);
			}
			ui.PlayUISFX(eSFX_Focus);
		}
		else if (leftClicked)
		{
			handleKeyDown(m_iPad, ACTION_MENU_A, false);
		}
		return;
	}

	/* 5. Craft Output preview slot click -> craft */
	if (leftClicked && m_outputSlotRect.valid)
	{
		if (localX >= (m_outputSlotRect.x0 - 6.0f) && localX <= (m_outputSlotRect.x1 + 6.0f) &&
			localY >= (m_outputSlotRect.y0 - 6.0f) && localY <= (m_outputSlotRect.y1 + 6.0f))
		{
			handleKeyDown(m_iPad, ACTION_MENU_A, false);
			return;
		}
	}
#endif
}

#ifdef __PSVITA__

void UIScene_CraftingMenu::handleTouchInput(unsigned int iPad, S32 x, S32 y, int iId, bool bPressed, bool bRepeat, bool bReleased)
{
	// perform action on release
	if(bPressed)
	{
		if(iId == ETouchInput_CraftingHSlots)
		{
			m_iCraftingSlotTouchStartY = y;
		}
	}
	else if(bRepeat)
	{
		if(iId == ETouchInput_CraftingHSlots)
		{
			if(y >= m_iCraftingSlotTouchStartY + m_TouchInput[ETouchInput_CraftingHSlots].getHeight())			// scroll list down
			{
				if(iVSlotIndexA[1]==(CanBeMadeA[m_iCurrentSlotHIndex].iCount-1))
				{
					iVSlotIndexA[1]=0;
				}
				else
				{
					iVSlotIndexA[1]++;
				}
				ui.PlayUISFX(eSFX_Focus);

				UpdateVerticalSlots();
				UpdateHighlight();

				m_iCraftingSlotTouchStartY = y;
			}
			else if(y <= m_iCraftingSlotTouchStartY - m_TouchInput[ETouchInput_CraftingHSlots].getHeight())		// scroll list up
			{
				if(iVSlotIndexA[1]==0)
				{
					iVSlotIndexA[1]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
				}
				else
				{
					iVSlotIndexA[1]--;
				}		
				ui.PlayUISFX(eSFX_Focus);

				UpdateVerticalSlots();
				UpdateHighlight();

				m_iCraftingSlotTouchStartY = y;
			}
		}
	}
	else if(bReleased)
	{
		if(iId >= ETouchInput_TouchPanel_0 && iId <= ETouchInput_TouchPanel_6)		// Touch Change Group
		{
			m_iGroupIndex = iId;
			// turn on the new group
			showTabHighlight(m_iGroupIndex,true);

			m_iCurrentSlotHIndex=0;
			m_iCurrentSlotVIndex=1;
			CheckRecipesAvailable();
			// reset the vertical slots
			iVSlotIndexA[0]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
			iVSlotIndexA[1]=0;
			iVSlotIndexA[2]=1;
			ui.PlayUISFX(eSFX_Focus);
			UpdateVerticalSlots();
			UpdateHighlight();
			setGroupText(GetGroupNameText(m_pGroupA[m_iGroupIndex]));
		}
		else if(iId == ETouchInput_CraftingHSlots)									// Touch Change Slot
		{
			int iMaxHSlots = 0;
			if(m_iContainerType==RECIPE_TYPE_3x3)
			{
				iMaxHSlots = m_iMaxHSlot3x3C;
			}
			else
			{
				iMaxHSlots = m_iMaxHSlot2x2C;
			}

			int iNewSlot = (x - m_TouchInput[ETouchInput_CraftingHSlots].getXPos() - m_controlMainPanel.getXPos()) / m_TouchInput[ETouchInput_CraftingHSlots].getHeight();

			int iOldHSlot=m_iCurrentSlotHIndex;

			m_iCurrentSlotHIndex = iNewSlot;
			if(m_iCurrentSlotHIndex>=m_iCraftablesMaxHSlotC) m_iCurrentSlotHIndex=0;
			m_iCurrentSlotVIndex=1;
			// clear the indices
			iVSlotIndexA[0]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
			iVSlotIndexA[1]=0;
			iVSlotIndexA[2]=1;	

			UpdateVerticalSlots();
			UpdateHighlight();
			// re-enable the old hslot
			if(CanBeMadeA[iOldHSlot].iCount>0)
			{
				setShowCraftHSlot(iOldHSlot,true);
			}
			ui.PlayUISFX(eSFX_Focus);
		}
	}
}

void UIScene_CraftingMenu::handleTouchBoxRebuild()
{
	addTimer(GAME_CRAFTING_TOUCHUPDATE_TIMER_ID,GAME_CRAFTING_TOUCHUPDATE_TIMER_TIME);
}

void UIScene_CraftingMenu::handleTimerComplete(int id)
{
	if(id == GAME_CRAFTING_TOUCHUPDATE_TIMER_ID)
	{
		// we cannot rebuild touch boxes in an iggy callback because it requires further iggy calls
		GetMainPanel()->UpdateControl();
		ui.TouchBoxRebuild(this);
		killTimer(GAME_CRAFTING_TOUCHUPDATE_TIMER_ID);
	}
}
#endif

void UIScene_CraftingMenu::handleReload()
{
	m_slotListInventory.addSlots(CRAFTING_INVENTORY_SLOT_START,CRAFTING_INVENTORY_SLOT_END - CRAFTING_INVENTORY_SLOT_START);
	m_slotListHotBar.addSlots(CRAFTING_HOTBAR_SLOT_START, CRAFTING_HOTBAR_SLOT_END - CRAFTING_HOTBAR_SLOT_START);

	for(unsigned int i = 0; i < 4; ++i)
	{
		m_slotListIngredients[i].addSlot(CRAFTING_INGREDIENTS_DESCRIPTION_START + i);
	}
	m_slotListCraftingOutput.addSlot(CRAFTING_OUTPUT_SLOT_START);
	m_slotListIngredientsLayout.addSlots(CRAFTING_INGREDIENTS_LAYOUT_START, m_iIngredientsMaxSlotC);

	// 3 Slot vertical scroll
	m_slotListCrafting3VSlots[0].addSlot(CRAFTING_V_SLOT_START + 0);
	m_slotListCrafting3VSlots[1].addSlot(CRAFTING_V_SLOT_START + 1);
	m_slotListCrafting3VSlots[2].addSlot(CRAFTING_V_SLOT_START + 2);

	// 2 Slot vertical scroll
	// 2 slot scroll has swapped order
	m_slotListCrafting2VSlots[0].addSlot(CRAFTING_V_SLOT_START + 1);
	m_slotListCrafting2VSlots[1].addSlot(CRAFTING_V_SLOT_START + 0);

	// 1 Slot scroll (for 480 mainly)
	m_slotListCrafting1VSlots.addSlot(CRAFTING_V_SLOT_START);

	m_slotListCraftingHSlots.addSlots(CRAFTING_H_SLOT_START,m_iCraftablesMaxHSlotC);

	app.DebugPrintf(app.USER_SR,"Reloading MultiPanel\n");
	int temp = m_iDisplayDescription;
	m_iDisplayDescription = m_iDisplayDescription==0?1:0;
	UpdateMultiPanel();
	m_iDisplayDescription = temp;
	UpdateMultiPanel();

	app.DebugPrintf(app.USER_SR,"Reloading Highlight and scroll\n");

	// reset the vertical slots
	m_iCurrentSlotHIndex = 0;
	m_iCurrentSlotVIndex = 1;
	iVSlotIndexA[0]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
	iVSlotIndexA[1]=0;
	iVSlotIndexA[2]=1;
	UpdateVerticalSlots();
	UpdateHighlight();

	app.DebugPrintf(app.USER_SR,"Reloading tabs\n");
	showTabHighlight(0,false);
	showTabHighlight(m_iGroupIndex,true);
}

void UIScene_CraftingMenu::customDraw(IggyCustomDrawCallbackRegion *region)
{
	Minecraft *pMinecraft = Minecraft::GetInstance();
	if(pMinecraft->localplayers[m_iPad] == NULL || pMinecraft->localgameModes[m_iPad] == NULL) return;

	shared_ptr<ItemInstance> item = nullptr;
	int slotId = -1;
	float alpha = 1.0f;
	bool decorations = true;
	bool inventoryItem = false;
	swscanf((wchar_t*)region->name,L"slot_%d",&slotId);
#ifdef _WINDOWS64
	if (slotId >= 0)
	{
		auto recordRegionRect = [&](SlotScreenRect &dst)
		{
			float mat[16] = {};
			RenderManager.GDrawCalculateCustomDraw4J(region, mat);
			const float sw = (float)getRenderWidth();
			const float sh = (float)getRenderHeight();
			const float left   = (sw + mat[3] * sw) * 0.5f;
			const float right  = left + ((sw *  mat[0]) * 0.5f) * region->x1;
			const float top    = (sh - mat[7] * sh) * 0.5f;
			const float bottom = top  + ((sh * -mat[5]) * 0.5f) * region->y1;
			if (right > left && bottom > top)
			{
				dst.x0 = left;
				dst.y0 = top;
				dst.x1 = right;
				dst.y1 = bottom;
				dst.valid = true;
			}
		};
		if (slotId >= CRAFTING_H_SLOT_START && slotId < (CRAFTING_H_SLOT_START + m_iCraftablesMaxHSlotC))
		{
			recordRegionRect(m_hSlotRects[slotId - CRAFTING_H_SLOT_START]);
		}
		else if (slotId >= CRAFTING_V_SLOT_START && slotId < CRAFTING_V_SLOT_END)
		{
			recordRegionRect(m_vSlotRects[slotId - CRAFTING_V_SLOT_START]);
		}
		else if (slotId == CRAFTING_OUTPUT_SLOT_START)
		{
			recordRegionRect(m_outputSlotRect);
		}
	}
#endif
	if (slotId == -1)
	{
		app.DebugPrintf("This is not the control we are looking for\n");
	}
	else if(slotId >= CRAFTING_INVENTORY_SLOT_START && slotId < CRAFTING_INVENTORY_SLOT_END)
	{
		int iIndex = slotId - CRAFTING_INVENTORY_SLOT_START;
		iIndex += m_iMenuInventoryStart;
		Slot *slot = m_menu->getSlot(iIndex);
		item = slot->getItem();
		inventoryItem = true;
	}
	else if(slotId >= CRAFTING_HOTBAR_SLOT_START && slotId < CRAFTING_HOTBAR_SLOT_END)
	{
		int iIndex = slotId - CRAFTING_HOTBAR_SLOT_START;
		iIndex += m_iMenuHotBarStart;
		Slot *slot = m_menu->getSlot(iIndex);
		item = slot->getItem();
		inventoryItem = true;
	}
	else if(slotId >= CRAFTING_V_SLOT_START && slotId < CRAFTING_V_SLOT_END )
	{
		decorations = false;
		int iIndex = slotId - CRAFTING_V_SLOT_START;
		if(m_vSlotsInfo[iIndex].show)
		{
			item = m_vSlotsInfo[iIndex].item;
			alpha = ((float)m_vSlotsInfo[iIndex].alpha)/31.0f;
		}
	}
	else if(slotId >= CRAFTING_H_SLOT_START && slotId < (CRAFTING_H_SLOT_START + m_iCraftablesMaxHSlotC) )
	{
		decorations = false;
		int iIndex = slotId - CRAFTING_H_SLOT_START;
		if(m_hSlotsInfo[iIndex].show)
		{
			item = m_hSlotsInfo[iIndex].item;
			alpha = ((float)m_hSlotsInfo[iIndex].alpha)/31.0f;
		}
	}
	else if(slotId >= CRAFTING_INGREDIENTS_LAYOUT_START && slotId < (CRAFTING_INGREDIENTS_LAYOUT_START + m_iIngredientsMaxSlotC) )
	{
		int iIndex = slotId - CRAFTING_INGREDIENTS_LAYOUT_START;
		if(m_ingredientsSlotsInfo[iIndex].show)
		{
			item = m_ingredientsSlotsInfo[iIndex].item;
			alpha = ((float)m_ingredientsSlotsInfo[iIndex].alpha)/31.0f;
		}
	}
	else if(slotId >= CRAFTING_INGREDIENTS_DESCRIPTION_START && slotId < (CRAFTING_INGREDIENTS_DESCRIPTION_START + 4) )
	{
		int iIndex = slotId - CRAFTING_INGREDIENTS_DESCRIPTION_START;
		if(m_ingredientsInfo[iIndex].show)
		{
			item = m_ingredientsInfo[iIndex].item;
			alpha = ((float)m_ingredientsInfo[iIndex].alpha)/31.0f;
		}
	}
	else if(slotId == CRAFTING_OUTPUT_SLOT_START )
	{
		if(m_craftingOutputSlotInfo.show)
		{
			item = m_craftingOutputSlotInfo.item;
			alpha = ((float)m_craftingOutputSlotInfo.alpha)/31.0f;
		}
	}

	if(item != NULL)
	{
		if(!inventoryItem)
		{
			if( item->id == Item::clock_Id || item->id == Item::compass_Id )
			{
				// 4J Stu - For clocks and compasses we set the aux value to a special one that signals we should use a default texture
				// rather than the dynamic one for the player
				item->setAuxValue(0xFF);
			}
			else if( (item->getAuxValue() & 0xFF) == 0xFF)
			{
				// 4J Stu - If the aux value is set to match any
				item->setAuxValue(0);
			}
		}
		customDrawSlotControl(region,m_iPad,item,alpha,item->isFoil(),decorations);
	}
}

int UIScene_CraftingMenu::getPad()
{
	return m_iPad;
}

bool UIScene_CraftingMenu::allowRepeat(int key)
{
	switch(key)
	{
	// X is used to open this menu, so don't let it repeat
	case ACTION_MENU_X:
		return false;
	}
	return true;
}

void UIScene_CraftingMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	//app.DebugPrintf("UIScene_InventoryMenu handling input for pad %d, key %d, down- %s, pressed- %s, released- %s\n", iPad, key, down?"TRUE":"FALSE", pressed?"TRUE":"FALSE", released?"TRUE":"FALSE");
	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	switch(key)
	{
	case ACTION_MENU_OTHER_STICK_UP:
	case ACTION_MENU_OTHER_STICK_DOWN:
		sendInputToMovie(key,repeat,pressed,released);
		break;
	default:
		if(pressed)
		{
			handled = handleKeyDown(m_iPad, key, repeat);
		}
		break;
	};
}

void UIScene_CraftingMenu::hideAllHSlots()
{
	for(unsigned int iIndex = 0; iIndex < m_iMaxHSlotC; ++iIndex)
	{
		m_hSlotsInfo[iIndex].item = nullptr;
		m_hSlotsInfo[iIndex].alpha = 31;
		m_hSlotsInfo[iIndex].show = false;
		if((int)iIndex < m_iCraftablesMaxHSlotC)
		{
			m_slotListCraftingHSlots.showSlotRedBox(iIndex, false);
		}
	}
}

void UIScene_CraftingMenu::hideAllVSlots()
{
	for(unsigned int iIndex = 0; iIndex < m_iMaxDisplayedVSlotC; ++iIndex)
	{
		m_vSlotsInfo[iIndex].item = nullptr;
		m_vSlotsInfo[iIndex].alpha = 31;
		m_vSlotsInfo[iIndex].show = false;
		m_slotListCrafting3VSlots[iIndex].showSlotRedBox(0, false);
	}
	m_slotListCrafting2VSlots[0].showSlotRedBox(0, false);
	m_slotListCrafting2VSlots[1].showSlotRedBox(0, false);
	m_slotListCrafting1VSlots.showSlotRedBox(0, false);
}

void UIScene_CraftingMenu::hideAllIngredientsSlots()
{
	for(int i=0;i<m_iIngredientsC;i++)
	{
		m_ingredientsInfo[i].item = nullptr;
		m_ingredientsInfo[i].alpha = 31;
		m_ingredientsInfo[i].show = false;

		m_labelIngredientsDesc[i].setLabel(L"");

		IggyDataValue result;
		IggyDataValue value[2];

		value[0].type = IGGY_DATATYPE_number;
		value[0].number = i;

		value[1].type = IGGY_DATATYPE_boolean;
		value[1].boolval = false;
		IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcShowIngredientSlot , 2 , value );
	}
}

void UIScene_CraftingMenu::setCraftHSlotItem(int iPad, int iIndex, shared_ptr<ItemInstance> item, unsigned int uiAlpha)
{
	m_hSlotsInfo[iIndex].item = item;
	m_hSlotsInfo[iIndex].alpha = uiAlpha;
	m_hSlotsInfo[iIndex].show = true;
	const bool missingMaterials = (item != NULL && uiAlpha < 31);
	m_slotListCraftingHSlots.showSlotRedBox(iIndex, missingMaterials);
}

void UIScene_CraftingMenu::setCraftVSlotItem(int iPad, int iIndex, shared_ptr<ItemInstance> item, unsigned int uiAlpha)
{
	m_vSlotsInfo[iIndex].item = item;
	m_vSlotsInfo[iIndex].alpha = uiAlpha;
	m_vSlotsInfo[iIndex].show = true;
	const bool missingMaterials = (item != NULL && uiAlpha < 31);
	if(iIndex >= 0 && iIndex < m_iMaxDisplayedVSlotC)
	{
		m_slotListCrafting3VSlots[iIndex].showSlotRedBox(0, missingMaterials);
	}
	if(iIndex == 0)
	{
		m_slotListCrafting2VSlots[1].showSlotRedBox(0, missingMaterials);
	}
	else if(iIndex == 1)
	{
		m_slotListCrafting2VSlots[0].showSlotRedBox(0, missingMaterials);
		m_slotListCrafting1VSlots.showSlotRedBox(0, missingMaterials);
	}
}

void UIScene_CraftingMenu::setCraftingOutputSlotItem(int iPad, shared_ptr<ItemInstance> item)
{
	m_craftingOutputSlotInfo.item = item;
	m_craftingOutputSlotInfo.alpha = 31;
	m_craftingOutputSlotInfo.show = item != NULL;
}

void UIScene_CraftingMenu::setCraftingOutputSlotRedBox(bool show)
{
	m_slotListCraftingOutput.showSlotRedBox(0,show);
}

void UIScene_CraftingMenu::setIngredientSlotItem(int iPad, int index, shared_ptr<ItemInstance> item)
{
	m_ingredientsSlotsInfo[index].item = item;
	m_ingredientsSlotsInfo[index].alpha = 31;
	m_ingredientsSlotsInfo[index].show = item != NULL;
}

void UIScene_CraftingMenu::setIngredientSlotRedBox(int index, bool show)
{
	m_slotListIngredientsLayout.showSlotRedBox(index,show);
}

void UIScene_CraftingMenu::setIngredientDescriptionItem(int iPad, int index, shared_ptr<ItemInstance> item)
{
	m_ingredientsInfo[index].item = item;
	m_ingredientsInfo[index].alpha = 31;
	m_ingredientsInfo[index].show = item != NULL;

	IggyDataValue result;
	IggyDataValue value[2];

	value[0].type = IGGY_DATATYPE_number;
	value[0].number = index;

	value[1].type = IGGY_DATATYPE_boolean;
	value[1].boolval = m_ingredientsInfo[index].show;
	IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcShowIngredientSlot , 2 , value );
}

void UIScene_CraftingMenu::setIngredientDescriptionRedBox(int index, bool show)
{
	m_slotListIngredients[index].showSlotRedBox(0,show);
}

void UIScene_CraftingMenu::setIngredientDescriptionText(int index, LPCWSTR text)
{
	m_labelIngredientsDesc[index].setLabel(text);
}


void UIScene_CraftingMenu::setShowCraftHSlot(int iIndex, bool show)
{
	m_hSlotsInfo[iIndex].show = show;
	if(!show)
	{
		m_slotListCraftingHSlots.showSlotRedBox(iIndex, false);
	}
	else
	{
		m_slotListCraftingHSlots.showSlotRedBox(iIndex, m_hSlotsInfo[iIndex].item != NULL && m_hSlotsInfo[iIndex].alpha < 31);
	}
}

void UIScene_CraftingMenu::showTabHighlight(int iIndex, bool show)
{
	if(show)
	{	
		IggyDataValue result;
		IggyDataValue value[1];

		value[0].type = IGGY_DATATYPE_number;
		value[0].number = iIndex;
		IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcSetActiveTab , 1 , value );
	}
}

void UIScene_CraftingMenu::setGroupText(LPCWSTR text)
{
	m_labelGroupName.setLabel(text);
}

void UIScene_CraftingMenu::setDescriptionText(LPCWSTR text)
{
	m_labelDescription.setLabel(text);
}

void UIScene_CraftingMenu::setItemText(LPCWSTR text)
{
	m_labelItemName.setLabel(text);
}

void UIScene_CraftingMenu::UpdateMultiPanel()
{
	// Call Iggy function to show the current panel
	IggyDataValue result;
	IggyDataValue value[1];

	value[0].type = IGGY_DATATYPE_number;
	value[0].number = m_iDisplayDescription;

	IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcShowPanelDisplay , 1 , value );
}

void UIScene_CraftingMenu::scrollDescriptionUp()
{
	// handled differently
}

void UIScene_CraftingMenu::scrollDescriptionDown()
{
	// handled differently
}

void UIScene_CraftingMenu::updateHighlightAndScrollPositions()
{
	{	
		IggyDataValue result;
		IggyDataValue value[2];

		value[0].type = IGGY_DATATYPE_number;
		value[0].number = m_iCurrentSlotHIndex;

		int selectorType = 0;
		if(CanBeMadeA[m_iCurrentSlotHIndex].iCount  == 2)
		{
			selectorType = 1;
		}
		else if( CanBeMadeA[m_iCurrentSlotHIndex].iCount  > 2)
		{
			selectorType = 2;
		}

		value[1].type = IGGY_DATATYPE_number;
		value[1].number = selectorType;

		IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcMoveSelector , 2 , value );
	}

	{
		IggyDataValue result;
		IggyDataValue value[1];

		value[0].type = IGGY_DATATYPE_number;
		value[0].number = m_iCurrentSlotVIndex;

		IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcSelectVerticalItem , 1 , value );
	}
}

void UIScene_CraftingMenu::updateVSlotPositions(int iSlots, int i)
{
	// Not needed
}
