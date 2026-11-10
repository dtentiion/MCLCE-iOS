// Camera.cpp, apart from where prepare gets its matrices. Upstream inverts
// modelview * projection with DirectXMath, which iOS doesn't have, so the
// matrices come from the GL bridge and get inverted here.

#include <cmath>

#include "../../../upstream/Minecraft.World/Player.h"
#include "../../../upstream/Minecraft.Client/Camera.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/Tile.h"
#include "../../../upstream/Minecraft.World/LiquidTile.h"
#include "../../../upstream/Minecraft.World/Material.h"
#include "../../../upstream/Minecraft.World/TilePos.h"
#include "../../../upstream/Minecraft.World/Vec3.h"

extern "C" void mcle_glbridge_get_modelview(float *out16);
extern "C" void mcle_glbridge_get_projection(float *out16);

float Camera::xPlayerOffs = 0.0f;
float Camera::yPlayerOffs = 0.0f;
float Camera::zPlayerOffs = 0.0f;

float Camera::xa = 0.0f;
float Camera::ya = 0.0f;
float Camera::za = 0.0f;
float Camera::xa2 = 0.0f;
float Camera::za2 = 0.0f;

namespace {

// Column major, out = a * b
void multiply(const float *a, const float *b, float *out) {
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) sum += a[k * 4 + row] * b[col * 4 + k];
            out[col * 4 + row] = sum;
        }
    }
}

// Cofactor inverse, false if m can't be inverted
bool invert(const float *m, float *out) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
    return true;
}

} // namespace

void Camera::prepare(shared_ptr<Player> player, bool mirror)
{
    float modelview[16], projection[16];
    mcle_glbridge_get_modelview(modelview);
    mcle_glbridge_get_projection(projection);

    // Where clip space (0,0,0) is relative to the player: the translation
    // of the inverted projection * modelview
    float combined[16], inverted[16];
    multiply(projection, modelview, combined);
    if (invert(combined, inverted) && inverted[15] != 0.0f) {
        xPlayerOffs = inverted[12] / inverted[15];
        yPlayerOffs = inverted[13] / inverted[15];
        zPlayerOffs = inverted[14] / inverted[15];
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

TilePos *Camera::getCameraTilePos(shared_ptr<LivingEntity> player, double alpha)
{
    return new TilePos(getCameraPos(player, alpha));
}

Vec3 *Camera::getCameraPos(shared_ptr<LivingEntity> player, double alpha)
{
    double xx = player->xo + (player->x - player->xo) * alpha;
    // The client's player has its y at the eyes and Player::getHeadHeight's
    // 0.12 goes on top. This one's is at its feet, and the way up to the
    // eyes is already in yPlayerOffs.
    double yy = player->yo + (player->y - player->yo) * alpha + 0.12f;
    double zz = player->zo + (player->z - player->zo) * alpha;

    double xt = xx + Camera::xPlayerOffs * 1;
    double yt = yy + Camera::yPlayerOffs * 1;
    double zt = zz + Camera::zPlayerOffs * 1;

    return Vec3::newTemp(xt, yt, zt);
}

int Camera::getBlockAt(Level *level, shared_ptr<LivingEntity> player, float alpha)
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
