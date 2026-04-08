#pragma once

class UITTFFont
{
private:
	PBYTE pbData;
	string m_fontName;

public:
	UITTFFont(const string &path, const string &fontName, S32 fallbackCharacter);
	~UITTFFont();
};
