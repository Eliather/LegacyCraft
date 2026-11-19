#include "stdafx.h"
#include "Camera.h"
#include "MemoryTracker.h"
#include "..\Minecraft.World\net.minecraft.world.entity.player.h"
#include "..\Minecraft.World\net.minecraft.world.level.h"
#include "..\Minecraft.World\net.minecraft.world.level.tile.h"
#include "..\Minecraft.World\TilePos.h"

float Camera::xPlayerOffs = 0.0f;
float Camera::yPlayerOffs = 0.0f;
float Camera::zPlayerOffs = 0.0f;

FloatBuffer *Camera::modelview	= MemoryTracker::createFloatBuffer(16);
FloatBuffer *Camera::projection = MemoryTracker::createFloatBuffer(16);

float Camera::xa = 0.0f;
float Camera::ya = 0.0f;
float Camera::za = 0.0f;
float Camera::xa2 = 0.0f;
float Camera::za2 = 0.0f;

static bool InvertMatrix4x4(const float m[16], float invOut[16])
{
    float inv[16], det;
    inv[0] = m[5]  * m[10] * m[15] - m[5]  * m[11] * m[14] - m[9]  * m[6]  * m[15] + m[9]  * m[7]  * m[14] + m[13] * m[6]  * m[11] - m[13] * m[7]  * m[10];
    inv[4] = -m[4]  * m[10] * m[15] + m[4]  * m[11] * m[14] + m[8]  * m[6]  * m[15] - m[8]  * m[7]  * m[14] - m[12] * m[6]  * m[11] + m[12] * m[7]  * m[10];
    inv[8] = m[4]  * m[9] * m[15] - m[4]  * m[11] * m[13] - m[8]  * m[5] * m[15] + m[8]  * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4]  * m[9] * m[14] + m[4]  * m[10] * m[13] + m[8]  * m[5] * m[14] - m[8]  * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1]  * m[10] * m[15] + m[1]  * m[11] * m[14] + m[9]  * m[2] * m[15] - m[9]  * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0]  * m[10] * m[15] - m[0]  * m[11] * m[14] - m[8]  * m[2] * m[15] + m[8]  * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0]  * m[9] * m[15] + m[0]  * m[11] * m[13] + m[8]  * m[1] * m[15] - m[8]  * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0]  * m[9] * m[14] - m[0]  * m[10] * m[13] - m[8]  * m[1] * m[14] + m[8]  * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1]  * m[6] * m[15] - m[1]  * m[7] * m[14] - m[5]  * m[2] * m[15] + m[5]  * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0]  * m[6] * m[15] + m[0]  * m[7] * m[14] + m[4]  * m[2] * m[15] - m[4]  * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0]  * m[5] * m[15] - m[0]  * m[7] * m[13] - m[4]  * m[1] * m[15] + m[4]  * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0]  * m[5] * m[14] + m[0]  * m[6] * m[13] + m[4]  * m[1] * m[14] - m[4]  * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) invOut[i] = inv[i] * det;
    return true;
}

static void MultiplyMatrix4x4(const float a[16], const float b[16], float out[16])
{
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] +
                             a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] +
                             a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

void Camera::prepare(shared_ptr<Player> player, bool mirror)
{
    glGetFloat(GL_MODELVIEW_MATRIX, modelview);
    glGetFloat(GL_PROJECTION_MATRIX, projection);

	float _modelview[16], _proj[16], _final[16], _invert[16];
	memcpy( _modelview, modelview->_getDataPointer(), 64 );
	memcpy( _proj, projection->_getDataPointer(), 64 );

	MultiplyMatrix4x4( _modelview, _proj, _final );
	if (InvertMatrix4x4(_final, _invert))
	{
		float transX = _invert[12];
		float transY = _invert[13];
		float transZ = _invert[14];
		float transW = _invert[15];

		if (transW != 0.0f)
		{
			xPlayerOffs = transX / transW;
			yPlayerOffs = transY / transW;
			zPlayerOffs = transZ / transW;
		}
	}

    int flipCamera = mirror ? 1 : 0;

    float xRot = player->xRot;
    float yRot = player->yRot;

    xa = cosf(yRot * PI / 180.0f) * (1 - flipCamera * 2);
    za = sinf(yRot * PI / 180.0f) * (1 - flipCamera * 2);

    xa2 = -za * sinf(xRot * PI / 180.0f) * (1 - flipCamera * 2);
    za2 = xa * sinf(xRot * PI / 180.0f) * (1 - flipCamera * 2);
    ya = cosf(xRot * PI / 180.0f);
}

TilePos *Camera::getCameraTilePos(shared_ptr<Mob> player, double alpha)
{
	return new TilePos(getCameraPos(player, alpha));
}

Vec3 *Camera::getCameraPos(shared_ptr<Mob> player, double alpha)
{
    double xx = player->xo + (player->x - player->xo) * alpha;
    double yy = player->yo + (player->y - player->yo) * alpha + player->getHeadHeight();
    double zz = player->zo + (player->z - player->zo) * alpha;

    double xt = xx + Camera::xPlayerOffs * 1;
    double yt = yy + Camera::yPlayerOffs * 1;
    double zt = zz + Camera::zPlayerOffs * 1;

    return Vec3::newTemp(xt, yt, zt);
}

int Camera::getBlockAt(Level *level, shared_ptr<Mob> player, float alpha)
{
    Vec3 *p = Camera::getCameraPos(player, alpha);
    TilePos tp = TilePos(p);
    int t = level->getTile(tp.x, tp.y, tp.z);
    if (t != 0 && Tile::tiles[t]->material->isLiquid())
	{
        float hh = LiquidTile::getHeight(level->getData(tp.x, tp.y, tp.z)) - 1 / 9.0f;
        float h = tp.y + 1 - hh;
        if (p->y >= h)
		{
            t = level->getTile(tp.x, tp.y + 1, tp.z);
        }
    }
    return t;
}
