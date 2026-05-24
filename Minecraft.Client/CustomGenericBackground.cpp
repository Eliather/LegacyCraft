#include "stdafx.h"
#include "CustomGenericBackground.h"

#include "BufferedImage.h"
#include "Minecraft.h"
#include "Tesselator.h"
#include "Common\UI\UI.h"

#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif

namespace
{
	enum EPanelPiece
	{
		ePanelPiece_TL = 0,
		ePanelPiece_TM,
		ePanelPiece_TR,
		ePanelPiece_ML,
		ePanelPiece_MM,
		ePanelPiece_MR,
		ePanelPiece_BL,
		ePanelPiece_BM,
		ePanelPiece_BR,
		ePanelPiece_Count
	};

	const wchar_t *kTextureSetPaths[CustomGenericBackground::eTextureSet_Count][CustomGenericBackground::kPieceCount] =
	{
		{
			L"/Graphics/PanelsAndTabs/Panel_TL.png",
			L"/Graphics/PanelsAndTabs/Panel_TM.png",
			L"/Graphics/PanelsAndTabs/Panel_TR.png",
			L"/Graphics/PanelsAndTabs/Panel_ML.png",
			L"/Graphics/PanelsAndTabs/Panel_MM.png",
			L"/Graphics/PanelsAndTabs/Panel_MR.png",
			L"/Graphics/PanelsAndTabs/Panel_BL.png",
			L"/Graphics/PanelsAndTabs/Panel_BM.png",
			L"/Graphics/PanelsAndTabs/Panel_BR.png",
		},
		{
			L"/Graphics/PanelsAndTabs/Panel_Top_L.png",
			L"/Graphics/PanelsAndTabs/Panel_Top_M.png",
			L"/Graphics/PanelsAndTabs/Panel_Top_R.png",
			L"/Graphics/PanelsAndTabs/Panel_Mid_L.png",
			L"/Graphics/PanelsAndTabs/Panel_Mid_M.png",
			L"/Graphics/PanelsAndTabs/Panel_Mid_R.png",
			L"/Graphics/PanelsAndTabs/Panel_Bot_L.png",
			L"/Graphics/PanelsAndTabs/Panel_Bot_M.png",
			L"/Graphics/PanelsAndTabs/Panel_Bot_R.png",
		},
		{
			L"/Graphics/PanelsAndTabs/Panel_Recess_Top_L.png",
			L"/Graphics/PanelsAndTabs/Panel_Recess_Top_M.png",
			L"/Graphics/PanelsAndTabs/Panel_Recess_Top_R.png",
			L"/Graphics/PanelsAndTabs/Panel_Recess_Mid_L.png",
			L"/Graphics/PanelsAndTabs/Panel_Recess_Mid_M.png",
			L"/Graphics/PanelsAndTabs/Panel_Recess_Mid_R.png",
			L"/Graphics/PanelsAndTabs/Panel_Recess_Bot_L.png",
			L"/Graphics/PanelsAndTabs/Panel_Recess_Bot_M.png",
			L"/Graphics/PanelsAndTabs/Panel_Recess_Bot_R.png",
		}
	};

	int MaxInt(int a, int b)
	{
		return (a > b) ? a : b;
	}

	int Max3(int a, int b, int c)
	{
		return MaxInt(MaxInt(a, b), c);
	}

	float ClampMin(float value, float minimum)
	{
		return (value < minimum) ? minimum : value;
	}

	float AbsFloat(float value)
	{
		return (value < 0.0f) ? -value : value;
	}

	void DrawSolidQuad(Tesselator *t, float x0, float y0, float x1, float y1,
	                   int r, int g, int b, int a)
	{
		if(t == NULL) return;

		t->begin();
		t->color(r, g, b, a);
		t->vertex(x0, y1, 0.0f);
		t->vertex(x1, y1, 0.0f);
		t->vertex(x1, y0, 0.0f);
		t->vertex(x0, y0, 0.0f);
		t->end();
	}
}

CustomGenericBackground::CustomGenericBackground()
	: m_textureSet(eTextureSet_Standard)
	, m_checked(false)
	, m_ready(false)
	, m_layoutDirty(true)
	, m_x(0.0f)
	, m_y(0.0f)
	, m_requestedWidth(256.0f)
	, m_requestedHeight(256.0f)
	, m_resolvedWidth(256.0f)
	, m_resolvedHeight(256.0f)
	, m_leftWidth(0.0f)
	, m_rightWidth(0.0f)
	, m_topHeight(0.0f)
	, m_bottomHeight(0.0f)
	, m_innerWidth(0.0f)
	, m_innerHeight(0.0f)
	, m_segmentWidth(0.0f)
	, m_segmentHeight(0.0f)
	, m_segmentsX(1)
	, m_segmentsY(1)
{
	ResetTextureState();
}

void CustomGenericBackground::Setup(float x, float y, float approxWidth, float approxHeight,
                                    ETextureSet textureSet)
{
	SetTextureSet(textureSet);
	SetPosition(x, y);
	SetApproxSize(approxWidth, approxHeight);
}

void CustomGenericBackground::SetPosition(float x, float y)
{
	m_x = x;
	m_y = y;
}

void CustomGenericBackground::SetApproxSize(float approxWidth, float approxHeight)
{
	m_requestedWidth = (approxWidth > 0.0f) ? approxWidth : 1.0f;
	m_requestedHeight = (approxHeight > 0.0f) ? approxHeight : 1.0f;
	m_resolvedWidth = m_requestedWidth;
	m_resolvedHeight = m_requestedHeight;
	m_layoutDirty = true;
}

void CustomGenericBackground::SetTextureSet(ETextureSet textureSet)
{
	if(textureSet < eTextureSet_Standard || textureSet >= eTextureSet_Count)
	{
		textureSet = eTextureSet_Standard;
	}

	if(m_textureSet == textureSet)
	{
		return;
	}

	m_textureSet = textureSet;
	ResetTextureState();
}

void CustomGenericBackground::ResetTextureState()
{
	for(int i = 0; i < kPieceCount; ++i)
	{
		m_pieces[i].textureId = -1;
		m_pieces[i].width = 0;
		m_pieces[i].height = 0;
	}

	m_checked = false;
	m_ready = false;
	m_layoutDirty = true;
}

void CustomGenericBackground::EnsureTextures(Minecraft *minecraft)
{
	if(m_ready || m_checked) return;
	if(minecraft == NULL || minecraft->textures == NULL) return;

	m_checked = true;
	bool allLoaded = true;

	for(int i = 0; i < kPieceCount; ++i)
	{
		m_pieces[i].textureId = -1;
		m_pieces[i].width = 0;
		m_pieces[i].height = 0;

		const wchar_t *path = kTextureSetPaths[m_textureSet][i];
		if(path == NULL)
		{
			allLoaded = false;
			continue;
		}

		BufferedImage *image = new BufferedImage(path);
		if(image != NULL && image->getData() != NULL &&
		   image->getWidth() > 0 && image->getHeight() > 0)
		{
			m_pieces[i].width = image->getWidth();
			m_pieces[i].height = image->getHeight();
			m_pieces[i].textureId = minecraft->textures->getTexture(image);
		}
		delete image;

		if(m_pieces[i].textureId < 0)
		{
			allLoaded = false;
		}
	}

	m_ready = allLoaded;
	if(!m_ready)
	{
		m_checked = false;
		return;
	}

	m_layoutDirty = true;
}

int CustomGenericBackground::PickSegmentCount(float targetSize, int segmentSize) const
{
	if(segmentSize <= 0 || targetSize <= 0.0f)
	{
		return 1;
	}

	const float idealCount = targetSize / (float)segmentSize;
	int lowerCount = (int)idealCount;
	if(lowerCount < 1)
	{
		lowerCount = 1;
	}

	int upperCount = lowerCount;
	if((float)upperCount < idealCount)
	{
		++upperCount;
	}

	const float lowerScale = targetSize / ((float)lowerCount * (float)segmentSize);
	const float upperScale = targetSize / ((float)upperCount * (float)segmentSize);
	const float lowerError = AbsFloat(lowerScale - 1.0f);
	const float upperError = AbsFloat(upperScale - 1.0f);

	return (lowerError <= upperError) ? lowerCount : upperCount;
}

void CustomGenericBackground::ResolveLayout()
{
	if(!m_ready)
	{
		m_resolvedWidth = m_requestedWidth;
		m_resolvedHeight = m_requestedHeight;
		return;
	}

	const int leftWidth = Max3(
		m_pieces[ePanelPiece_TL].width,
		m_pieces[ePanelPiece_ML].width,
		m_pieces[ePanelPiece_BL].width);
	const int rightWidth = Max3(
		m_pieces[ePanelPiece_TR].width,
		m_pieces[ePanelPiece_MR].width,
		m_pieces[ePanelPiece_BR].width);
	const int topHeight = Max3(
		m_pieces[ePanelPiece_TL].height,
		m_pieces[ePanelPiece_TM].height,
		m_pieces[ePanelPiece_TR].height);
	const int bottomHeight = Max3(
		m_pieces[ePanelPiece_BL].height,
		m_pieces[ePanelPiece_BM].height,
		m_pieces[ePanelPiece_BR].height);
	const int tileWidth = Max3(
		m_pieces[ePanelPiece_TM].width,
		m_pieces[ePanelPiece_MM].width,
		m_pieces[ePanelPiece_BM].width);
	const int tileHeight = Max3(
		m_pieces[ePanelPiece_ML].height,
		m_pieces[ePanelPiece_MM].height,
		m_pieces[ePanelPiece_MR].height);

	m_leftWidth = (float)leftWidth;
	m_rightWidth = (float)rightWidth;
	m_topHeight = (float)topHeight;
	m_bottomHeight = (float)bottomHeight;

	const float minWidth = m_leftWidth + m_rightWidth + (float)tileWidth;
	const float minHeight = m_topHeight + m_bottomHeight + (float)tileHeight;

	m_resolvedWidth = ClampMin(m_requestedWidth, minWidth);
	m_resolvedHeight = ClampMin(m_requestedHeight, minHeight);

	m_innerWidth = m_resolvedWidth - m_leftWidth - m_rightWidth;
	m_innerHeight = m_resolvedHeight - m_topHeight - m_bottomHeight;

	m_segmentsX = PickSegmentCount(m_innerWidth, tileWidth);
	m_segmentsY = PickSegmentCount(m_innerHeight, tileHeight);

	m_segmentWidth = m_innerWidth / (float)m_segmentsX;
	m_segmentHeight = m_innerHeight / (float)m_segmentsY;

	m_layoutDirty = false;
}

void CustomGenericBackground::DrawPiece(Minecraft *minecraft, int textureId,
                                        float x, float y, float width, float height)
{
	if(minecraft == NULL || minecraft->textures == NULL) return;
	if(textureId < 0 || width <= 0.0f || height <= 0.0f) return;

	minecraft->textures->bind(textureId);
	Tesselator *t = Tesselator::getInstance();
	t->begin();
	t->color(0xffffff);
	t->vertexUV(x,         y + height, 0.0f, 0.0f, 1.0f);
	t->vertexUV(x + width, y + height, 0.0f, 1.0f, 1.0f);
	t->vertexUV(x + width, y,          0.0f, 1.0f, 0.0f);
	t->vertexUV(x,         y,          0.0f, 0.0f, 0.0f);
	t->end();
}

void CustomGenericBackground::Render(Minecraft *minecraft, int viewport)
{
	EnsureTextures(minecraft);
	if(minecraft == NULL || minecraft->textures == NULL) return;

	if(m_ready && m_layoutDirty)
	{
		ResolveLayout();
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

	if(!m_ready)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_TEXTURE_2D);

		Tesselator *t = Tesselator::getInstance();
		const float x0 = m_x;
		const float y0 = m_y;
		const float x1 = m_x + m_resolvedWidth;
		const float y1 = m_y + m_resolvedHeight;

		DrawSolidQuad(t, x0 + 2.0f, y0 + 2.0f, x1 + 2.0f, y1 + 2.0f, 0, 0, 0, 64);
		DrawSolidQuad(t, x0, y0, x1, y1, 36, 36, 36, 168);
		DrawSolidQuad(t, x0 + 2.0f, y0 + 2.0f, x1 - 2.0f, y1 - 2.0f, 198, 198, 198, 220);
		DrawSolidQuad(t, x0 + 2.0f, y0 + 2.0f, x1 - 2.0f, y0 + 4.0f, 255, 255, 255, 220);
		DrawSolidQuad(t, x0 + 2.0f, y1 - 4.0f, x1 - 2.0f, y1 - 2.0f, 0, 0, 0, 180);

		glEnable(GL_TEXTURE_2D);
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	const float leftX = m_x;
	const float innerX = m_x + m_leftWidth;
	const float rightX = m_x + m_resolvedWidth - m_rightWidth;
	const float topY = m_y;
	const float innerY = m_y + m_topHeight;
	const float bottomY = m_y + m_resolvedHeight - m_bottomHeight;

	DrawPiece(minecraft, m_pieces[ePanelPiece_TL].textureId, leftX, topY, m_leftWidth, m_topHeight);
	DrawPiece(minecraft, m_pieces[ePanelPiece_TR].textureId, rightX, topY, m_rightWidth, m_topHeight);
	DrawPiece(minecraft, m_pieces[ePanelPiece_BL].textureId, leftX, bottomY, m_leftWidth, m_bottomHeight);
	DrawPiece(minecraft, m_pieces[ePanelPiece_BR].textureId, rightX, bottomY, m_rightWidth, m_bottomHeight);

	for(int x = 0; x < m_segmentsX; ++x)
	{
		const float drawX = innerX + ((float)x * m_segmentWidth);
		DrawPiece(minecraft, m_pieces[ePanelPiece_TM].textureId, drawX, topY, m_segmentWidth, m_topHeight);
		DrawPiece(minecraft, m_pieces[ePanelPiece_BM].textureId, drawX, bottomY, m_segmentWidth, m_bottomHeight);
	}

	for(int y = 0; y < m_segmentsY; ++y)
	{
		const float drawY = innerY + ((float)y * m_segmentHeight);
		DrawPiece(minecraft, m_pieces[ePanelPiece_ML].textureId, leftX, drawY, m_leftWidth, m_segmentHeight);
		DrawPiece(minecraft, m_pieces[ePanelPiece_MR].textureId, rightX, drawY, m_rightWidth, m_segmentHeight);
	}

	for(int y = 0; y < m_segmentsY; ++y)
	{
		const float drawY = innerY + ((float)y * m_segmentHeight);
		for(int x = 0; x < m_segmentsX; ++x)
		{
			const float drawX = innerX + ((float)x * m_segmentWidth);
			DrawPiece(minecraft, m_pieces[ePanelPiece_MM].textureId, drawX, drawY, m_segmentWidth, m_segmentHeight);
		}
	}

	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
