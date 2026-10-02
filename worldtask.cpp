#include <sys/stat.h>

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

#include "worldtask.h"

#include "audio_manager.h"
#include "aabb.h"
#include "blockrenderer.h"
#include "blocklisttask.h"
#include "commandtask.h"
#include "menutask.h"
#include "settingstask.h"
#include "fastmath.h"
#include "font.h"
#include "inventory.h"
#include "inventorytask.h"
#include "graphtask.h"

#include "textures/items.h"

// The icon and inventory atlases are only needed by the overlay, which lives in
// worldhud.cpp. Including them here would instantiate a second copy of each
// (they are static arrays in the generated headers).
#include "textures/blockselection.h"

#include "deathtask.h"
#include "livestockentity.h"
#include "villagegen.h"
#include "villagerentity.h"
#include "worldclock.h"
#include "creeperentity.h"
#include "grounddrops.h"
#include "itemrules.h"
#include "worlditems.h"

WorldTask world_task;

constexpr GLFix  WorldTask::player_width,  WorldTask::player_height,  WorldTask::eye_pos;

/** The level of one enchantment on the held stack, 0 when it has none. */
static int heldEnchant(const Enchanting::Id id)
{
    return current_inventory.currentSlotEnchant().levelOf(id);
}

static BLOCK_WDATA inventoryDropItem(const BLOCK_WDATA block)
{
    if(getBLOCK(block) == BLOCK_STONE)
        return BLOCK_COBBLESTONE;

    if(getBLOCK(block) == BLOCK_GRASS)
        return BLOCK_DIRT;

    if(getBLOCK(block) == BLOCK_LEAVES)
        return BLOCK_AIR;

    // Fast water dropping regular water
    if(getBLOCK(block) == BLOCK_WATER_FAST)
        return getBLOCKWDATA(BLOCK_WATER, RANGE_WATER);

    // An item drops as itself, with its full id: re-packing it through
    // getBLOCKDATA() would truncate an id of 128 or more.
    if(getBLOCK(block) == BLOCK_ITEM)
        return block;

    if(global_block_renderer.isOriented(block))
        return getBLOCK(block);

    return getBLOCKWDATA(getBLOCK(block), getBLOCKDATA(block));
}

// The tool tiers, the harvest requirements and the wear a use costs all live in
// itemrules.h now: they are a table, not a behaviour, and the host tests pin
// every entry down (tests/itemrules_test.cc).

/** Maps a block to the footstep/break material family used by the audio engine. */
static GameAudio::Material materialForBlock(const BLOCK block)
{
    // A bed is a mattress on a wooden frame, so it sounds like the wool it is
    // mostly made of rather than like the stone the default would give it.
    if(block == BLOCK_BED || (block >= BLOCK_WOOL_BLACK && block <= BLOCK_WOOL_ORANGE))
        return GameAudio::MaterialCloth;

    switch(block)
    {
    case BLOCK_GRASS:
    case BLOCK_LEAVES:
    case BLOCK_SPIDERWEB:
        return GameAudio::MaterialGrass;

    case BLOCK_WOOD:
    case BLOCK_PLANKS_NORMAL:
    case BLOCK_PLANKS_DARK:
    case BLOCK_PLANKS_BRIGHT:
    case BLOCK_CRAFTING_TABLE:
    case BLOCK_BOOKSHELF:
    case BLOCK_FURNACE:
        return GameAudio::MaterialWood;

    case BLOCK_SAND:
        return GameAudio::MaterialSand;

    case BLOCK_SNOW:
        // The audio pack has a snow family of its own, so a footstep on a snow
        // layer is a crunch rather than a stone click: weather that is visible and
        // audible changes the sound of the ground it settles on.
        return GameAudio::MaterialSnow;

    case BLOCK_DIRT:
        return GameAudio::MaterialGravel;

    case BLOCK_WATER:
    case BLOCK_WATER_FAST:
        return GameAudio::MaterialWetGrass;

    default:
        return GameAudio::MaterialStone;
    }
}

/** Normalised crosshair direction, shared by melee and mob interaction. */
static void crosshairRay(GLFix &dx, GLFix &dy, GLFix &dz)
{
    GLFix yr = world_task.yr;
    yr.normaliseAngle();
    GLFix xr = world_task.xr;
    xr.normaliseAngle();

    dx = GLFix(fast_sin(yr)) * GLFix(fast_cos(xr));
    dy = -GLFix(fast_sin(xr));
    dz = GLFix(fast_cos(yr)) * GLFix(fast_cos(xr));
}

/** Feeds a targeted animal to breed it; consumes the held food on success. */
static bool tryInteractMob()
{
    const BLOCK_WDATA held = current_inventory.currentSlot();
    if(getBLOCK(held) != BLOCK_ITEM)
        return false;

    GLFix dx, dy, dz;
    crosshairRay(dx, dy, dz);

    const GLFix eye_y = world_task.y + WorldTask::eye_pos;
    if(livestockTryInteract(world_task.x, eye_y, world_task.z, dx, dy, dz, getITEMDATA(held)))
    {
        current_inventory.removeFromCurrentSlot();
        world_task.setMessage("Animal fed");
        return true;
    }

    // Bartering with a villager: consume the wanted item and hand over the offer.
    uint16_t result_stack = 0;
    unsigned int result_count = 0;
    unsigned int consumed_count = 0;
    const char *trade_message = nullptr;
    const TradeResult trade = villagerTryTrade(world_task.x, eye_y, world_task.z, dx, dy, dz,
                                               held, current_inventory.currentSlotCount(),
                                               consumed_count, result_stack, result_count, &trade_message);
    if(trade == TradeResult::Traded)
    {
        current_inventory.removeFromCurrentSlot(consumed_count);
        current_inventory.addItem(result_stack, result_count);
        if(trade_message != nullptr)
            world_task.setMessage(trade_message);
        return true;
    }
    if(trade == TradeResult::Busy || trade == TradeResult::NoOffer)
    {
        if(trade_message != nullptr)
            world_task.setMessage(trade_message);
        return true;
    }

    return false;
}

/** Left click / KEY_7 primary: punch nearest mob in crosshair if closer than block hit (MC melee). */
static bool tryMeleeMob()
{
    GLFix dx, dy, dz;
    crosshairRay(dx, dy, dz);
    GLFix yr = world_task.yr;
    yr.normaliseAngle();
    const GLFix eye_y = world_task.y + WorldTask::eye_pos;

    LivestockEntity *hit_c = nullptr;
    GLFix c_dist = GLFix::maxValue();
    for(auto &c : livestock_entities)
    {
        if(!c.isAliveMob())
            continue;
        GLFix dist;
        if(c.aabb.intersectsRay(world_task.x, eye_y, world_task.z, dx, dy, dz, dist) == AABB::NONE)
            continue;
        if(dist < GLFix(0))
            continue;
        if(dist < c_dist)
        {
            c_dist = dist;
            hit_c = &c;
        }
    }

    CreeperEntity *hit_cr = nullptr;
    GLFix cr_dist = GLFix::maxValue();
    for(auto &cr : creeper_entities)
    {
        if(!cr.isAliveMob())
            continue;
        GLFix dist;
        if(cr.aabb.intersectsRay(world_task.x, eye_y, world_task.z, dx, dy, dz, dist) == AABB::NONE)
            continue;
        if(dist < GLFix(0))
            continue;
        if(dist < cr_dist)
        {
            cr_dist = dist;
            hit_cr = &cr;
        }
    }

    VillagerEntity *hit_v = nullptr;
    GLFix v_dist = GLFix::maxValue();
    for(auto &v : villager_entities)
    {
        if(!v.isAliveMob())
            continue;
        GLFix dist;
        if(v.aabb.intersectsRay(world_task.x, eye_y, world_task.z, dx, dy, dz, dist) == AABB::NONE)
            continue;
        if(dist < GLFix(0))
            continue;
        if(dist < v_dist)
        {
            v_dist = dist;
            hit_v = &v;
        }
    }

    if(hit_c == nullptr && hit_cr == nullptr && hit_v == nullptr)
        return false;

    GLFix best_dist = GLFix::maxValue();
    int pick = -1; // 0 livestock, 1 creeper, 2 villager
    if(hit_c != nullptr && c_dist < best_dist)
    {
        best_dist = c_dist;
        pick = 0;
    }
    if(hit_cr != nullptr && cr_dist < best_dist)
    {
        best_dist = cr_dist;
        pick = 1;
    }
    if(hit_v != nullptr && v_dist < best_dist)
    {
        best_dist = v_dist;
        pick = 2;
    }
    if(pick < 0)
        return false;

    VECTOR3 hit_block;
    AABB::SIDE side = AABB::NONE;
    GLFix block_dist;
    const bool got_block = world.intersectsRay(world_task.x, eye_y, world_task.z, dx, dy, dz, hit_block, side, block_dist, false);
    if(got_block && side != AABB::NONE && block_dist <= best_dist)
        return false;

    // What the hit is worth: the weapon's own damage (ItemRules::attackDamage,
    // vanilla's table), its Sharpness and the Strength effect, in one rule kept
    // with the survival state. The weapon's other enchantments travel with the hit
    // rather than changing its damage: Knockback throws the mob further, Looting is
    // rolled into its drop when it dies and Fire Aspect sets it alight.
    const int damage = world_task.heldMeleeDamage(Enchanting::TargetKind::Normal);
    const Enchanting::Set &weapon = current_inventory.currentSlotEnchant();
    const int knockback = Enchanting::knockbackSteps(weapon.levelOf(Enchanting::Knockback));
    const int looting = weapon.levelOf(Enchanting::Looting);
    const int fire_aspect = Enchanting::fireAspectTicks(weapon.levelOf(Enchanting::FireAspect));

    if(pick == 0)
        hit_c->applyMeleeDamage(damage, yr, knockback, looting, fire_aspect);
    else if(pick == 1)
        hit_cr->applyMeleeDamage(damage, yr, knockback, looting, fire_aspect);
    else
        hit_v->applyMeleeDamage(damage, yr, knockback, looting, fire_aspect);
    // The swing is the heavier sample when the hit does more than a bare hand.
    GameAudio::playerAttack(damage > 1);

    // Landing a hit is work: vanilla charges exhaustion for every attack.
    world_task.addExhaustion(Survival::ExhaustionPerAttack);

    // Landing a hit wears the weapon: a sword by one point, any other tool by
    // two, and bare hands not at all.
    wearHeldItem(ItemRules::durabilityPerAttack(current_inventory.currentSlot()));
    return true;
}

void WorldTask::makeCurrent()
{
    Task::background_saved = false;
    GameAudio::startMusic();

    Task::makeCurrent();
}

//Invert pixel at (x|y) relative to the center of the screen
void WorldTask::crosshairPixel(int x, int y)
{
    int pos = SCREEN_WIDTH/2 + x + (SCREEN_HEIGHT/2 + y)*SCREEN_WIDTH;
    screen->bitmap[pos] = ~screen->bitmap[pos];
}

void WorldTask::getForward(GLFix *x, GLFix *z)
{
    *x = GLFix(fast_sin(yr)) * speed();
    *z = GLFix(fast_cos(yr)) * speed();
}

void WorldTask::getRight(GLFix *x, GLFix *z)
{
    *x = GLFix(fast_sin((yr + 90).normaliseAngle())) * speed();
    *z = GLFix(fast_cos((yr + 90).normaliseAngle())) * speed();
}

GLFix WorldTask::speed()
{
    GLFix base = 10 * settings_task.getValue(SettingsTask::SPEED) + 10;

    if(keyPressed(KEY_NSPIRE_CTRL)) // Sprint
        return base * 2;

    if(keyPressed(KEY_NSPIRE_SHIFT)) // Sneak
        return base / 2;

    if(speed_multiplier_held) // 10x speed  
        return base * 10;

    return base;
}

void WorldTask::logic(GLFix dt)
{
    const bool graph_mode = world.worldType() == World::WorldType::Graph;

    updateClock(dt);
    // After the clock: the weather is derived from the time of day it just moved.
    updateWeather(dt);

    // A sleeping player lies still. The night was skipped the moment the bed was
    // used, so all that is left is the short fade that carries them into the
    // morning (worldbed.cpp).
    if(sleeping)
    {
        updateSleep(dt);
        return;
    }
#ifndef _TINSPIRE
    const Uint8 *desktop_keys = SDL_GetKeyState(nullptr);
    const bool desktop_t_held = desktop_keys[SDLK_t] != 0;
    const bool desktop_f_held = desktop_keys[SDLK_f] != 0;
    const bool desktop_g_held = desktop_keys[SDLK_g] != 0;
    const bool desktop_j_held = desktop_keys[SDLK_j] != 0;
    const bool desktop_x_held = desktop_keys[SDLK_x] != 0;
    const bool desktop_z_held = desktop_keys[SDLK_z] != 0;
    const bool desktop_v_held = desktop_keys[SDLK_v] != 0;
    speed_multiplier_held = desktop_v_held;
#else
    const bool desktop_t_held = false;
    const bool desktop_f_held = false;
    const bool desktop_g_held = false;
    const bool desktop_j_held = false;
    const bool desktop_x_held = false;
    const bool desktop_z_held = false;
    speed_multiplier_held = false;
#endif

    GLFix dx = 0, dz = 0;

    if(keyPressed(KEY_NSPIRE_8)) //Forward
    {
        GLFix dx1, dz1;
        getForward(&dx1, &dz1);

        dx += dx1;
        dz += dz1;
    }
    else if(keyPressed(KEY_NSPIRE_2)) //Backward
    {
        GLFix dx1, dz1;
        getForward(&dx1, &dz1);

        dx -= dx1;
        dz -= dz1;
    }

    if(keyPressed(KEY_NSPIRE_4)) //Left
    {
        GLFix dx1, dz1;
        getRight(&dx1, &dz1);

        dx -= dx1;
        dz -= dz1;
    }
    else if(keyPressed(KEY_NSPIRE_6)) //Right
    {
        GLFix dx1, dz1;
        getRight(&dx1, &dz1);

        dx += dx1;
        dz += dz1;
    }

    dx *= dt;
    dz *= dt;

    if(graph_mode)
    {
        x += dx;
        z += dz;

        GLFix dy = 0;
        if(keyPressed(KEY_NSPIRE_5))
            dy += speed() * dt;
        if(keyPressed(KEY_NSPIRE_CTRL))
            dy -= speed() * dt;

        y += dy;
        if(y < GLFix(2 * BLOCK_SIZE))
            y = GLFix(2 * BLOCK_SIZE);

        vy = 0;
        can_jump = true;
        in_water = false;
        fall_distance = 0;
        safe_spawn_pending = false;
    }
    else if(!world.intersect(aabb))
    {
        AABB aabb_moved = aabb;
        aabb_moved.low_x += dx;
        aabb_moved.high_x += dx;

        if(!world.intersect(aabb_moved))
        {
            x += dx;
            aabb = aabb_moved;
        }

        aabb_moved = aabb;
        aabb_moved.low_z += dz;
        aabb_moved.high_z += dz;

        if(!world.intersect(aabb_moved))
        {
            z += dz;
            aabb = aabb_moved;
        }

        const GLFix vy_before = vy;
        aabb_moved = aabb;
        aabb_moved.low_y += vy * dt;
        aabb_moved.high_y += vy * dt;

        can_jump = world.intersect(aabb_moved);

        const bool landed_from_fall = (vy_before < GLFix(0)) && can_jump && fall_distance > GLFix(0);

        if(!can_jump)
        {
            y += vy * dt;
            aabb = aabb_moved;

            // While falling (downward velocity) and we actually move, accumulate fall distance.
            if(vy_before < GLFix(0))
                fall_distance += (-vy_before) * dt;
        }
        else if(vy > GLFix(0))
        {
            can_jump = false;
            vy = 0;
        }
        else
            vy = 0;

        vy -= GLFix(5) * dt;

        in_water = getBLOCK(world.getBlock((x / BLOCK_SIZE).floor(), ((y + eye_pos) / BLOCK_SIZE).floor(), (z / BLOCK_SIZE).floor())) == BLOCK_WATER
                || getBLOCK(world.getBlock((x / BLOCK_SIZE).floor(), ((y + eye_pos) / BLOCK_SIZE).floor(), (z / BLOCK_SIZE).floor())) == BLOCK_WATER_FAST;

        if(landed_from_fall)
        {
            const int fall_blocks = fall_distance.toInteger<int>() / BLOCK_SIZE;
            // Vanilla has a small and a big landing sound; the big one is the
            // fall that is tall enough to start hurting.
            GameAudio::playerFall(Survival::fallDamage(fall_blocks) > 0);
            // Apply fall damage only when not in water, and never during the initial safe spawn.
            if(!safe_spawn_pending && !in_water)
            {
                int dmg = Survival::fallDamage(fall_blocks);
                // Feather Falling is the boots' enchantment, and it takes its share
                // off the fall before the armour ever sees it (vanilla applies it to
                // the fall itself).
                const int feather = Enchanting::featherFallingPercentReduction(
                    current_inventory.armor_enchant[ItemRules::BootsSlot].levelOf(Enchanting::FeatherFalling));
                if(feather > 0)
                    dmg = dmg * (100 - feather) / 100;
                if(dmg > 0)
                {
                    // applyDamage switches to the death screen at zero health.
                    applyDamage(dmg, Survival::Damage::Fall, "Ouch!");
                    if(health <= 0)
                        return;
                }
            }

            // Whether we took damage or not, we've impacted.
            fall_distance = 0;
        }

        if(in_water)
            can_jump = true;
    }

    // Movement and jumping cost food, which is how hunger actually drains.
    if(!graph_mode)
    {
        if(!exhaustion_tracking)
        {
            exhaustion_x = x;
            exhaustion_z = z;
            exhaustion_tracking = true;
        }
        GLFix travelled_x = x - exhaustion_x, travelled_z = z - exhaustion_z;
        if(travelled_x < GLFix(0))
            travelled_x = -travelled_x;
        if(travelled_z < GLFix(0))
            travelled_z = -travelled_z;
        exhaustion_x = x;
        exhaustion_z = z;

        const GLFix travelled = travelled_x + travelled_z;
        if(travelled > GLFix(0))
        {
            const float blocks = travelled.toFloat() / static_cast<float>(BLOCK_SIZE);
            const float per_block = keyPressed(KEY_NSPIRE_CTRL) ? Survival::ExhaustionPerBlockSprinted
                                                                : Survival::ExhaustionPerBlockWalked;
            Survival::addExhaustion(hunger, blocks * per_block);
        }
    }

    updateSurvival(dt);

    // Lava sets the player alight, which keeps burning after they climb out.
    if(!graph_mode)
    {
        const BLOCK_WDATA at_feet = world.getBlock((x / BLOCK_SIZE).floor(), (y / BLOCK_SIZE).floor(), (z / BLOCK_SIZE).floor());
        const BLOCK feet_type = getBLOCK(at_feet);
        if(feet_type == BLOCK_LAVA)
        {
            fire_ticks = Survival::FireTicksFromLava;
            fire_timer = Survival::FireDamageInterval;
        }
    }

    if(!graph_mode && keyPressed(KEY_NSPIRE_5) && can_jump) //Jump
    {
        vy = 50;
        can_jump = false;
        Survival::addExhaustion(hunger, Survival::ExhaustionPerJump);
        // Vanilla has no jump sound; the landing is what is heard.
    }

    // --- audio: footsteps, water/cave ambience --------------------------
    if(!graph_mode)
    {
        if(!step_tracking)
        {
            last_step_x = x;
            last_step_z = z;
            step_tracking = true;
        }

        GLFix moved_x = x - last_step_x, moved_z = z - last_step_z;
        last_step_x = x;
        last_step_z = z;
        if(moved_x < GLFix(0))
            moved_x = -moved_x;
        if(moved_z < GLFix(0))
            moved_z = -moved_z;

        if(can_jump && !in_water)
        {
            step_distance += moved_x + moved_z;
            if(step_distance >= GLFix(BLOCK_SIZE))
            {
                step_distance = 0;
                const BLOCK below = getBLOCK(world.getBlock((x / BLOCK_SIZE).floor(),
                    ((y - GLFix(1)) / BLOCK_SIZE).floor(), (z / BLOCK_SIZE).floor()));
                GameAudio::footstep(materialForBlock(below));
            }
        }
        else
            step_distance = 0;

        const unsigned int wanted_ambience = in_water
            ? static_cast<unsigned int>(GameAudio::Sound::AmbientUnderwaterUnderwaterAmbience) : 0u;
        if(wanted_ambience != current_ambience)
        {
            current_ambience = wanted_ambience;
            GameAudio::setAmbience(wanted_ambience);
        }

        if(++ambience_timer > 2600)
        {
            ambience_timer = 0;
            if(y < GLFix(World::HEIGHT * Chunk::SIZE * BLOCK_SIZE / 2))
                GameAudio::ambienceCue();
        }
    }

#ifndef _TINSPIRE
    int rel_x = 0, rel_y = 0;
    SDL_GetRelativeMouseState(&rel_x, &rel_y);
    if(rel_x != 0 || rel_y != 0)
    {
        yr += GLFix(rel_x) / 3;
        xr += GLFix(rel_y) / 3;
    }
#endif

    if(has_touchpad)
    {
        touchpad_report_t touchpad;
        touchpad_scan(&touchpad);

        if(touchpad.pressed)
        {
            switch(touchpad.arrow)
            {
            case TPAD_ARROW_DOWN:
                xr += speed()/2 * dt;
                break;
            case TPAD_ARROW_UP:
                xr -= speed()/2 * dt;
                break;
            case TPAD_ARROW_LEFT:
                yr -= speed()/2 * dt;
                break;
            case TPAD_ARROW_RIGHT:
                yr += speed()/2 * dt;
                break;
            case TPAD_ARROW_RIGHTDOWN:
                xr += speed()/2 * dt;
                yr += speed()/2 * dt;
                break;
            case TPAD_ARROW_UPRIGHT:
                xr -= speed()/2 * dt;
                yr += speed()/2 * dt;
                break;
            case TPAD_ARROW_DOWNLEFT:
                xr += speed()/2 * dt;
                yr -= speed()/2 * dt;
                break;
            case TPAD_ARROW_LEFTUP:
                xr -= speed()/2 * dt;
                yr -= speed()/2 * dt;
                break;
            }
        }
        else if(tp_had_contact && touchpad.contact)
        {
            yr += (touchpad.x - tp_last_x) / 17;
            xr -= (touchpad.y - tp_last_y) / 17;
        }

        tp_had_contact = touchpad.contact;
        tp_last_x = touchpad.x;
        tp_last_y = touchpad.y;
    }
    else
    {
        if(keyPressed(KEY_NSPIRE_UP))
            xr -= speed()/3 * dt;
        else if(keyPressed(KEY_NSPIRE_DOWN))
            xr += speed()/3 * dt;

        if(keyPressed(KEY_NSPIRE_LEFT))
            yr -= speed()/3 * dt;
        else if(keyPressed(KEY_NSPIRE_RIGHT))
            yr += speed()/3 * dt;
    }

    //Normalisation required for rotation with nglRotate
    yr.normaliseAngle();
    xr.normaliseAngle();

    //xr and yr are normalised, so we can't test for negative values
    if(xr > GLFix(180))
        if(xr <= GLFix(270))
            xr = 269;

    if(xr < GLFix(180))
        if(xr >= GLFix(90))
            xr = 89;

    //Do test only on every second frame, it's expensive
    if(do_test)
    {
        GLFix dx = fast_sin(yr)*fast_cos(xr), dy = -fast_sin(xr), dz = fast_cos(yr)*fast_cos(xr);
        GLFix dist;
        if(!world.intersectsRay(x, y + eye_pos, z, dx, dy, dz, selection_pos, selection_side, dist, in_water))
            selection_side = AABB::NONE;
        else
            selection_pos_abs = {x + dx * dist, y + eye_pos + dy * dist, z + dz * dist};
    }

    world.setPosition(x, y, z);

    if(safe_spawn_pending)
    {
        VECTOR3 hit_pos = {-1, -1, -1};
        AABB::SIDE hit_side = AABB::NONE;
        GLFix dist;
        constexpr GLFix dx = GLFix(0);
        constexpr GLFix dz = GLFix(0);
        constexpr GLFix dy = GLFix(-1);

        // Raycast straight down from the player's eye.
        if(world.intersectsRay(x, y + eye_pos, z, dx, dy, dz, hit_pos, hit_side, dist, false))
        {
            const int hit_block_y = hit_pos.y.toInteger<int>();
            // Place player 1 block above the first solid block hit.
            y = GLFix(hit_block_y + 1) * BLOCK_SIZE + GLFix::minStep();
        }

        // Reset velocities and fall tracking after teleporting.
        vy = 0;
        fall_distance = 0;
        safe_spawn_pending = false;
    }

    do_test = !do_test;

    if(!keyPressed(KEY_NSPIRE_9))
    {
        mining_progress = 0;
        mining_tick_accum = 0;
    }

    if(key_held_down)
    {
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_7) || keyPressed(KEY_NSPIRE_1) || keyPressed(KEY_NSPIRE_3) || keyPressed(KEY_NSPIRE_PERIOD) || keyPressed(KEY_NSPIRE_MINUS) || keyPressed(KEY_NSPIRE_PLUS) || keyPressed(KEY_NSPIRE_MENU) || keyPressed(KEY_NSPIRE_A) || keyPressed(KEY_NSPIRE_DIVIDE) || desktop_t_held || desktop_f_held;
        key_held_down = key_held_down || desktop_g_held || desktop_j_held || desktop_x_held || desktop_z_held;
    }

    else if(keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_MENU))
    {
        menu_task.makeCurrent();
        key_held_down = true;
        return;
    }
    else if(keyPressed(KEY_NSPIRE_DIVIDE))
    {
        // The divide key is the one the calculator spells '/' with, which is what
        // a command is written after. The console takes the frame from here, so
        // the world stops moving while a line is typed and nothing the player
        // walks into is lost.
        command_task.open();
        key_held_down = true;
        return;
    }
    else if(keyPressed(KEY_NSPIRE_7)) //Put block down
    {
        key_held_down = true;

        // Melee first; do not return from logic() so mob updates still run this tick (avoids stale state / render glitches).
        if(!tryInteractMob() && !tryMeleeMob())
        {
            if(selection_side == AABB::NONE)
                return;

            if(world.intersect(aabb))
                return;

            // A block that does something when used (a lever, a door, a crafting
            // table) acts before placement is considered, in every mode.
            if(world.blockAction(selection_pos.x, selection_pos.y, selection_pos.z))
                return;

            // Armour is worn rather than placed, so using a piece puts it on (and
            // swaps out whatever was in that slot) instead of doing nothing.
            if(tryEquipHeldArmor())
                return;

            BLOCK_WDATA current_block = world.getBlock(selection_pos.x, selection_pos.y, selection_pos.z),
                        block_to_place = current_inventory.currentSlot();

            if(getBLOCK(block_to_place) == BLOCK_AIR)
                return;

            if(getBLOCK(block_to_place) == BLOCK_ITEM)
                return;

            // When placing fluid onto a non-full fluid block of the same type, "fill" it
            if(current_block != block_to_place
               && ((getBLOCK(current_block) == BLOCK_WATER && getBLOCK(block_to_place) == BLOCK_WATER)
                   || (getBLOCK(current_block) == BLOCK_LAVA && getBLOCK(block_to_place) == BLOCK_LAVA)))
            {
                world.changeBlock(selection_pos.x, selection_pos.y, selection_pos.z, block_to_place);
                // Creative builds without spending anything, which is the point of
                // the mode: the hotbar is a palette rather than a supply.
                if(!isCreative())
                    current_inventory.removeFromCurrentSlot();
                GameAudio::placeBlock(materialForBlock(getBLOCK(block_to_place)));
                return;
            }

            VECTOR3 pos = selection_pos;
            switch(selection_side)
            {
            case AABB::BACK:
                ++pos.z;
                break;
            case AABB::FRONT:
                --pos.z;
                break;
            case AABB::LEFT:
                --pos.x;
                break;
            case AABB::RIGHT:
                ++pos.x;
                break;
            case AABB::BOTTOM:
                --pos.y;
                break;
            case AABB::TOP:
                ++pos.y;
                break;
            default:
                puts("This can't normally happen #1");
                break;
            }

            current_block = world.getBlock(pos.x, pos.y, pos.z);

            //Only set the block if there's air
            if(current_block == BLOCK_AIR || (in_water && getBLOCK(current_block) == BLOCK_WATER))
            {
                // A bed is two cells laid flat sharing one facing, so which way it
                // points comes from where the player is looking rather than from
                // the face that was clicked, and both halves go down together or
                // neither does (worlditems.cpp).
                if(getBLOCK(block_to_place) == BLOCK_BED)
                {
                    if(tryPlaceBed(pos.x, pos.y, pos.z, yr))
                    {
                        if(!isCreative())
                            current_inventory.removeFromCurrentSlot();
                        GameAudio::placeBlock(materialForBlock(BLOCK_BED));
                    }
                    else
                        setMessage("No room for a bed");

                    return;
                }

                bool placed = false;
                if(!global_block_renderer.isOriented(block_to_place))
                {
                    world.changeBlock(pos.x, pos.y, pos.z, block_to_place);
                    placed = true;
                }
                else
                {
                    AABB::SIDE side = selection_side;
                    //If the block is not fully oriented and has been placed on top or bottom of another block, determine the orientation by yr
                    if(!global_block_renderer.isFullyOriented(block_to_place) && (side == AABB::TOP || side == AABB::BOTTOM))
                        side = yr < GLFix(45) ? AABB::FRONT : yr < GLFix(135) ? AABB::LEFT : yr < GLFix(225) ? AABB::BACK : yr < GLFix(315) ? AABB::RIGHT : AABB::FRONT;

                    world.changeBlock(pos.x, pos.y, pos.z, getBLOCKWDATA(block_to_place, side)); //AABB::SIDE is compatible to BLOCK_SIDE
                    placed = true;
                }

                //If the player is stuck now, it's because of the block change, so remove it again
                if(placed && world.intersect(aabb))
                {
                    world.changeBlock(pos.x, pos.y, pos.z, current_block);
                    placed = false;
                }

                if(placed)
                {
                    if(getBLOCK(block_to_place) == BLOCK_FURNACE)
                        inventory_task.ensureFurnaceTile(pos.x, pos.y, pos.z);
                    // A chest starts an empty container, and pairs with a chest
                    // beside it to become a double chest.
                    if(getBLOCK(block_to_place) == BLOCK_CHEST)
                        placeChestAt(pos.x, pos.y, pos.z);
                    if(!isCreative())
                        current_inventory.removeFromCurrentSlot();
                    GameAudio::placeBlock(materialForBlock(getBLOCK(block_to_place)));
                }
            }
        }
    }
    else if(keyPressed(KEY_NSPIRE_9)) //Remove block
    {
        BLOCK_WDATA b = world.getBlock(selection_pos.x, selection_pos.y, selection_pos.z);
        if(selection_side != AABB::NONE && getBLOCK(b) != BLOCK_BEDROCK && getBLOCK(b) != BLOCK_AIR)
        {
            const BLOCK b_type = getBLOCK(b);
            const int pickaxe_tier = ItemRules::pickaxeTier(current_inventory.currentSlot());

            if (mining_pos.x != selection_pos.x || mining_pos.y != selection_pos.y || mining_pos.z != selection_pos.z) {
                mining_pos = selection_pos;
                mining_progress = 0;
                mining_tick_accum = 0;

                mining_duration = 30; // Default
                if (b_type == BLOCK_DIRT || b_type == BLOCK_SAND || b_type == BLOCK_LEAVES || b_type == BLOCK_GRASS) mining_duration = 10;
                else if (b_type == BLOCK_STONE || b_type == BLOCK_COBBLESTONE || b_type == BLOCK_IRON_ORE || b_type == BLOCK_COAL_ORE || b_type == BLOCK_FURNACE) mining_duration = 60;
                else if (b_type == BLOCK_WOOD || b_type == BLOCK_PLANKS_NORMAL || b_type == BLOCK_CRAFTING_TABLE) mining_duration = 30;
                else if (b_type == BLOCK_GLASS) mining_duration = 15;
                else if (b_type == BLOCK_IRON || b_type == BLOCK_GOLD || b_type == BLOCK_DIAMOND) mining_duration = 100;

                if(ItemRules::isPickaxeMinedBlock(b_type))
                {
                    if(pickaxe_tier == 0)
                        mining_duration *= 3;
                    else if(pickaxe_tier == 1)
                        mining_duration = mining_duration * 100 / 100;
                    else if(pickaxe_tier == 2)
                        mining_duration = mining_duration * 70 / 100;
                    else if(pickaxe_tier == 3)
                        mining_duration = mining_duration * 50 / 100;
                    else if(pickaxe_tier == 4)
                        mining_duration = mining_duration * 40 / 100;
                    else
                        mining_duration = mining_duration * 35 / 100;

                    if(mining_duration < 3)
                        mining_duration = 3;
                }

                // Mining under water is five times slower, exactly as vanilla has
                // it -- and Aqua Affinity (the helmet's) is the enchantment that
                // takes that penalty away, which is the whole reason it exists.
                const int aqua_affinity = current_inventory.armor_enchant[ItemRules::HelmetSlot]
                                              .levelOf(Enchanting::AquaAffinity);
                if(in_water && !Enchanting::aquaAffinity(aqua_affinity))
                    mining_duration *= 5;

                // Creative breaks a block at a touch, whatever it is made of.
                if(isCreative())
                    mining_duration = 1;
            }
            mining_tick_accum += dt;
            while(mining_tick_accum >= GLFix(1))
            {
                mining_tick_accum -= GLFix(1);

                // Efficiency speeds the swing up rather than shortening the block:
                // the progress bar moves faster, which is what a player sees in
                // vanilla, and the digging sound and the wear keep their cadence.
                const int efficiency = Enchanting::efficiencyPercent(heldEnchant(Enchanting::Efficiency));
                mining_progress += efficiency >= 100 ? efficiency / 100 : 1;

                if ((mining_progress / 10) != ((mining_progress - 1) / 10)) {
                    world.spawnDestructionParticles(selection_pos.x, selection_pos.y, selection_pos.z);
                }

                if (mining_progress >= mining_duration) {
                    world.spawnDestructionParticles(selection_pos.x, selection_pos.y, selection_pos.z);
                    const int required_pickaxe_tier = ItemRules::requiredPickaxeTierForDrop(b_type);
                    const bool can_harvest = required_pickaxe_tier == 0 || pickaxe_tier >= required_pickaxe_tier;
                    if(can_harvest)
                    {
                        // Silk Touch drops the block itself; Fortune rolls for extra
                        // drops of whatever it would have dropped anyway. Both are
                        // read off the tool that is doing the mining.
                        BLOCK_WDATA drop_stack = inventoryDropItem(b);
                        unsigned int drop_count = 1;
                        if(Enchanting::silkTouch(heldEnchant(Enchanting::SilkTouch)))
                            drop_stack = b;
                        else
                        {
                            // Fortune: one roll per level, each with the chance that
                            // level adds (the steps in enchanting.cpp's multiplier
                            // table), so Fortune III really does drop more than
                            // Fortune I rather than rolling the same dice three times.
                            const int fortune = heldEnchant(Enchanting::Fortune);
                            for(int level = 1; level <= fortune; ++level)
                            {
                                const int chance = Enchanting::fortuneExtraDropPercent(level)
                                    - Enchanting::fortuneExtraDropPercent(level - 1);
                                if((rand() % 100) < chance)
                                    ++drop_count;
                            }
                        }

                        if(getBLOCK(drop_stack) != BLOCK_AIR)
                        {
                            const GLFix sx = selection_pos.x * GLFix(BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2);
                            const GLFix sy = selection_pos.y * GLFix(BLOCK_SIZE) + GLFix(BLOCK_SIZE) + GLFix::minStep();
                            const GLFix sz = selection_pos.z * GLFix(BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2);
                            spawnWorldDrop(sx, sy, sz, drop_stack, drop_count);
                        }

                        // Ores are worth experience, and which ore is worth how much
                        // is survival.h's table rather than a number written here.
                        const Survival::XpRange range = Survival::xpBlockRange(b_type);
                        const int xp = Survival::xpRoll(range, static_cast<uint32_t>(rand()));
                        if(xp > 0)
                            addExperience(xp);
                    }
                    if(b_type == BLOCK_FURNACE)
                        inventory_task.removeFurnaceTile(selection_pos.x, selection_pos.y, selection_pos.z);
                    // Before the block goes: the chest hands its contents to the
                    // world as drops, so nothing inside is lost.
                    if(b_type == BLOCK_CHEST)
                        breakChestAt(selection_pos.x, selection_pos.y, selection_pos.z);
                    world.changeBlock(selection_pos.x, selection_pos.y, selection_pos.z, BLOCK_AIR);
                    GameAudio::digBlock(materialForBlock(b_type));
                    // Using a tool wears it: one point per block, and a tool that
                    // reaches its limit breaks instead of dropping. Unbreaking is
                    // rolled inside wearHeldItem(), where every use goes through.
                    wearHeldItem(ItemRules::durabilityPerBlockMined(current_inventory.currentSlot()));
                    mining_progress = 0;
                    mining_tick_accum = 0;
                    break;
                }
            }
        }
        else
        {
            mining_progress = 0;
            mining_tick_accum = 0;
        }
    }
    else if(keyPressed(KEY_NSPIRE_1)) //Switch inventory slot
    {
        current_inventory.previousSlot();

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_3))
    {
        current_inventory.nextSlot();

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_PERIOD)) //Open list of blocks (or take screenshot with Ctrl + .)
    {
        if(keyPressed(KEY_NSPIRE_CTRL))
        {
            //Find a filename that doesn't exist
            char buf[45];

            unsigned int i;
            for(i = 0; i <= 999; ++i)
            {
                snprintf(buf, sizeof(buf), "/documents/ndless/screenshot_%d.ppm.tns", i);

                struct stat stat_buf;
                if(stat(buf, &stat_buf) != 0)
                    break;
            }

            if(i > 999 || !saveTextureToFile(*screen, buf))
                setMessage("Screenshot failed!");
            else
            {
                snprintf(message, sizeof(message), "Screenshot taken (%d)!", i);
                message_timeout = 20;
            }
        }
        else
        {
            draw_inventory = false;
            render();
            draw_inventory = true;
            block_list_task.makeCurrent();
        }

        key_held_down = true;
    }
    else if(graph_mode && desktop_z_held)
    {
        const int old_zoom = world.graphZoomPercent();
        if(world.setGraphZoomPercent(old_zoom - 10))
        {
            world.clear();
            char msg[32];
            snprintf(msg, sizeof(msg), "Graph zoom: %d%%", world.graphZoomPercent());
            setMessage(msg);
        }
        else
            setMessage("Graph zoom min: 20%");

        key_held_down = true;
    }
    else if(graph_mode && desktop_x_held)
    {
        const int old_zoom = world.graphZoomPercent();
        if(world.setGraphZoomPercent(old_zoom + 10))
        {
            world.clear();
            char msg[32];
            snprintf(msg, sizeof(msg), "Graph zoom: %d%%", world.graphZoomPercent());
            setMessage(msg);
        }
        else
            setMessage("Graph zoom max: 800%");

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_MINUS)) //Decrease max view distance
    {
        int fov = world.fieldOfView() - 1;
        world.setFieldOfView(fov < 1 ? 1 : fov);

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_PLUS)) //Increase max view distance
    {
        world.setFieldOfView(world.fieldOfView() + 1);

        key_held_down = true;
    }
    // Handled above with ESC
    else if(graph_mode && desktop_g_held)
    {
        graph_task.makeCurrent();
        key_held_down = true;
    }
    else if(graph_mode && desktop_j_held)
    {
        world.setGraphUnbounded(!world.graphUnbounded());
        world.clear();
        setMessage(world.graphUnbounded() ? "Graph bounds: infinite" : "Graph bounds: [-30,30]");
        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_A))
    {
        inventory_task.makeCurrent();

        key_held_down = true;
    }
    // Vanilla's "swap items with offhand" key: F trades the selected hotbar
    // stack with the offhand one, wear and all, without opening a window.
    else if(desktop_f_held)
    {
        current_inventory.swapOffhandWithCurrentSlot();
        key_held_down = true;
    }
#ifndef _TINSPIRE
    else
    {
        const Uint8 *keys = SDL_GetKeyState(nullptr);
        if(keys[SDLK_t] != 0)
        {
            inventory_task.makeCurrent();
            key_held_down = true;
        }
    }
#endif
    // Discrete tick-based entities: advance ~dt worth of old 1-tick steps (carry fractional remainder).
    sim_tick_accum += dt;
    unsigned sim_steps = 0;
    while(!graph_mode && sim_tick_accum >= GLFix(1) && sim_steps < 8)
    {
        sim_tick_accum -= GLFix(1);
        inventory_task.tickFurnaces(world);
        updateLivestockEntities();
        updateVillagerEntities();
        updateCreeperEntities();
        // Real milliseconds per step, so a drop's five-minute lifetime is the
        // same on the calculator (300 ms steps) and on the desktop (33 ms ones).
        updateGroundDrops(simulation_tick_ms);
        ++sim_steps;
    }
}

void WorldTask::render()
{
    const bool graph_mode = world.worldType() == World::WorldType::Graph;
    aabb = {x - player_width/2, y, z - player_width/2, x + player_width/2, y + player_height, z + player_width/2};
    //printf("X: %f Y: %f Z: %f XR: %d YR: %d\n", x.toFloat(), y.toFloat(), z.toFloat(), xr.toInt(), yr.toInt());

    renderSky();

    // Time-of-day light for everything textured drawn below. The graph view is
    // left at full brightness so a plot stays readable at midnight, and at full
    // daylight the shade stays neutral, which costs nothing per pixel.
    const bool day_night = !graph_mode && settings_task.getValue(SettingsTask::DAY_NIGHT) != 0;
    // Night vision raises the floor the tint may darken the world to, which is
    // what survival.h's lightFloorFor() is for: a player under the effect keeps
    // the dusk they had when they drank it instead of sinking to the usual floor.
    // Nothing happens without the effect, since the floor it returns is zero.
    const int light_floor = effectAmplifier(Survival::NightVision) >= 0
        ? Survival::lightFloorFor(Survival::NightVision) : 0;
    unsigned int global_shade = day_night
        ? static_cast<unsigned int>(WorldClock::skyLightFactor(light_floor) * 256.0f + 0.5f)
        : 256u;
    // Rain and cloud darken the world on top of the time of day. A lightning
    // flash is an overlay in renderWeather() instead: this factor is a ceiling
    // of 256 and pushing it past that would overflow into the colour channels.
    if(weather_darkness > 0)
        global_shade = (global_shade * static_cast<unsigned int>(256 - weather_darkness)) >> 8;
    nglSetGlobalShade(global_shade);

    glPushMatrix();

    //Inverted rotation of the world
    nglRotateX((GLFix(359) - xr).normaliseAngle());
    nglRotateY((GLFix(359) - yr).normaliseAngle());
    //Inverted translation of the world
    glTranslatef(-x, -y - eye_pos, -z);

    glBindTexture(terrain_current);

    world.render();

    // Render entities only in normal worlds.
    if(!graph_mode)
    {
        renderLivestockEntities();
        renderVillagerEntities();
        renderCreeperEntities();
        renderGroundDrops();
    }
    // Re-bind terrain texture for the rest of the world rendering
    glBindTexture(terrain_current);

    // Draw selection / breaking indication.
    if(settings_task.getValue(SettingsTask::BLOCK_INDICATOR))
    {
        TextureAtlasEntry tex;
        const bool show_breaking_overlay = mining_progress > 0 && mining_duration > 0 &&
                                           selection_pos.x == mining_pos.x && selection_pos.y == mining_pos.y && selection_pos.z == mining_pos.z;
        if(show_breaking_overlay)
        {
            glBindTexture(terrain_current);
            static constexpr unsigned int breaking_frames = 10; // (0,15) to (9,15)
            unsigned int breaking_frame = (static_cast<unsigned int>(mining_progress) * breaking_frames) / static_cast<unsigned int>(mining_duration);
            if(breaking_frame >= breaking_frames)
                breaking_frame = breaking_frames - 1;

            tex = terrain_atlas[breaking_frame][15].current;
        }
        else
        {
            glBindTexture(&blockselection);

            //Do a quick animation
            const unsigned int blockselection_frame_width = blockselection.width / blockselection_frames;
            tex = textureArea(0, 0, blockselection_frame_width, blockselection.height);
            tex.left += blockselection_frame_width * blockselection_frame;
            tex.right += blockselection_frame_width * blockselection_frame;

            //Only increment the frame nr each 5 frames
            if(++blockselection_frame_fraction == 5)
            {
                blockselection_frame_fraction = 0;

                if(++blockselection_frame == blockselection_frames)
                    blockselection_frame = 0;
            }
        }

        const GLFix indicator_x = selection_pos.x * BLOCK_SIZE, indicator_y = selection_pos.y * BLOCK_SIZE, indicator_z = selection_pos.z * BLOCK_SIZE;
        const GLFix selection_offset = 3; //Needed to prevent Z-fighting

        glPushMatrix();
        glTranslatef(indicator_x, indicator_y, indicator_z);

        glBegin(GL_QUADS);
        if(show_breaking_overlay)
        {
            const GLFix block_size_fix = GLFix(BLOCK_SIZE);
            const GLFix minus_offset = GLFix(0) - selection_offset;
            const GLFix plus_offset = block_size_fix + selection_offset;

            // Front
            nglAddVertex({0, 0, minus_offset, tex.left, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({0, block_size_fix, minus_offset, tex.left, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({block_size_fix, block_size_fix, minus_offset, tex.right, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({block_size_fix, 0, minus_offset, tex.right, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});

            // Back
            nglAddVertex({block_size_fix, 0, plus_offset, tex.left, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({block_size_fix, block_size_fix, plus_offset, tex.left, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({0, block_size_fix, plus_offset, tex.right, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({0, 0, plus_offset, tex.right, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});

            // Right
            nglAddVertex({plus_offset, 0, 0, tex.right, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({plus_offset, block_size_fix, 0, tex.right, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({plus_offset, block_size_fix, block_size_fix, tex.left, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({plus_offset, 0, block_size_fix, tex.left, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});

            // Left
            nglAddVertex({minus_offset, 0, block_size_fix, tex.left, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({minus_offset, block_size_fix, block_size_fix, tex.left, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({minus_offset, block_size_fix, 0, tex.right, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({minus_offset, 0, 0, tex.right, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});

            // Top
            nglAddVertex({0, plus_offset, 0, tex.left, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({0, plus_offset, block_size_fix, tex.left, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({block_size_fix, plus_offset, block_size_fix, tex.right, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({block_size_fix, plus_offset, 0, tex.right, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});

            // Bottom
            nglAddVertex({block_size_fix, minus_offset, 0, tex.left, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({block_size_fix, minus_offset, block_size_fix, tex.left, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({0, minus_offset, block_size_fix, tex.right, tex.top, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
            nglAddVertex({0, minus_offset, 0, tex.right, tex.bottom, TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE});
        }
        else switch(selection_side)
        {
        case AABB::FRONT:
            nglAddVertex({0, 0, selection_pos_abs.z - indicator_z - selection_offset, tex.left, tex.bottom, TEXTURE_TRANSPARENT});
            nglAddVertex({0, BLOCK_SIZE, selection_pos_abs.z - indicator_z - selection_offset, tex.left, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({BLOCK_SIZE, BLOCK_SIZE, selection_pos_abs.z - indicator_z - selection_offset, tex.right, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({BLOCK_SIZE, 0, selection_pos_abs.z - indicator_z - selection_offset, tex.right, tex.bottom, TEXTURE_TRANSPARENT});
            break;
        case AABB::BACK:
            nglAddVertex({BLOCK_SIZE, 0, selection_pos_abs.z - indicator_z + selection_offset, tex.left, tex.bottom, TEXTURE_TRANSPARENT});
            nglAddVertex({BLOCK_SIZE, BLOCK_SIZE, selection_pos_abs.z - indicator_z + selection_offset, tex.left, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({0, BLOCK_SIZE, selection_pos_abs.z - indicator_z + selection_offset, tex.right, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({0, 0, selection_pos_abs.z - indicator_z + selection_offset, tex.right, tex.bottom, TEXTURE_TRANSPARENT});
            break;
        case AABB::RIGHT:
            nglAddVertex({selection_pos_abs.x - indicator_x + selection_offset, 0, 0, tex.right, tex.bottom, TEXTURE_TRANSPARENT});
            nglAddVertex({selection_pos_abs.x - indicator_x + selection_offset, BLOCK_SIZE, 0, tex.right, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({selection_pos_abs.x - indicator_x + selection_offset, BLOCK_SIZE, BLOCK_SIZE, tex.left, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({selection_pos_abs.x - indicator_x + selection_offset, 0, BLOCK_SIZE, tex.left, tex.bottom, TEXTURE_TRANSPARENT});
            break;
        case AABB::LEFT:
            nglAddVertex({selection_pos_abs.x - indicator_x - selection_offset, 0, BLOCK_SIZE, tex.left, tex.bottom, TEXTURE_TRANSPARENT});
            nglAddVertex({selection_pos_abs.x - indicator_x - selection_offset, BLOCK_SIZE, BLOCK_SIZE, tex.left, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({selection_pos_abs.x - indicator_x - selection_offset, BLOCK_SIZE, 0, tex.right, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({selection_pos_abs.x - indicator_x - selection_offset, 0, 0, tex.right, tex.bottom, TEXTURE_TRANSPARENT});
            break;
        case AABB::TOP:
            nglAddVertex({0, selection_pos_abs.y - indicator_y + selection_offset, 0, tex.left, tex.bottom, TEXTURE_TRANSPARENT});
            nglAddVertex({0, selection_pos_abs.y - indicator_y + selection_offset, BLOCK_SIZE, tex.left, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({BLOCK_SIZE, selection_pos_abs.y - indicator_y + selection_offset, BLOCK_SIZE, tex.right, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({BLOCK_SIZE, selection_pos_abs.y - indicator_y + selection_offset, 0, tex.right, tex.bottom, TEXTURE_TRANSPARENT});
            break;
        case AABB::BOTTOM:
            nglAddVertex({BLOCK_SIZE, selection_pos_abs.y - indicator_y - selection_offset, 0, tex.left, tex.bottom, TEXTURE_TRANSPARENT});
            nglAddVertex({BLOCK_SIZE, selection_pos_abs.y - indicator_y - selection_offset, BLOCK_SIZE, tex.left, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({0, selection_pos_abs.y - indicator_y - selection_offset, BLOCK_SIZE, tex.right, tex.top, TEXTURE_TRANSPARENT});
            nglAddVertex({0, selection_pos_abs.y - indicator_y - selection_offset, 0, tex.right, tex.bottom, TEXTURE_TRANSPARENT});
            break;
        case AABB::NONE:
            break;
        }
        glEnd();

        glPopMatrix();
    }

    glPopMatrix();

    nglSetGlobalShade(256);

    // No crosshair while the player is asleep: there is nothing to aim at with
    // their eyes shut, and the fade is already darkening the screen.
    if(!isSleeping())
    {
        crosshairPixel(0, 0);
        crosshairPixel(-1, 0);
        crosshairPixel(-2, 0);
        crosshairPixel(0, -1);
        crosshairPixel(0, -2);
        crosshairPixel(1, 0);
        crosshairPixel(2, 0);
        crosshairPixel(0, 1);
        crosshairPixel(0, 2);
    }

    // In front of the world, behind the HUD: rain must not obscure the bars.
    renderWeather();

    renderHud();

    // Over everything, the HUD included: the night goes by behind a dark screen.
    renderSleepFade();

    frame_counter++;
}

void WorldTask::resetWorld()
{
    x = z = 0;
    y = world.worldType() == World::WorldType::Graph ? GLFix((world.graphOriginY() + 8) * BLOCK_SIZE) : GLFix(World::HEIGHT * Chunk::SIZE * BLOCK_SIZE);
    xr = yr = 0;
    world.generateSeed();
    if(world.worldType() != World::WorldType::Graph)
    {
        initLivestockEntities();
        initVillagerEntities();
        initCreeperEntities();
    }
    else
    {
        clearLivestockEntities();
        clearVillagerEntities();
        creeper_entities.clear();
    }
    // Villages are derived from the seed, so the registry starts empty and is
    // repopulated as the new world's chunks generate.
    Village::clearRegisteredPlans();
    // A brand new world starts at sunrise on day 0, so the same seed always
    // begins under the same sky.
    WorldClock::reset();
    clearGroundDrops();
    // A new world has no chests; a loaded one fills this in from its save file.
    clearChests();
    world.clear();
    current_inventory.reset();
    inventory_task.reset();
    block_list_task.current_selection = 1;

    // A new world is a fresh start: survival, and the weather its own seed and
    // clock imply rather than whatever the previous world was forced into. It has
    // no bed either, so the last world's is forgotten along with everything else.
    gamemode = 0;
    // The score is the player's, not the world's: a respawn keeps it (vanilla
    // keeps a player's score across death), but a new world has a new player.
    player_score = 0;
    clearWeatherOverride();
    bed_spawn = Bed::SpawnPoint();
    sleeping = false;
    sleep_ms = 0;

    resetSurvivalState();
    fall_distance = 0;
    safe_spawn_pending = world.worldType() != World::WorldType::Graph;

    vy = 0;
    can_jump = false;
    tp_had_contact = false;
    in_water = false;
    mining_pos = {-1, -1, -1};
    mining_progress = 0;
    mining_tick_accum = 0;
    sim_tick_accum = 0;
    graph_line_tick_accum = 0;
    message_timeout = 0;
    step_tracking = false;
    step_distance = 0;
    ambience_timer = 0;
    GameAudio::setAmbience(0);
    current_ambience = 0;
}

void WorldTask::respawnPlayer()
{
    // Respawn without wiping the world.
    resetSurvivalState();
    vy = 0;
    fall_distance = 0;
    safe_spawn_pending = world.worldType() != World::WorldType::Graph;
    in_water = false;
    can_jump = false;
    message_timeout = 0;

    // A bed that was slept in is where the player comes back to. They are dropped
    // in from just above the mattress and settled by the same down-ray that a
    // fresh spawn uses, so a bed that has since been built over still leaves them
    // on solid ground instead of inside a wall.
    if(bed_spawn.valid && world.worldType() != World::WorldType::Graph)
    {
        x = GLFix(bed_spawn.x * BLOCK_SIZE + BLOCK_SIZE / 2);
        z = GLFix(bed_spawn.z * BLOCK_SIZE + BLOCK_SIZE / 2);
        y = GLFix((bed_spawn.y + 1) * BLOCK_SIZE);
        return;
    }

    // Place above the world so the down-ray has something to hit.
    y = world.worldType() == World::WorldType::Graph ? GLFix((world.graphOriginY() + 8) * BLOCK_SIZE) : GLFix(World::HEIGHT * Chunk::SIZE * BLOCK_SIZE);
}

void WorldTask::setGamemode(int mode)
{
    gamemode = mode == 1 ? 1 : 0;

    if(isCreative())
    {
        // The state is settled here rather than left to be fixed up on the next
        // survival tick: /gamemode is instant, and the HUD draws before that tick
        // would have run.
        health = Survival::MaxHealth;
        air = Survival::MaxAir;
        fire_ticks = 0;
        clearEffects();
    }

    setMessage(isCreative() ? "Creative mode" : "Survival mode");
}

void WorldTask::teleportTo(int block_x, int block_y, int block_z)
{
    x = GLFix(block_x * BLOCK_SIZE + BLOCK_SIZE / 2);
    y = GLFix(block_y * BLOCK_SIZE);
    z = GLFix(block_z * BLOCK_SIZE + BLOCK_SIZE / 2);

    // Nothing from the old place may follow the player: a fall in progress would
    // otherwise be paid for on arrival, and a jump in progress would carry over.
    vy = 0;
    fall_distance = 0;
    can_jump = false;
    in_water = false;
    tp_had_contact = false;
    // The down-ray that settles a fresh spawn would move the player off the spot
    // that was just asked for, so it is told not to run.
    safe_spawn_pending = false;

    // Loading the chunks around the destination now, rather than waiting for the
    // next frame, is what keeps a teleport from arriving in unloaded air.
    world.setPosition(block_x, block_y, block_z);
    world.setDirty();
}

void WorldTask::setWeatherOverride(int state, unsigned int seconds)
{
    // The override is counted in game ticks, because that is the unit the weather
    // module speaks and the unit the clock advances in, so a duration in real
    // seconds has to go through the configured day length. Four days is the
    // ceiling: past that the natural weather has plainly been replaced for good,
    // and the setting is the place to make that choice.
    const unsigned int day_seconds = WorldClock::dayLengthSeconds();
    const unsigned long long day_ticks = WorldClock::TicksPerDay;
    unsigned long long ticks = day_seconds == 0
        ? day_ticks
        : static_cast<unsigned long long>(seconds) * day_ticks / day_seconds;

    if(ticks > day_ticks * 4ULL)
        ticks = day_ticks * 4ULL;
    if(ticks == 0)
        ticks = 1;

    weather_override_state = state;
    weather_override_ticks = static_cast<unsigned int>(ticks);
}

void WorldTask::clearWeatherOverride()
{
    weather_override_state = -1;
    weather_override_ticks = 0;
}

void WorldTask::setMessage(const char *message)
{
    if(strlen(message) >= sizeof(this->message))
        return;

    strcpy(this->message, message);
    message_timeout = 50;
}
