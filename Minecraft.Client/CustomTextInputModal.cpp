#include "stdafx.h"
#include "CustomTextInputModal.h"

#include <cmath>

#include "CustomGenericButton.h"
#include "Minecraft.h"
#include "Tesselator.h"
#include "stubs.h"
#include "Common\UI\UI.h"
#include "..\Minecraft.World\StringHelpers.h"
#include <cwctype>

#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif

namespace
{
	CustomTextInputModal::Params g_params;
	bool g_open = false;
	std::wstring g_text;
	size_t g_cursor = 0;
	size_t g_scrollOffset = 0;
	bool g_selectedAll = false;
	std::wstring g_errorText;
	bool g_waitRelease = false;
	int g_suppressMenuFrames = 0;
	long long g_lastInputClickTime = 0;

	CustomGenericButton g_saveButton;
	CustomGenericButton g_cancelButton;
	bool g_buttonsReady = false;
	S32 g_lastW = -1;
	S32 g_lastH = -1;

	struct ModalLayout
	{
		float scale;
		float panelX, panelY, panelW, panelH;
		float inputX, inputY, inputW, inputH;
		float textX, textY, textScale, maxVisibleW;
		float hintY;
		float btnY, saveX, cancelX, btnW, btnH;
	};

	const wchar_t *GetSaveLabel()
	{
		switch(XGetLanguage())
		{
		case XC_LANGUAGE_SPANISH:
			return L"Guardar";
		case XC_LANGUAGE_FRENCH:
			return L"Enregistrer";
		case XC_LANGUAGE_GERMAN:
			return L"Speichern";
		case XC_LANGUAGE_ITALIAN:
			return L"Salva";
		case XC_LANGUAGE_PORTUGUESE:
		case MINECRAFT_LANGUAGE_BRAZILIAN:
			return L"Salvar";
		default:
			return L"Save";
		}
	}

	const wchar_t *GetCancelLabel()
	{
		switch(XGetLanguage())
		{
		case XC_LANGUAGE_SPANISH:
		case XC_LANGUAGE_PORTUGUESE:
		case MINECRAFT_LANGUAGE_BRAZILIAN:
			return L"Cancelar";
		case XC_LANGUAGE_FRENCH:
			return L"Annuler";
		case XC_LANGUAGE_GERMAN:
			return L"Abbrechen";
		case XC_LANGUAGE_ITALIAN:
			return L"Annulla";
		default:
			return L"Cancel";
		}
	}

	std::wstring GetDefaultLengthErrorText(int minLength)
	{
		wchar_t buf[96] = {0};
		switch(XGetLanguage())
		{
		case XC_LANGUAGE_SPANISH:
			_snwprintf(buf, 96, L"Debe tener al menos %d caracteres", minLength);
			break;
		case XC_LANGUAGE_FRENCH:
			_snwprintf(buf, 96, L"%d caracteres minimum requis", minLength);
			break;
		case XC_LANGUAGE_GERMAN:
			_snwprintf(buf, 96, L"Mindestens %d Zeichen erforderlich", minLength);
			break;
		case XC_LANGUAGE_ITALIAN:
			_snwprintf(buf, 96, L"Minimo %d caratteri richiesti", minLength);
			break;
		case XC_LANGUAGE_PORTUGUESE:
		case MINECRAFT_LANGUAGE_BRAZILIAN:
			_snwprintf(buf, 96, L"Minimo de %d caracteres obrigatorio", minLength);
			break;
		default:
			_snwprintf(buf, 96, L"Must be at least %d characters", minLength);
			break;
		}
		buf[95] = 0;
		return buf;
	}

	bool DefaultCharFilter(wchar_t ch)
	{
		return (ch >= 32 && ch <= 126);
	}

	std::wstring SanitizeText(const std::wstring &raw, int maxLength, CustomTextInputModal::CharFilterFunc filter)
	{
		std::wstring clean;
		if(maxLength <= 0)
		{
			return clean;
		}

		CustomTextInputModal::CharFilterFunc activeFilter = (filter != NULL) ? filter : &DefaultCharFilter;
		clean.reserve(raw.length());
		for(size_t i = 0; i < raw.length() && (int)clean.length() < maxLength; ++i)
		{
			if(activeFilter(raw[i]))
			{
				clean += raw[i];
			}
		}
		return clean;
	}

	ModalLayout ComputeLayout(S32 width, S32 height)
	{
		ModalLayout l;
		const float sx = (width > 0) ? ((float)width / 1280.0f) : 1.0f;
		const float sy = (height > 0) ? ((float)height / 720.0f) : 1.0f;
		l.scale = (sx < sy) ? sx : sy;
		if(l.scale < 0.5f) l.scale = 0.5f;

		l.panelW = 560.0f * l.scale;
		l.panelH = 236.0f * l.scale;
		l.panelX = ((float)width - l.panelW) * 0.5f;
		l.panelY = ((float)height - l.panelH) * 0.5f;

		l.inputW = 496.0f * l.scale;
		l.inputH = 44.0f * l.scale;
		l.inputX = l.panelX + (l.panelW - l.inputW) * 0.5f;
		l.inputY = l.panelY + 66.0f * l.scale;

		l.textScale = 2.0f * l.scale;
		l.textX = l.inputX + 12.0f * l.scale;
		l.textY = l.inputY + (l.inputH - 8.0f * l.textScale) * 0.5f;
		l.maxVisibleW = l.inputW - 24.0f * l.scale;
		l.hintY = l.inputY + l.inputH + 12.0f * l.scale;

		l.btnW = 196.0f * l.scale;
		l.btnH = 40.0f * l.scale;
		const float btnGap = 24.0f * l.scale;
		const float totalBtnW = l.btnW * 2.0f + btnGap;
		l.saveX = l.panelX + (l.panelW - totalBtnW) * 0.5f;
		l.cancelX = l.saveX + l.btnW + btnGap;
		l.btnY = l.panelY + l.panelH - l.btnH - 22.0f * l.scale;
		return l;
	}

	void EnsureButtons(S32 width, S32 height)
	{
		if(g_buttonsReady && g_lastW == width && g_lastH == height)
		{
			return;
		}
		g_lastW = width;
		g_lastH = height;
		const ModalLayout l = ComputeLayout(width, height);
		g_saveButton.Setup(NULL, NULL, l.saveX, l.btnY, l.btnW, l.btnH);
		g_cancelButton.Setup(NULL, NULL, l.cancelX, l.btnY, l.btnW, l.btnH);
		g_buttonsReady = true;
	}

	void FillSolidRect(float x0, float y0, float x1, float y1, int r, int g, int b, int a)
	{
		Tesselator *t = Tesselator::getInstance();
		t->begin();
		t->color(r, g, b, a);
		t->vertex(x0, y0, 0.0f);
		t->vertex(x1, y0, 0.0f);
		t->vertex(x1, y1, 0.0f);
		t->vertex(x0, y1, 0.0f);
		t->end();
	}

	void UpdateScrollWindow(Font *font, const ModalLayout &l)
	{
		if(g_cursor > g_text.length())
		{
			g_cursor = g_text.length();
		}
		if(g_scrollOffset > g_cursor)
		{
			g_scrollOffset = g_cursor;
		}
		if(font == NULL)
		{
			return;
		}

		while(g_scrollOffset < g_cursor &&
			  (float)font->width(g_text.substr(g_scrollOffset, g_cursor - g_scrollOffset)) * l.textScale > l.maxVisibleW)
		{
			++g_scrollOffset;
		}
	}

	size_t ComputeVisibleEnd(Font *font, const ModalLayout &l)
	{
		if(font == NULL)
		{
			return g_text.length();
		}
		size_t endIdx = g_scrollOffset;
		while(endIdx < g_text.length())
		{
			const float w = (float)font->width(g_text.substr(g_scrollOffset, endIdx - g_scrollOffset + 1)) * l.textScale;
			if(w > l.maxVisibleW)
			{
				break;
			}
			++endIdx;
		}
		return endIdx;
	}

	void InsertText(const std::wstring &rawText)
	{
		const std::wstring clean = SanitizeText(rawText, g_params.maxLength, g_params.charFilter);
		if(clean.empty())
		{
			return;
		}

		if(g_selectedAll)
		{
			g_text.clear();
			g_cursor = 0;
			g_scrollOffset = 0;
			g_selectedAll = false;
		}

		for(size_t i = 0; i < clean.length() && (int)g_text.length() < g_params.maxLength; ++i)
		{
			g_text.insert(g_cursor, 1, clean[i]);
			++g_cursor;
		}
		g_errorText.clear();
	}

	void PasteFromClipboard()
	{
		if(!g_params.allowPaste || !OpenClipboard(NULL))
		{
			return;
		}

		HANDLE hData = GetClipboardData(CF_UNICODETEXT);
		if(hData != NULL)
		{
			const wchar_t *clipText = (const wchar_t *)GlobalLock(hData);
			if(clipText != NULL)
			{
				InsertText(clipText);
				GlobalUnlock(hData);
			}
		}
		CloseClipboard();
	}

	void CloseModal(bool saveChanges)
	{
		if(!g_open)
		{
			return;
		}

		std::wstring resultText = SanitizeText(g_text, g_params.maxLength, g_params.charFilter);
		if(saveChanges)
		{
			if((int)resultText.length() < g_params.minLength)
			{
				g_errorText = g_params.lengthErrorText.empty()
					? GetDefaultLengthErrorText(g_params.minLength)
					: g_params.lengthErrorText;
				return;
			}
			if(g_params.validateFunc != NULL)
			{
				std::wstring customError;
				if(!g_params.validateFunc(resultText, customError))
				{
					g_errorText = customError.empty()
						? (g_params.lengthErrorText.empty()
							? GetDefaultLengthErrorText(max(1, g_params.minLength))
							: g_params.lengthErrorText)
						: customError;
					return;
				}
			}
		}

		CustomTextInputModal::CompleteFunc cb = g_params.completeFunc;
		LPVOID cbParam = g_params.completeParam;
		g_params.completeFunc = NULL;
		g_params.completeParam = NULL;

		g_open = false;
		g_errorText.clear();
		g_suppressMenuFrames = 2;
		Keyboard::enableRepeatEvents(false);
		while(Keyboard::next()) {}
		while(Mouse::next()) {}

		if(cb != NULL)
		{
			cb(cbParam, saveChanges, resultText);
		}
	}
}

void CustomTextInputModal::Open(const Params &params)
{
	if(g_open)
	{
		ForceCloseForParam(g_params.completeParam);
	}

	g_params = params;
	if(g_params.maxLength < 1) g_params.maxLength = 1;
	if(g_params.minLength < 0) g_params.minLength = 0;

	g_text = SanitizeText(g_params.initialText, g_params.maxLength, g_params.charFilter);
	g_cursor = g_text.length();
	g_scrollOffset = 0;
	g_selectedAll = !g_text.empty();
	g_errorText.clear();
	g_waitRelease = true;
	g_buttonsReady = false;
	g_lastInputClickTime = 0;
	g_suppressMenuFrames = 0;
	g_open = true;

	while(Keyboard::next()) {}
	while(Mouse::next()) {}
	Keyboard::enableRepeatEvents(true);
}

void CustomTextInputModal::ForceCloseForParam(LPVOID lpParam)
{
	if(!g_open)
	{
		return;
	}
	if(lpParam != NULL && g_params.completeParam != lpParam)
	{
		return;
	}

	g_params.completeFunc = NULL;
	g_params.completeParam = NULL;
	g_open = false;
	g_errorText.clear();
	g_suppressMenuFrames = 0;
	Keyboard::enableRepeatEvents(false);
}

bool CustomTextInputModal::IsOpen()
{
	return g_open;
}

bool CustomTextInputModal::IsSuppressingMenuInput()
{
	return g_open || (g_suppressMenuFrames > 0);
}

void CustomTextInputModal::Tick(Minecraft *minecraft)
{
	if(!g_open)
	{
		if(g_suppressMenuFrames > 0)
		{
			--g_suppressMenuFrames;
			while(Keyboard::next()) {}
			while(Mouse::next()) {}
		}
		return;
	}

	if(minecraft == NULL)
	{
		return;
	}

	const S32 width = minecraft->width_phys;
	const S32 height = minecraft->height_phys;
	EnsureButtons(width, height);
	const ModalLayout l = ComputeLayout(width, height);

	const float screenW = ui.getScreenWidth();
	const float screenH = ui.getScreenHeight();
	const float mouseX = (screenW > 0.0f && width > 0)
		? (((float)Mouse::getX() * (float)width) / screenW)
		: (float)Mouse::getX();
	const float mouseY = (screenH > 0.0f && height > 0)
		? (((float)Mouse::getY() * (float)height) / screenH)
		: (float)Mouse::getY();

	const bool ctrlDown = Keyboard::isKeyDown(Keyboard::KEY_LCONTROL) ||
		Keyboard::isKeyDown(Keyboard::KEY_RCONTROL) ||
		Keyboard::isKeyDown(VK_CONTROL);

	while(Keyboard::next())
	{
		const int eventKey = Keyboard::getEventKey();
		const wchar_t eventChar = Keyboard::getEventCharacter();
		const bool keyDown = Keyboard::getEventKeyState();
		if(!keyDown)
		{
			continue;
		}

		if(ctrlDown)
		{
			if(eventKey == Keyboard::KEY_A)
			{
				g_selectedAll = !g_text.empty();
				g_cursor = g_text.length();
				continue;
			}
			if(eventKey == Keyboard::KEY_V)
			{
				PasteFromClipboard();
				continue;
			}
			if(eventKey == Keyboard::KEY_BACK)
			{
				g_text.clear();
				g_cursor = 0;
				g_scrollOffset = 0;
				g_selectedAll = false;
				g_errorText.clear();
				continue;
			}
		}

		switch(eventKey)
		{
		case Keyboard::KEY_ESCAPE:
			CloseModal(false);
			return;
		case Keyboard::KEY_RETURN:
			CloseModal(true);
			if(!g_open)
			{
				ui.PlayUISFX(eSFX_Press);
				return;
			}
			ui.PlayUISFX(eSFX_Focus);
			continue;
		case Keyboard::KEY_BACK:
			if(g_selectedAll)
			{
				g_text.clear();
				g_cursor = 0;
				g_scrollOffset = 0;
				g_selectedAll = false;
			}
			else if(g_cursor > 0)
			{
				g_text.erase(g_cursor - 1, 1);
				--g_cursor;
			}
			g_errorText.clear();
			continue;
		case Keyboard::KEY_LEFT:
			if(g_selectedAll)
			{
				g_selectedAll = false;
				g_cursor = 0;
			}
			else if(g_cursor > 0)
			{
				--g_cursor;
			}
			continue;
		case Keyboard::KEY_RIGHT:
			if(g_selectedAll)
			{
				g_selectedAll = false;
				g_cursor = g_text.length();
			}
			else if(g_cursor < g_text.length())
			{
				++g_cursor;
			}
			continue;
		}

		if(eventKey == VK_DELETE)
		{
			if(g_selectedAll)
			{
				g_text.clear();
				g_cursor = 0;
				g_scrollOffset = 0;
				g_selectedAll = false;
			}
			else if(g_cursor < g_text.length())
			{
				g_text.erase(g_cursor, 1);
			}
			g_errorText.clear();
			continue;
		}

		if(eventKey == VK_HOME)
		{
			g_selectedAll = false;
			g_cursor = 0;
			continue;
		}

		if(eventKey == VK_END)
		{
			g_selectedAll = false;
			g_cursor = g_text.length();
			continue;
		}

		if(eventKey != Keyboard::KEY_NONE || ctrlDown)
		{
			continue;
		}

		wchar_t buf[2] = { eventChar, 0 };
		InsertText(buf);
	}

	while(Mouse::next()) {}

	if(g_waitRelease)
	{
		if(!Mouse::isButtonDown(0) && !Mouse::isButtonPressed(0))
		{
			g_waitRelease = false;
		}
		return;
	}

	if(g_saveButton.Update(minecraft))
	{
		CloseModal(true);
		return;
	}
	if(g_cancelButton.Update(minecraft))
	{
		CloseModal(false);
		return;
	}

	if(Mouse::isButtonPressed(0))
	{
		const bool inInput = (mouseX >= l.inputX && mouseX <= (l.inputX + l.inputW) &&
							  mouseY >= l.inputY && mouseY <= (l.inputY + l.inputH));
		const bool inPanel = (mouseX >= l.panelX && mouseX <= (l.panelX + l.panelW) &&
							  mouseY >= l.panelY && mouseY <= (l.panelY + l.panelH));

		if(inInput && minecraft->font != NULL)
		{
			const long long now = System::currentTimeMillis();
			if(now - g_lastInputClickTime < 350 && !g_text.empty())
			{
				g_selectedAll = true;
				g_cursor = g_text.length();
			}
			else
			{
				UpdateScrollWindow(minecraft->font, l);
				const size_t viewEnd = ComputeVisibleEnd(minecraft->font, l);
				const float relX = mouseX - l.textX;
				size_t bestIdx = g_scrollOffset;
				float bestDist = fabsf(relX);
				for(size_t i = g_scrollOffset + 1; i <= viewEnd; ++i)
				{
					const float cx = (float)minecraft->font->width(g_text.substr(g_scrollOffset, i - g_scrollOffset)) * l.textScale;
					const float dist = fabsf(relX - cx);
					if(dist < bestDist)
					{
						bestDist = dist;
						bestIdx = i;
					}
				}
				g_selectedAll = false;
				g_cursor = bestIdx;
			}
			g_lastInputClickTime = now;
		}
		else if(!inPanel)
		{
			CloseModal(false);
		}
	}
}

void CustomTextInputModal::Render(Minecraft *minecraft)
{
	if(!g_open || minecraft == NULL || minecraft->font == NULL)
	{
		return;
	}

	Font *font = minecraft->font;
	const S32 width = minecraft->width_phys;
	const S32 height = minecraft->height_phys;
	EnsureButtons(width, height);
	const ModalLayout l = ComputeLayout(width, height);
	UpdateScrollWindow(font, l);
	const size_t viewEnd = ComputeVisibleEnd(font, l);
	const std::wstring visibleText = g_text.substr(g_scrollOffset, viewEnd - g_scrollOffset);

	// Re-bind backbuffer and immediate-mode state cleanly after GDraw/Iggy rendering.
	RenderManager.StartFrame();
	ui.setupRenderPosition(C4JRender::VIEWPORT_TYPE_FULLSCREEN);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glDisable(GL_FOG);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0, (double)width, (double)height, 0.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	// Fullscreen backdrop dim
	FillSolidRect(0.0f, 0.0f, (float)width, (float)height, 0, 0, 0, 175);

	// Panel border & fill
	const float border = 2.0f * l.scale;
	FillSolidRect(l.panelX - border, l.panelY - border, l.panelX + l.panelW + border, l.panelY + l.panelH + border, 160, 160, 170, 245);
	FillSolidRect(l.panelX, l.panelY, l.panelX + l.panelW, l.panelY + l.panelH, 28, 28, 34, 245);

	// Input box border & recessed fill
	const bool hasErr = !g_errorText.empty();
	FillSolidRect(l.inputX - border, l.inputY - border, l.inputX + l.inputW + border, l.inputY + l.inputH + border,
				  hasErr ? 235 : 220, hasErr ? 80 : 220, hasErr ? 80 : 230, 255);
	FillSolidRect(l.inputX, l.inputY, l.inputX + l.inputW, l.inputY + l.inputH, 10, 10, 14, 255);

	// Selection highlight
	if(g_selectedAll && !visibleText.empty())
	{
		const float selW = (float)font->width(visibleText) * l.textScale;
		FillSolidRect(l.textX - 2.0f * l.scale, l.textY - 4.0f * l.scale,
					  l.textX + selW + 2.0f * l.scale, l.textY + 8.0f * l.textScale + 4.0f * l.scale,
					  47, 111, 214, 210);
	}

	// Blinking caret
	if((System::currentTimeMillis() % 1000) < 500)
	{
		const size_t caretRel = (g_cursor >= g_scrollOffset) ? (g_cursor - g_scrollOffset) : 0;
		const size_t safeCaret = (caretRel <= visibleText.length()) ? caretRel : visibleText.length();
		const float caretX = l.textX + (float)font->width(visibleText.substr(0, safeCaret)) * l.textScale;
		FillSolidRect(caretX, l.textY - 3.0f * l.scale, caretX + 2.0f * l.scale, l.textY + 8.0f * l.textScale + 3.0f * l.scale, 255, 255, 255, 255);
	}

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	// Title
	if(!g_params.title.empty())
	{
		const float titleScale = 2.0f * l.scale;
		const float titleX = l.panelX + (l.panelW - (float)font->width(g_params.title) * titleScale) * 0.5f;
		const float titleY = l.panelY + 22.0f * l.scale;
		glPushMatrix();
		glTranslatef(titleX, titleY, 0.0f);
		glScalef(titleScale, titleScale, 1.0f);
		font->draw(g_params.title, 1, 1, 0xff000000);
		font->draw(g_params.title, 0, 0, 0xffffffff);
		glPopMatrix();
	}

	// Input text
	if(!visibleText.empty())
	{
		glPushMatrix();
		glTranslatef(l.textX, l.textY, 0.0f);
		glScalef(l.textScale, l.textScale, 1.0f);
		font->draw(visibleText, 0, 0, 0xffffffff);
		glPopMatrix();
	}

	// Hint / validation line + live counter
	const float hintScale = 1.35f * l.scale;
	const std::wstring &subText = hasErr ? g_errorText : g_params.hintText;
	if(!subText.empty())
	{
		glPushMatrix();
		glTranslatef(l.inputX, l.hintY, 0.0f);
		glScalef(hintScale, hintScale, 1.0f);
		font->draw(subText, 0, 0, hasErr ? 0xffff6666 : 0xffb8b8c0);
		glPopMatrix();
	}

	wchar_t countBuf[32] = {0};
	_snwprintf(countBuf, 32, L"%u/%d", (unsigned int)g_text.length(), g_params.maxLength);
	countBuf[31] = 0;
	const float countW = (float)font->width(countBuf) * hintScale;
	const unsigned int countColor = ((int)g_text.length() < g_params.minLength) ? 0xffff8888 : 0xffb8b8c0;
	glPushMatrix();
	glTranslatef(l.inputX + l.inputW - countW, l.hintY, 0.0f);
	glScalef(hintScale, hintScale, 1.0f);
	font->draw(countBuf, 0, 0, countColor);
	glPopMatrix();

	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	g_saveButton.Render(minecraft, font, GetSaveLabel(), (int)C4JRender::VIEWPORT_TYPE_FULLSCREEN);
	g_cancelButton.Render(minecraft, font, GetCancelLabel(), (int)C4JRender::VIEWPORT_TYPE_FULLSCREEN);
}

bool CustomTextInputModal::IsAcceptedWorldNameChar(wchar_t ch)
{
	return ch == L' ' || ch == L'-' || ch == L'_' || iswalpha(ch) != 0 || (ch >= L'0' && ch <= L'9');
}

bool CustomTextInputModal::ValidateWorldNameNotEmpty(const std::wstring &text, std::wstring &outError)
{
	if(!trimString(text).empty())
	{
		return true;
	}

	switch(XGetLanguage())
	{
	case XC_LANGUAGE_SPANISH:
		outError = L"El nombre del mundo no puede estar vacio";
		break;
	case XC_LANGUAGE_FRENCH:
		outError = L"Le nom du monde ne peut pas etre vide";
		break;
	case XC_LANGUAGE_GERMAN:
		outError = L"Der Weltname darf nicht leer sein";
		break;
	case XC_LANGUAGE_ITALIAN:
		outError = L"Il nome del mondo non puo essere vuoto";
		break;
	case XC_LANGUAGE_PORTUGUESE:
	case MINECRAFT_LANGUAGE_BRAZILIAN:
		outError = L"O nome do mundo nao pode estar vazio";
		break;
	default:
		outError = L"World name cannot be empty";
		break;
	}
	return false;
}
