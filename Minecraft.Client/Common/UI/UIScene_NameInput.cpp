#include "stdafx.h"
#include "UI.h"
#include "UIScene_NameInput.h"

#define NAME_INPUT_DONE_TIMER_ID 0
#define NAME_INPUT_DONE_TIMER_TIME 100

namespace
{
	const int DEFAULT_NAME_INPUT_CHAR_LIMIT = 15;
	const int NAME_INPUT_BUTTON_DONE = 0;
	const int NAME_INPUT_BUTTON_SPACE = 1;
	const int NAME_INPUT_BUTTON_CURSOR_LEFT = 2;
	const int NAME_INPUT_BUTTON_CURSOR_RIGHT = 3;
	const int NAME_INPUT_BUTTON_CAPS = 4;
	const int NAME_INPUT_BUTTON_SYMBOLS = 5;
	const int NAME_INPUT_BUTTON_BACKSPACE = 6;
	const int NAME_INPUT_MOUSE_BASE_WIDTH = 1920;
	const int NAME_INPUT_MOUSE_BASE_HEIGHT = 1080;

	S32 ToNameInputBaseX(S32 x)
	{
		const S32 screenWidth = (S32)ui.getScreenWidth();
		if(screenWidth > 0 && screenWidth != NAME_INPUT_MOUSE_BASE_WIDTH)
		{
			return (x * NAME_INPUT_MOUSE_BASE_WIDTH) / screenWidth;
		}
		return x;
	}

	S32 ToNameInputBaseY(S32 y)
	{
		const S32 screenHeight = (S32)ui.getScreenHeight();
		if(screenHeight > 0 && screenHeight != NAME_INPUT_MOUSE_BASE_HEIGHT)
		{
			return (y * NAME_INPUT_MOUSE_BASE_HEIGHT) / screenHeight;
		}
		return y;
	}

	bool NameInputHitRect(S32 x, S32 y, S32 left, S32 top, S32 width, S32 height)
	{
		return x >= left && x <= (left + width) && y >= top && y <= (top + height);
	}

	bool IsAcceptedNameInputCharacter(wchar_t ch)
	{
		return ((ch >= L'0' && ch <= L'9') ||
				(ch >= L'A' && ch <= L'Z') ||
				(ch >= L'a' && ch <= L'z') ||
				ch == L'_');
	}

	wstring SanitiseNameInputText(const wchar_t *pwchText, int charLimit)
	{
		wstring sanitised;
		if(pwchText == NULL || charLimit <= 0)
		{
			return sanitised;
		}

		while(*pwchText != 0 && (int)sanitised.length() < charLimit)
		{
			if(IsAcceptedNameInputCharacter(*pwchText))
			{
				sanitised += *pwchText;
			}
			++pwchText;
		}

		return sanitised;
	}
}

UIScene_NameInput::UIScene_NameInput(int iPad, void *initData, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	initialiseMovie();

	NameInputParams *params = (NameInputParams *)initData;

	wstring title = L"Enter Text";
	wstring initialText = L"";
	int charLimit = DEFAULT_NAME_INPUT_CHAR_LIMIT;

	m_completeFunc = NULL;
	m_completeFuncParam = NULL;

	if(params != NULL)
	{
		if(!params->title.empty())
		{
			title = params->title;
		}

		initialText = params->initialText;
		if(params->charLimit > 0)
		{
			charLimit = params->charLimit;
		}

		m_completeFunc = params->completeFunc;
		m_completeFuncParam = params->completeFuncParam;
	}

	m_charLimit = charLimit;
	initialText = SanitiseNameInputText(initialText.c_str(), m_charLimit);

	m_EnterTextLabel.init(title);

	m_TextInput.init(initialText, 1);
	m_TextInput.SetCharLimit(m_charLimit);

	m_ButtonSpace.init(L"Space", NAME_INPUT_BUTTON_SPACE);
	m_ButtonCursorLeft.init(L"Cursor Left", NAME_INPUT_BUTTON_CURSOR_LEFT);
	m_ButtonCursorRight.init(L"Cursor Right", NAME_INPUT_BUTTON_CURSOR_RIGHT);
	m_ButtonCaps.init(L"Caps", NAME_INPUT_BUTTON_CAPS);
	m_ButtonDone.init(L"Done", NAME_INPUT_BUTTON_DONE);
	m_ButtonSymbols.init(L"Symbols", NAME_INPUT_BUTTON_SYMBOLS);
	m_ButtonBackspace.init(L"Backspace", NAME_INPUT_BUTTON_BACKSPACE);

	wstring label = L"Abc";
	IggyStringUTF16 stringVal;
	stringVal.string = (IggyUTF16*)label.c_str();
	stringVal.length = label.length();

	IggyDataValue result;
	IggyDataValue value[1];
	value[0].type = IGGY_DATATYPE_string_UTF16;
	value[0].string16 = stringVal;

	IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcInitFunctionButtons, 1, value);

	m_bDonePressed = false;
	m_bCompletionSent = false;
	m_bHandlePressCalledThisTick = false;
	m_bMouseCaps = false;

	parentLayer->addComponent(iPad, eUIComponent_MenuBackground);
#ifdef _WINDOWS64
	while(Mouse::next()) {}
	while(Keyboard::next()) {}
#endif
	Keyboard::enableRepeatEvents(true);
	SetFocusToElement(m_TextInput.getId());

	delete params;
}

UIScene_NameInput::~UIScene_NameInput()
{
	Keyboard::enableRepeatEvents(false);
#ifdef _WINDOWS64
	while(Mouse::next()) {}
	while(Keyboard::next()) {}
#endif
	m_parentLayer->removeComponent(eUIComponent_MenuBackground);
}

wstring UIScene_NameInput::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1 && !m_parentLayer->IsFullscreenGroup())
	{
		return L"KeyboardSplit";
	}
	else
	{
		return L"Keyboard";
	}
}

void UIScene_NameInput::tick()
{
	m_bHandlePressCalledThisTick = false;
	dispatchMouseInputToMovie();
	processDirectKeyboardInput();

	UIScene::tick();
	sanitiseCurrentInputText();
}

void UIScene_NameInput::updateTooltips()
{
	ui.SetTooltips(DEFAULT_XUI_MENU_USER, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK, -1, -1);
}

bool UIScene_NameInput::allowRepeat(int key)
{
	switch(key)
	{
	case ACTION_MENU_OK:
	case ACTION_MENU_CANCEL:
	case ACTION_MENU_A:
	case ACTION_MENU_B:
	case ACTION_MENU_PAUSEMENU:
		return false;
	}
	return true;
}

void UIScene_NameInput::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	IggyDataValue result;

	if(pressed)
	{
		m_bHandlePressCalledThisTick = false;
	}

	if(repeat || pressed)
	{
		switch(key)
		{
		case ACTION_MENU_CANCEL:
			completeInput(false);
			navigateBack();
			handled = true;
			break;
		case ACTION_MENU_X:
			IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcBackspaceButtonPressed, 0, NULL);
			handled = true;
			break;
		case ACTION_MENU_PAGEUP:
			IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSymbolButtonPressed, 0, NULL);
			handled = true;
			break;
		case ACTION_MENU_Y:
			IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSpaceButtonPressed, 0, NULL);
			handled = true;
			break;
		case ACTION_MENU_STICK_PRESS:
			IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcCapsButtonPressed, 0, NULL);
			handled = true;
			break;
		case ACTION_MENU_LEFT_SCROLL:
			IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcCursorLeftButtonPressed, 0, NULL);
			handled = true;
			break;
		case ACTION_MENU_RIGHT_SCROLL:
			IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcCursorRightButtonPressed, 0, NULL);
			handled = true;
			break;
		case ACTION_MENU_PAUSEMENU:
			if(!m_bDonePressed)
			{
				IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcDoneButtonPressed, 0, NULL);
				addTimer(NAME_INPUT_DONE_TIMER_ID, NAME_INPUT_DONE_TIMER_TIME);
				m_bDonePressed = true;
			}
			handled = true;
			break;
		}
	}

	switch(key)
	{
	case ACTION_MENU_OK:
#ifdef _WINDOWS64
		// Physical Enter/Space and mouse clicks are handled directly; forwarding OK
		// to Keyboard.swf activates its focused virtual key (e.g. '1').
		handled = true;
		break;
#endif
	case ACTION_MENU_LEFT:
	case ACTION_MENU_RIGHT:
	case ACTION_MENU_UP:
	case ACTION_MENU_DOWN:
		sendInputToMovie(key, repeat, pressed, released);
		handled = true;
		break;
	}
}

void UIScene_NameInput::handlePress(F64 controlId, F64 childId)
{
	UNREFERENCED_PARAMETER(childId);

	m_bHandlePressCalledThisTick = true;

	IggyDataValue result;

	switch((int)controlId)
	{
	case NAME_INPUT_BUTTON_DONE:
		if(!m_bDonePressed)
		{
			addTimer(NAME_INPUT_DONE_TIMER_ID, NAME_INPUT_DONE_TIMER_TIME);
			m_bDonePressed = true;
		}
		break;
	case NAME_INPUT_BUTTON_SPACE:
		IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSpaceButtonPressed, 0, NULL);
		break;
	case NAME_INPUT_BUTTON_CURSOR_LEFT:
		IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcCursorLeftButtonPressed, 0, NULL);
		break;
	case NAME_INPUT_BUTTON_CURSOR_RIGHT:
		IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcCursorRightButtonPressed, 0, NULL);
		break;
	case NAME_INPUT_BUTTON_CAPS:
		IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcCapsButtonPressed, 0, NULL);
		break;
	case NAME_INPUT_BUTTON_SYMBOLS:
		IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSymbolButtonPressed, 0, NULL);
		break;
	case NAME_INPUT_BUTTON_BACKSPACE:
		IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcBackspaceButtonPressed, 0, NULL);
		break;
	}
}

void UIScene_NameInput::dispatchMouseInputToMovie()
{
#ifdef _WINDOWS64
	if(!hasMovie())
	{
		return;
	}

	while(Mouse::next())
	{
		S32 eventX = (S32)Mouse::getEventX();
		S32 eventY = (S32)Mouse::getEventY();

		const int button = Mouse::getEventButton();
		if(button == 0 && Mouse::getEventButtonState())
		{
			handleMouseKeyboardClick(eventX, eventY);
		}
	}
#endif
}

bool UIScene_NameInput::handleMouseKeyboardClick(S32 mouseX, S32 mouseY)
{
	const S32 x = ToNameInputBaseX(mouseX);
	const S32 y = ToNameInputBaseY(mouseY);

	const S32 leftSpecialX = 267;
	const S32 mainX = 410;
	const S32 rightSpecialX = 1516;
	const S32 topY = 570;
	const S32 keyW = 102;
	const S32 keyH = 65;
	const S32 keyStepX = 109;
	const S32 keyStepY = 73;

	static const wchar_t *rows[4] =
	{
		L"1234567890",
		L"qwertyuiop",
		L"asdfghjkl,",
		L"zxcvbnm,.?"
	};

	if(NameInputHitRect(x, y, leftSpecialX, topY, 136, 210))
	{
		IggyDataValue result;
		IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcCursorLeftButtonPressed, 0, NULL);
		return true;
	}

	if(NameInputHitRect(x, y, rightSpecialX, topY, 136, 210))
	{
		IggyDataValue result;
		IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcCursorRightButtonPressed, 0, NULL);
		return true;
	}

	for(int row = 0; row < 4; ++row)
	{
		const S32 rowY = topY + (row * keyStepY);
		if(y < rowY || y > rowY + keyH)
		{
			continue;
		}

		for(int col = 0; col < 10; ++col)
		{
			const S32 keyX = mainX + (col * keyStepX);
			if(NameInputHitRect(x, y, keyX, rowY, keyW, keyH))
			{
				appendInputCharacter(rows[row][col]);
				return true;
			}
		}
	}

	if(NameInputHitRect(x, y, leftSpecialX, topY + keyStepY * 3, 136, 137))
	{
		m_bMouseCaps = !m_bMouseCaps;
		return true;
	}

	if(NameInputHitRect(x, y, mainX, topY + keyStepY * 4, 320, keyH))
	{
		return true;
	}

	if(NameInputHitRect(x, y, mainX + 327, topY + keyStepY * 4, 430, keyH))
	{
		appendInputCharacter(L' ');
		return true;
	}

	if(NameInputHitRect(x, y, mainX + 764, topY + keyStepY * 4, 320, keyH))
	{
		backspaceInputText();
		return true;
	}

	if(NameInputHitRect(x, y, rightSpecialX, topY + keyStepY * 3, 136, 137))
	{
		handleDonePressed();
		return true;
	}

	return false;
}

void UIScene_NameInput::handleTimerComplete(int id)
{
	if(id == NAME_INPUT_DONE_TIMER_ID)
	{
		killTimer(NAME_INPUT_DONE_TIMER_ID);
		handleDonePressed();
	}
}

void UIScene_NameInput::handleDestroy()
{
	completeInput(false);
}

bool UIScene_NameInput::processDirectKeyboardInput()
{
	bool handledAny = false;

	while(Keyboard::next())
	{
		const int eventKey = Keyboard::getEventKey();
		const wchar_t eventChar = Keyboard::getEventCharacter();
		const bool keyDown = Keyboard::getEventKeyState();

		if(!keyDown)
		{
			continue;
		}

		switch(eventKey)
		{
		case Keyboard::KEY_ESCAPE:
			completeInput(false);
			navigateBack();
			return true;
		case Keyboard::KEY_RETURN:
			handleDonePressed();
			return true;
		case Keyboard::KEY_BACK:
			backspaceInputText();
			handledAny = true;
			continue;
		}

		if(eventKey == VK_DELETE)
		{
			backspaceInputText();
			handledAny = true;
			continue;
		}

		if(eventKey != Keyboard::KEY_NONE)
		{
			continue;
		}

		if(eventChar == 0 || eventChar == L'\r' || eventChar == L'\n' || eventChar == L'\t' || eventChar == L'\b')
		{
			continue;
		}

		if(!IsAcceptedNameInputCharacter(eventChar))
		{
			continue;
		}

		appendInputCharacter(eventChar);
		handledAny = true;
	}

	return handledAny;
}

void UIScene_NameInput::appendInputCharacter(wchar_t ch)
{
	if(ch >= L'a' && ch <= L'z' && m_bMouseCaps)
	{
		ch = (wchar_t)(ch - L'a' + L'A');
	}

	if(!IsAcceptedNameInputCharacter(ch))
	{
		return;
	}

	wstring currentText = SanitiseNameInputText(m_TextInput.getLabel(), m_charLimit);
	if((int)currentText.length() >= m_charLimit)
	{
		return;
	}

	currentText += ch;
	setInputText(currentText);
}

void UIScene_NameInput::sanitiseCurrentInputText()
{
	const wstring sanitisedText = SanitiseNameInputText(m_TextInput.getLabel(), m_charLimit);
	setInputText(sanitisedText);
}

void UIScene_NameInput::setInputText(const wstring &text, bool force)
{
	const wstring sanitisedText = SanitiseNameInputText(text.c_str(), m_charLimit);
	if(force || sanitisedText != m_TextInput.getLabel())
	{
		m_TextInput.setLabel(sanitisedText, true, force);
	}
}

void UIScene_NameInput::backspaceInputText()
{
	wstring currentText = SanitiseNameInputText(m_TextInput.getLabel(), m_charLimit);
	if(!currentText.empty())
	{
		currentText.erase(currentText.length() - 1, 1);
		setInputText(currentText, true);
	}
}

void UIScene_NameInput::clearInputText()
{
	setInputText(L"", true);
}

void UIScene_NameInput::completeInput(bool accepted)
{
	if(m_bCompletionSent)
	{
		return;
	}

	sanitiseCurrentInputText();
	m_bCompletionSent = true;

	if(m_completeFunc != NULL)
	{
		m_completeFunc(m_completeFuncParam, accepted, m_TextInput.getLabel());
	}
}

void UIScene_NameInput::handleDonePressed()
{
	sanitiseCurrentInputText();
	completeInput(true);
	navigateBack();
}
