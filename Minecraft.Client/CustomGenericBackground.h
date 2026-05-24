#pragma once

class Minecraft;

class CustomGenericBackground
{
public:
	enum
	{
		kPieceCount = 9
	};

	enum ETextureSet
	{
		eTextureSet_Standard = 0,
		eTextureSet_Compact,
		eTextureSet_Recessed,
		eTextureSet_Count
	};

	CustomGenericBackground();

	void Setup(float x, float y, float approxWidth, float approxHeight,
	           ETextureSet textureSet = eTextureSet_Standard);
	void SetPosition(float x, float y);
	void SetApproxSize(float approxWidth, float approxHeight);
	void SetTextureSet(ETextureSet textureSet);

	void Render(Minecraft *minecraft, int viewport);

	float GetX() const               { return m_x; }
	float GetY() const               { return m_y; }
	float GetWidth() const           { return m_resolvedWidth; }
	float GetHeight() const          { return m_resolvedHeight; }
	float GetRequestedWidth() const  { return m_requestedWidth; }
	float GetRequestedHeight() const { return m_requestedHeight; }
	bool  IsReady() const            { return m_ready; }

private:
	struct Piece
	{
		int textureId;
		int width;
		int height;
	};

	void ResetTextureState();
	void EnsureTextures(Minecraft *minecraft);
	void ResolveLayout();
	void DrawPiece(Minecraft *minecraft, int textureId,
	               float x, float y, float width, float height);
	int PickSegmentCount(float targetSize, int segmentSize) const;

	Piece m_pieces[kPieceCount];
	ETextureSet m_textureSet;
	bool m_checked;
	bool m_ready;
	bool m_layoutDirty;

	float m_x;
	float m_y;
	float m_requestedWidth;
	float m_requestedHeight;
	float m_resolvedWidth;
	float m_resolvedHeight;

	float m_leftWidth;
	float m_rightWidth;
	float m_topHeight;
	float m_bottomHeight;
	float m_innerWidth;
	float m_innerHeight;
	float m_segmentWidth;
	float m_segmentHeight;

	int m_segmentsX;
	int m_segmentsY;
};
