#include "stdafx.h"
#include "CustomCheckbox.h"

#include "BufferedImage.h"
#include "Minecraft.h"
#include "Tesselator.h"
#include "Common\UI\UI.h"

#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif

namespace
{
	const wchar_t *kTickboxNormalPath = L"/Graphics/Tickbox_Norm.png";
	const wchar_t *kTickboxHoverPath  = L"/Graphics/Tickbox_Over.png";
	const wchar_t *kTickPath          = L"/Graphics/Tick.png";

	const float kDefaultTickboxSize = 24.0f;
}

CustomCheckbox::CustomCheckbox()
	: m_normalTexId(-1)
	, m_hoverTexId(-1)
	, m_tickTexId(-1)
	, m_boxTextureWidth(0)
	, m_boxTextureHeight(0)
	, m_tickTextureWidth(0)
	, m_tickTextureHeight(0)
	, m_checkedTextures(false)
	, m_ready(false)
	, m_x(0.0f)
	, m_y(0.0f)
	, m_width(kDefaultTickboxSize)
	, m_height(kDefaultTickboxSize)
	, m_stringId(-1)
	, m_checked(false)
	, m_hovered(false)
	, m_wasHovered(false)
	, m_wasPressed(false)
{
}

void CustomCheckbox::Setup(float x, float y, float width, float height, int stringId)
{
	m_x = x;
	m_y = y;
	m_width = (width > 0.0f) ? width : kDefaultTickboxSize;
	m_height = (height > 0.0f) ? height : kDefaultTickboxSize;
	m_stringId = stringId;
}

bool CustomCheckbox::HitTest(float mouseX, float mouseY) const
{
	return (mouseX >= m_x && mouseX <= (m_x + m_width) &&
	        mouseY >= m_y && mouseY <= (m_y + m_height));
}

void CustomCheckbox::EnsureTextures(Minecraft *minecraft)
{
	if(m_ready || m_checkedTextures) return;
	if(minecraft == NULL || minecraft->textures == NULL) return;

	m_checkedTextures = true;

	BufferedImage *normalImage = new BufferedImage(kTickboxNormalPath);
	if(normalImage != NULL && normalImage->getData() != NULL &&
	   normalImage->getWidth() > 0 && normalImage->getHeight() > 0)
	{
		m_boxTextureWidth = normalImage->getWidth();
		m_boxTextureHeight = normalImage->getHeight();
		m_normalTexId = minecraft->textures->getTexture(normalImage);
	}
	delete normalImage;

	BufferedImage *hoverImage = new BufferedImage(kTickboxHoverPath);
	if(hoverImage != NULL && hoverImage->getData() != NULL &&
	   hoverImage->getWidth() > 0 && hoverImage->getHeight() > 0)
	{
		m_hoverTexId = minecraft->textures->getTexture(hoverImage);
	}
	delete hoverImage;

	BufferedImage *tickImage = new BufferedImage(kTickPath);
	if(tickImage != NULL && tickImage->getData() != NULL &&
	   tickImage->getWidth() > 0 && tickImage->getHeight() > 0)
	{
		m_tickTextureWidth = tickImage->getWidth();
		m_tickTextureHeight = tickImage->getHeight();
		m_tickTexId = minecraft->textures->getTexture(tickImage);
	}
	delete tickImage;

	if(m_hoverTexId < 0)
	{
		m_hoverTexId = m_normalTexId;
	}

	m_ready = (m_normalTexId >= 0 && m_tickTexId >= 0);
	if(!m_ready)
	{
		m_checkedTextures = false;
	}
}

bool CustomCheckbox::Update(Minecraft *minecraft)
{
	if(minecraft == NULL) return false;

	const float mouseX = ((float)Mouse::getX() / 1920.0f) * (float)minecraft->width_phys;
	const float mouseY = ((float)Mouse::getY() / 1080.0f) * (float)minecraft->height_phys;

	m_hovered = HitTest(mouseX, mouseY);
	if(m_hovered && !m_wasHovered)
	{
		ui.PlayUISFX(eSFX_Focus);
	}
	m_wasHovered = m_hovered;

	const bool isPressed = Mouse::isButtonPressed(0);
	const bool clicked = m_hovered && isPressed && !m_wasPressed;
	m_wasPressed = isPressed;

	if(clicked)
	{
		m_checked = !m_checked;
		ui.PlayUISFX(eSFX_Press);
		return true;
	}

	return false;
}

void CustomCheckbox::Render(Minecraft *minecraft, int viewport)
{
	Draw(minecraft, NULL, NULL, viewport);
}

void CustomCheckbox::Render(Minecraft *minecraft, Font *font, int viewport)
{
	Draw(minecraft, font, NULL, viewport);
}

void CustomCheckbox::Render(Minecraft *minecraft, Font *font, const std::wstring &label, int viewport)
{
	Draw(minecraft, font, &label, viewport);
}

void CustomCheckbox::Draw(Minecraft *minecraft, Font *font, const std::wstring *label, int viewport)
{
	EnsureTextures(minecraft);
	if(!m_ready || minecraft == NULL || minecraft->textures == NULL) return;

	std::wstring resolvedLabel;
	const std::wstring *labelToDraw = label;
	if(labelToDraw == NULL && m_stringId >= 0)
	{
		const wchar_t *localised = app.GetString(m_stringId);
		if(localised != NULL && localised[0] != 0)
		{
			resolvedLabel = localised;
			labelToDraw = &resolvedLabel;
		}
	}

	ui.setupRenderPosition((C4JRender::eViewportType)viewport);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glDisable(GL_FOG);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.0f);
	glEnable(GL_TEXTURE_2D);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0, (double)minecraft->width_phys, (double)minecraft->height_phys, 0.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	Tesselator *t = Tesselator::getInstance();

	minecraft->textures->bind(m_hovered ? m_hoverTexId : m_normalTexId);
	t->begin();
	t->color(0xffffff);
	t->vertexUV(m_x,           m_y + m_height, 0.0f, 0.0f, 1.0f);
	t->vertexUV(m_x + m_width, m_y + m_height, 0.0f, 1.0f, 1.0f);
	t->vertexUV(m_x + m_width, m_y,            0.0f, 1.0f, 0.0f);
	t->vertexUV(m_x,           m_y,            0.0f, 0.0f, 0.0f);
	t->end();

	if(m_checked)
	{
		const float drawTickWidth =
			(m_boxTextureWidth > 0) ? ((float)m_tickTextureWidth * (m_width / (float)m_boxTextureWidth)) : m_width;
		const float drawTickHeight =
			(m_boxTextureHeight > 0) ? ((float)m_tickTextureHeight * (m_height / (float)m_boxTextureHeight)) : m_height;
		const float tickX = m_x + ((m_width - drawTickWidth) * 0.5f);
		const float tickY = m_y + ((m_height - drawTickHeight) * 0.5f);

		minecraft->textures->bind(m_tickTexId);
		t->begin();
		t->color(0xffffff);
		t->vertexUV(tickX,                 tickY + drawTickHeight, 0.0f, 0.0f, 1.0f);
		t->vertexUV(tickX + drawTickWidth, tickY + drawTickHeight, 0.0f, 1.0f, 1.0f);
		t->vertexUV(tickX + drawTickWidth, tickY,                  0.0f, 1.0f, 0.0f);
		t->vertexUV(tickX,                 tickY,                  0.0f, 0.0f, 0.0f);
		t->end();
	}

	if(font != NULL && labelToDraw != NULL && !labelToDraw->empty())
	{
		const float textScale = 2.0f;
		const float textX = m_x + m_width + 12.0f;
		const float textY = m_y + ((m_height - 8.0f * textScale) * 0.5f);
		const unsigned int textColor = m_hovered ? 0xffffff00 : 0xffffffff;

		glPushMatrix();
		glTranslatef(textX, textY, 0.0f);
		glScalef(textScale, textScale, 1.0f);
		font->draw(*labelToDraw, 1.0f, 1.0f, 0xff000000);
		font->draw(*labelToDraw, 0.0f, 0.0f, textColor);
		glPopMatrix();
	}

	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
