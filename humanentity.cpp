#include "humanentity.h"

#include <cstdlib>
#include <vector>
#include <cmath>

#include "gl.h"
#include "world.h"
#include "terrain.h"
#include "fastmath.h"
#include "grounddrops.h"

#include "textures/items.h"
#include "textures/steve.h"

// ─── static member definitions ───────────────────────────────────────────────
const GLFix HumanEntity::WIDTH  = GLFix(77);  // ~0.6 * BLOCK_SIZE(128)
const GLFix HumanEntity::HEIGHT = GLFix(230); // ~1.8 * BLOCK_SIZE(128)

// ─── globals ─────────────────────────────────────────────────────────────────
std::vector<HumanEntity> human_entities;

const TEXTURE *humanSkinTexture() { return &steve_tex; }

// ─── UV helpers ──────────────────────────────────────────────────────────────
// Pixel-absolute UV coordinates into the 64×64 steve skin.
static inline TextureAtlasEntry skinArea(int u, int v, int w, int h)
{
    return { (unsigned)u, (unsigned)(u + w), (unsigned)v, (unsigned)(v + h) };
}

static inline TextureAtlasEntry mirrorU(TextureAtlasEntry t)
{
    unsigned tmp = t.left; t.left = t.right; t.right = tmp;
    return t;
}

// ─── constructors ────────────────────────────────────────────────────────────
HumanEntity::HumanEntity()
    : x(0), y(GLFix(World::HEIGHT * Chunk::SIZE) * BLOCK_SIZE), z(0),
      vx(0), vy(0), vz(0),
      yaw(0), walk_timer(0), swing_intensity(0),
      health(20), hurt_time(0), hurt_resistant(0), death_time(0),
      dir_timer(60), on_ground(false), loot_spawned(false)
{
    aabb = { x - WIDTH/2, y, z - WIDTH/2,
             x + WIDTH/2, y + HEIGHT, z + WIDTH/2 };
}

HumanEntity::HumanEntity(GLFix px, GLFix py, GLFix pz)
    : x(px), y(py), z(pz),
      vx(0), vy(0), vz(0),
      yaw(GLFix(rand() % 360)), walk_timer(0), swing_intensity(0),
      health(20), hurt_time(0), hurt_resistant(0), death_time(0),
      dir_timer(rand() % 60), on_ground(false), loot_spawned(false)
{
    aabb = { x - WIDTH/2, y, z - WIDTH/2,
             x + WIDTH/2, y + HEIGHT, z + WIDTH/2 };
}

// ─── update ──────────────────────────────────────────────────────────────────
void HumanEntity::update()
{
    if(hurt_time > 0)
        --hurt_time;
    if(hurt_resistant > 0)
        --hurt_resistant;

    const bool dead = health <= 0;
    if(dead && !loot_spawned)
    {
        loot_spawned = true;
        const unsigned int n = 1u + static_cast<unsigned int>(rand() % 2);
        spawnWorldDrop(x, y, z,
                       getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::ROTTEN_FLESH)), n);
    }
    if(dead)
    {
        if(death_time <= 20)
            ++death_time;
        vx *= GLFix(0.92f);
        vz *= GLFix(0.92f);
    }
    else
    {
        // ── AI: random direction changes ──────────────────────────────
        if(--dir_timer <= 0)
        {
            int r = rand() % 8;
            const GLFix speed(4);
            if(r < 6)
            {
                yaw = GLFix(rand() % 360);
                vx = GLFix(fast_sin(yaw)) * speed;
                vz = GLFix(fast_cos(yaw)) * speed;
            }
            else
            {
                vx = 0;
                vz = 0;
            }
            dir_timer = 40 + rand() % 80;
        }
    }

    GLFix old_x = x;
    GLFix old_z = z;

    if(!world.intersect(aabb))
    {
        AABB moved = aabb;
        moved.low_x += vx;
        moved.high_x += vx;
        if(!world.intersect(moved))
        {
            x += vx;
            aabb = moved;
        }
        else
        {
            vx = -vx;
            dir_timer = 0;
        }

        moved = aabb;
        moved.low_z += vz;
        moved.high_z += vz;
        if(!world.intersect(moved))
        {
            z += vz;
            aabb = moved;
        }
        else
        {
            vz = -vz;
            dir_timer = 0;
        }

        AABB moved_y = aabb;
        moved_y.low_y += vy;
        moved_y.high_y += vy;
        bool hit = world.intersect(moved_y);
        if(!hit)
        {
            y += vy;
            aabb = moved_y;
            on_ground = false;
        }
        else
        {
            if(vy < GLFix(0))
                on_ground = true;
            vy = 0;
        }
        vy -= GLFix(5);

        if(!dead && on_ground && (vx != GLFix(0) || vz != GLFix(0)) && (rand() % 80 == 0))
        {
            vy = GLFix(50);
            on_ground = false;
        }
    }

    aabb = { x - WIDTH/2, y, z - WIDTH/2, x + WIDTH/2, y + HEIGHT, z + WIDTH/2 };

    if(dead)
    {
        swing_intensity *= GLFix(0.85f);
        return;
    }

    GLFix dx = x - old_x;
    GLFix dz = z - old_z;
    GLFix actual_dist = GLFix(std::sqrt((float)(dx * dx + dz * dz)));
    GLFix target_amp = actual_dist * GLFix(0.33f);
    if(target_amp > GLFix(1))
        target_amp = GLFix(1);
    swing_intensity += (target_amp - swing_intensity) * GLFix(0.4f);

    GLFix horizontal_speed = GLFix(std::sqrt((float)(vx * vx + vz * vz)));
    if(horizontal_speed > GLFix(0))
    {
        walk_timer += horizontal_speed * GLFix(1.66f);
        walk_timer.normaliseAngle();
    }
}

// ─── rendering ───────────────────────────────────────────────────────────────
//
// Minecraft biped UV on 64×64 skin (ModelBiped / ModelPlayer source):
//
//  Part          UV offset  box dims (w×h×d in model px)
//  Head          ( 0, 0)    8×8×8
//  Body          (16,16)    8×12×4
//  Right Arm     (40,16)    4×12×4
//  Left Arm      (32,48)    4×12×4  (mirrored)
//  Right Leg     ( 0,16)    4×12×4
//  Left Leg      (16,48)    4×12×4  (mirrored)
//
// Box UV wrapping (UV offset u0,v0; box w×h×d pixels):
//   top    : (u0+d,    v0,   w,  d)
//   bottom : (u0+d+w,  v0,   w,  d)
//   right  : (u0,      v0+d, d,  h)
//   front  : (u0+d,    v0+d, w,  h)
//   left   : (u0+d+w,  v0+d, d,  h)
//   back   : (u0+d+w+d,v0+d, w,  h)

// Emit one textured quad (4 vertices, CCW winding from front)
static void emitQuad(
    GLFix ax, GLFix ay, GLFix az,
    GLFix bx, GLFix by, GLFix bz,
    GLFix cx, GLFix cy, GLFix cz,
    GLFix dx, GLFix dy, GLFix dz,
    const TextureAtlasEntry &tex)
{
    const COLOR flags = TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE;
    nglAddVertex({ ax, ay, az, GLFix((int)tex.left),  GLFix((int)tex.bottom), flags });
    nglAddVertex({ bx, by, bz, GLFix((int)tex.left),  GLFix((int)tex.top),    flags });
    nglAddVertex({ cx, cy, cz, GLFix((int)tex.right), GLFix((int)tex.top),    flags });
    nglAddVertex({ dx, dy, dz, GLFix((int)tex.right), GLFix((int)tex.bottom), flags });
}

// Draw all 6 faces of a biped box.
// bx/by/bz  – min local-space corner (nGL units)
// bw/bh/bd  – box dimensions (nGL units)
// u0/v0     – UV origin in pixel-space on the 64×64 skin
// wp/hp/dp  – box dimensions in texture pixels
// mirror    – flip horizontally (for left-side limbs)
static void drawBipedBox(
    GLFix bx, GLFix by, GLFix bz,
    GLFix bw, GLFix bh, GLFix bd,
    int u0, int v0, int wp, int hp, int dp,
    bool mirror = false)
{
    auto top  = skinArea(u0+dp,      v0,    wp, dp);
    auto bot  = skinArea(u0+dp+wp,   v0,    wp, dp);
    auto rgt  = skinArea(u0,         v0+dp, dp, hp);
    auto frt  = skinArea(u0+dp,      v0+dp, wp, hp);
    auto lft  = skinArea(u0+dp+wp,   v0+dp, dp, hp);
    auto bck  = skinArea(u0+dp+wp+dp,v0+dp, wp, hp);

    if (mirror)
    {
        top = mirrorU(top); bot = mirrorU(bot);
        // swap right↔left panels and flip each
        auto tmp = mirrorU(rgt); rgt = mirrorU(lft); lft = tmp;
        frt = mirrorU(frt); bck = mirrorU(bck);
    }

    GLFix x0=bx, x1=bx+bw;
    GLFix y0=by, y1=by+bh;
    GLFix z0=bz, z1=bz+bd;

    // Front  (-z face, normal towards -z)
    emitQuad(x0,y0,z0, x0,y1,z0, x1,y1,z0, x1,y0,z0, frt);
    // Back   (+z face)
    emitQuad(x1,y0,z1, x1,y1,z1, x0,y1,z1, x0,y0,z1, bck);
    // Right  (-x face)
    emitQuad(x0,y0,z1, x0,y1,z1, x0,y1,z0, x0,y0,z0, rgt);
    // Left   (+x face)
    emitQuad(x1,y0,z0, x1,y1,z0, x1,y1,z1, x1,y0,z1, lft);
    // Top
    emitQuad(x0,y1,z0, x0,y1,z1, x1,y1,z1, x1,y1,z0, top);
    // Bottom
    emitQuad(x1,y0,z0, x1,y0,z1, x0,y0,z1, x0,y0,z0, bot);
}

void HumanEntity::render() const
{
    if(health <= 0 && death_time > 20)
        return;

    if(hurt_time > 0)
    {
        GLFix t = GLFix(hurt_time) / GLFix(10);
        nglSetTextureModulate(GLFix(1), GLFix(1) - t * GLFix(0.52f), GLFix(1) - t * GLFix(0.48f));
    }

    GLFix render_yaw = yaw + GLFix(180);
    render_yaw.normaliseAngle();

    glPushMatrix();
    glTranslatef(x, y + HEIGHT / 2, z);
    nglRotateY(render_yaw);

    if(health <= 0 && death_time > 0)
    {
        float df = float(death_time - 1) / 20.0f * 1.6f;
        if(df < 0.f)
            df = 0.f;
        float f = std::sqrt(df);
        if(f > 1.f)
            f = 1.f;
        nglRotateZ(GLFix(f * 90.0f));
    }

    // ── proportions in nGL units ─────────────────────────────────────
    // Model pixels → nGL: use BLOCK_SIZE/16 per pixel (like MC).
    const GLFix px = GLFix(BLOCK_SIZE) / GLFix(16);

    // Dimensions (pixels)
    const GLFix head_w = GLFix(8) * px;
    const GLFix head_h = GLFix(8) * px;
    const GLFix head_d = GLFix(8) * px;

    const GLFix body_w = GLFix(8) * px;
    const GLFix body_h = GLFix(12) * px;
    const GLFix body_d = GLFix(4) * px;

    const GLFix arm_w = GLFix(4) * px;
    const GLFix arm_h = GLFix(12) * px;
    const GLFix arm_d = GLFix(4) * px;

    const GLFix leg_w = GLFix(4) * px;
    const GLFix leg_h = GLFix(12) * px;
    const GLFix leg_d = GLFix(4) * px;

    // Origin: center at torso center.
    const GLFix torso_y = GLFix(0);

    // Head position (top of torso + half head)
    const GLFix head_y = torso_y + body_h/2 + head_h/2;

    // Legs origin (below torso)
    const GLFix leg_y = torso_y - body_h/2 - leg_h/2;

    // Shoulder height relative to torso center
    const GLFix shoulder_y = torso_y + body_h/2 - arm_h/2;

    // Limb swing angles
    const GLFix swing = fast_sin(walk_timer) * GLFix(35) * swing_intensity;
    const GLFix swing_op = fast_sin(walk_timer + GLFix(180)) * GLFix(35) * swing_intensity;

    glBegin(GL_QUADS);

    // Head (centered)
    drawBipedBox(-head_w/2, head_y - head_h/2, -head_d/2,
                 head_w, head_h, head_d,
                 0, 0, 8, 8, 8, false);

    // Body (centered)
    drawBipedBox(-body_w/2, torso_y - body_h/2, -body_d/2,
                 body_w, body_h, body_d,
                 16, 16, 8, 12, 4, false);

    // Right Arm (player right = -x)
    glPushMatrix();
    glTranslatef(-body_w/2 - arm_w/2, shoulder_y, 0);
    nglRotateX(swing);
    drawBipedBox(-arm_w/2, -arm_h/2, -arm_d/2,
                 arm_w, arm_h, arm_d,
                 40, 16, 4, 12, 4, false);
    glPopMatrix();

    // Left Arm (player left = +x) – mirrored UVs
    glPushMatrix();
    glTranslatef(body_w/2 + arm_w/2, shoulder_y, 0);
    nglRotateX(swing_op);
    drawBipedBox(-arm_w/2, -arm_h/2, -arm_d/2,
                 arm_w, arm_h, arm_d,
                 32, 48, 4, 12, 4, true);
    glPopMatrix();

    // Right Leg (player right = -x)
    glPushMatrix();
    glTranslatef(-leg_w/2, leg_y, 0);
    nglRotateX(swing_op);
    drawBipedBox(-leg_w/2, -leg_h/2, -leg_d/2,
                 leg_w, leg_h, leg_d,
                 0, 16, 4, 12, 4, false);
    glPopMatrix();

    // Left Leg (player left = +x) – mirrored UVs
    glPushMatrix();
    glTranslatef(leg_w/2, leg_y, 0);
    nglRotateX(swing);
    drawBipedBox(-leg_w/2, -leg_h/2, -leg_d/2,
                 leg_w, leg_h, leg_d,
                 16, 48, 4, 12, 4, true);
    glPopMatrix();

    glEnd();

    glPopMatrix();

    nglResetTextureModulate();
}

void HumanEntity::applyMeleeDamage(int amount, GLFix attacker_yaw)
{
    if(health <= 0 || hurt_resistant > 0)
        return;

    health -= amount;
    hurt_time = 10;
    hurt_resistant = 10;

    GLFix ay = attacker_yaw;
    ay.normaliseAngle();
    GLFix kx = GLFix(fast_sin(ay));
    GLFix kz = GLFix(fast_cos(ay));
    vx /= 2;
    vz /= 2;
    vx += kx * GLFix(10);
    vz += kz * GLFix(10);

    vy /= 2;
    vy += GLFix(12);
    const GLFix cap(22);
    if(vy > cap)
        vy = cap;

    if(health < 0)
        health = 0;
}

void initHumanEntities()
{
    human_entities.clear();
    // Steve disabled by request.
}

void updateHumanEntities()
{
    // Steve disabled by request.
}

void renderHumanEntities()
{
    // Steve disabled by request.
}
