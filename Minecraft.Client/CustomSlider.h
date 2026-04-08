#pragma once

#include <string>

class Minecraft;
class Font;

class CustomSlider
{
public:
    CustomSlider();

    void Setup(float x, float y, int limitMax = 100);
    void Setup(float x, float y, float width, float height, int limitMax = 100);

    bool Update(Minecraft *minecraft);

    void Render(Minecraft *minecraft, Font *font, int stringId, int viewport);
    void RenderLabel(Minecraft *minecraft, Font *font, const std::wstring &label, int viewport);
    void SetValue(int value);

    int   GetValue()    const { return m_value; }
    float GetX()        const { return m_x; }
    float GetY()        const { return m_y; }
    float GetWidth()    const { return m_width; }
    float GetHeight()   const { return m_height; }
    bool  IsHovered()   const { return m_hovered; }
    bool  IsDragging()  const { return m_dragging; }

private:
    void EnsureTextures(Minecraft *minecraft);
    void DrawBorder(float x, float y, float w, float h, float thickness, unsigned int colour);

    static const wchar_t *kTrackPath;
    static const wchar_t *kHandlePath;
    static const wchar_t *kHoverBgPath;

    int  m_trackTexId;
    int  m_handleTexId;
    int  m_hoverBgTexId;
    bool m_checked;
    bool m_ready;

    float m_x, m_y;
    float m_width;
    float m_height;

    float m_handleW;
    float m_handleH;

    int m_limitMax;
    int m_value;

    bool m_hovered;
    bool m_wasHovered;
    bool m_dragging;
};