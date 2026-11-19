#pragma once

#include <string>

class Minecraft;

/**
 * Reusable Windows64 modal text-input dialog rendered on top of the UI layer.
 * Captures keyboard and mouse input exclusively while open so menus and Iggy
 * Flash scenes underneath cannot intercept keystrokes or buffered clicks.
 */
class CustomTextInputModal
{
public:
	typedef bool (*CharFilterFunc)(wchar_t ch);
	typedef bool (*ValidateFunc)(const std::wstring &text, std::wstring &outError);
	typedef void (*CompleteFunc)(LPVOID lpParam, bool accepted, const std::wstring &text);

	struct Params
	{
		std::wstring title;
		std::wstring initialText;
		std::wstring hintText;
		std::wstring lengthErrorText;
		int minLength;
		int maxLength;
		bool allowPaste;
		CharFilterFunc charFilter;
		ValidateFunc validateFunc;
		CompleteFunc completeFunc;
		LPVOID completeParam;

		Params()
			: minLength(1)
			, maxLength(25)
			, allowPaste(true)
			, charFilter(NULL)
			, validateFunc(NULL)
			, completeFunc(NULL)
			, completeParam(NULL)
		{}
	};

	static void Open(const Params &params);
	static void ForceCloseForParam(LPVOID lpParam);
	static bool IsOpen();
	static bool IsSuppressingMenuInput();
	static void Tick(Minecraft *minecraft);
	static void Render(Minecraft *minecraft);

	static bool IsAcceptedWorldNameChar(wchar_t ch);
	static bool ValidateWorldNameNotEmpty(const std::wstring &text, std::wstring &outError);
};
