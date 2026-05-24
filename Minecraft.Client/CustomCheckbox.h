#pragma once

#include <string>

class Minecraft;
class Font;

class CustomCheckbox
{
public:
	CustomCheckbox();

	void Setup(float x, float y, float width = 0.0f, float height = 0.0f, int stringId = -1);

	bool HitTest(float mouseX, float mouseY) const;
	bool Update(Minecraft *minecraft);

	void Render(Minecraft *minecraft, int viewport);
	void Render(Minecraft *minecraft, Font *font, int viewport);
	void Render(Minecraft *minecraft, Font *font, const std::wstring &label, int viewport);

	void SetChecked(bool checked) { m_checked = checked; }
	void SetStringId(int stringId) { m_stringId = stringId; }
	bool IsChecked() const        { return m_checked; }
	bool IsHovered() const        { return m_hovered; }
	int  GetStringId() const      { return m_stringId; }
	float GetX() const            { return m_x; }
	float GetY() const            { return m_y; }
	float GetWidth() const        { return m_width; }
	float GetHeight() const       { return m_height; }

private:
	void EnsureTextures(Minecraft *minecraft);
	void Draw(Minecraft *minecraft, Font *font, const std::wstring *label, int viewport);

	int  m_normalTexId;
	int  m_hoverTexId;
	int  m_tickTexId;
	int  m_boxTextureWidth;
	int  m_boxTextureHeight;
	int  m_tickTextureWidth;
	int  m_tickTextureHeight;
	bool m_checkedTextures;
	bool m_ready;

	float m_x;
	float m_y;
	float m_width;
	float m_height;
	int   m_stringId;

	bool m_checked;
	bool m_hovered;
	bool m_wasHovered;
	bool m_wasPressed;
};
