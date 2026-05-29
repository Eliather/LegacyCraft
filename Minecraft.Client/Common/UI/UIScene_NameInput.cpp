#include "stdafx.h"
#include "UI.h"
#include "UIScene_NameInput.h"

#define NAME_INPUT_DONE_TIMER_ID 0
#define NAME_INPUT_DONE_TIMER_TIME 100

namespace
{
	const int DEFAULT_NAME_INPUT_CHAR_LIMIT = 15;

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

	m_ButtonSpace.init(L"Space", -1);
	m_ButtonCursorLeft.init(L"Cursor Left", -1);
	m_ButtonCursorRight.init(L"Cursor Right", -1);
	m_ButtonCaps.init(L"Caps", -1);
	m_ButtonDone.init(L"Done", 0);
	m_ButtonSymbols.init(L"Symbols", -1);
	m_ButtonBackspace.init(L"Backspace", -1);

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

	parentLayer->addComponent(iPad, eUIComponent_MenuBackground);
	Keyboard::enableRepeatEvents(true);
	SetFocusToElement(m_TextInput.getId());

	delete params;
}

UIScene_NameInput::~UIScene_NameInput()
{
	Keyboard::enableRepeatEvents(false);
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
	UIScene::tick();

	if(processDirectKeyboardInput())
	{
		return;
	}

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
	if((int)controlId == 0 && !m_bDonePressed)
	{
		addTimer(NAME_INPUT_DONE_TIMER_ID, NAME_INPUT_DONE_TIMER_TIME);
		m_bDonePressed = true;
	}
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
			continue;
		}

		if(eventKey == VK_DELETE)
		{
			clearInputText();
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

		wstring currentText = SanitiseNameInputText(m_TextInput.getLabel(), m_charLimit);
		if((int)currentText.length() >= m_charLimit)
		{
			continue;
		}

		currentText += eventChar;
		setInputText(currentText);
	}

	return false;
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
