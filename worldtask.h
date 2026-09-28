#ifndef WORLDTASK_H
#define WORLDTASK_H

#include "task.h"
#include "world.h"
#include "gl.h"
#include "aabb.h"
#include "survival.h"
#include "weather.h"

class WorldTask : public Task
{
public:
    virtual void makeCurrent() override;

    virtual void logic(GLFix dt) override;
    virtual void render() override;

    void resetWorld();
    void respawnPlayer();

    GLFix x, y = World::HEIGHT * Chunk::SIZE * BLOCK_SIZE, z, xr, yr;

    static constexpr GLFix player_width = BLOCK_SIZE*0.8f, player_height = BLOCK_SIZE*1.8f, eye_pos = BLOCK_SIZE*1.6f;

    void setMessage(const char *message);

    /** Player damage from mobs / hazards (hearts, death screen, optional HUD message). */
    void hurtPlayer(unsigned int dmg, const char *msg = "Ouch!");

    unsigned int frameCount() { return frame_counter; }

private:
    void crosshairPixel(int x, int y);

    /**
     * Clears the frame and paints the sky: the time-of-day colour, then the sun,
     * moon and stars as pixels in screen space, so they sit behind the terrain
     * that is drawn over them. Uses the day/night setting; when it is off the sky
     * is the flat daytime blue the game has always used.
     */
    void renderSky();
    /**
     * The 2D overlay: survival bars, hotbar, messages, coordinate readout and the
     * graph view's labels. Drawn after the 3D scene, in screen space.
     */
    void renderHud();
    /** Single pixel of the sky, in coordinates relative to the screen centre. */
    void skyPixel(int x, int y, unsigned short color);
    /** Square body (sun/moon) centred on a celestial position. */
    void skyBody(int azimuth, int elevation, int radius, unsigned short color);

    /** Advances the day/night clock by the frame's real elapsed time. */
    void updateClock(GLFix dt);

    // --- Weather ----------------------------------------------------------

    /**
     * Asks weather.h what the weather is now, announces a change, and advances
     * the rain's fall. Called once per logic step; the rules module is a pure
     * function of the world clock, so this holds no weather state of its own.
     */
    void updateWeather(GLFix dt);
    /**
     * Draws the rain streaks and a lightning flash straight into the framebuffer,
     * after the 3D scene and before the HUD. Nothing is drawn when it is clear or
     * when the player is under a roof.
     */
    void renderWeather();

    // --- Survival ---------------------------------------------------------

    /**
     * Whole 1/20-second survival ticks elapsed this frame, carrying the
     * remainder. The rules module states its intervals in those ticks (Minecraft's
     * rate), so they are converted from real time rather than counted in frames,
     * which is what keeps a 3 Hz CX logic loop and a 30 Hz desktop loop agreeing.
     */
    int survivalSteps(GLFix dt);
    /** Hunger, regeneration, starvation, breath, burning, suffocation, effects. */
    void updateSurvival(GLFix dt);
    /** Puts the survival state back to that of a fresh player (new world, respawn). */
    void resetSurvivalState();
    /** True when the head is inside a block, which suffocates. */
    bool headInsideBlock() const;
    /**
     * Applies damage after the defences the rules module describes: fire
     * resistance blocks fire and lava outright, resistance scales the rest, and
     * absorption damage is spent before health. Switches to the death screen at
     * zero health, so callers must stop touching player state afterwards.
     */
    void applyDamage(int amount, Survival::Damage source, const char *msg = nullptr);

    /** Amplifier of an active effect, or -1 when it is not active. */
    int effectAmplifier(uint8_t effect) const;
    /** Adds or refreshes an effect (keeping the stronger of the two). */
    void addEffect(uint8_t effect, uint16_t duration, uint8_t amplifier);
    void removeEffect(unsigned int index);
    void clearEffects();

    /**
     * Eats the held food when it is edible and the player can eat. Returns true
     * when something was consumed, so the caller does not then try to place it.
     */
    bool tryEat();

    /** Adds experience and nothing else; the HUD derives the level from it. */
    void addExperience(int amount);

    /**
     * Serialises the survival state for the debug screen and the save file. Not
     * used by the save (which reuses the members) but keeps the field list in one
     * place if it grows.
     */
    int healthPoints() const { return health; }
    int airTicks() const { return air; }

    void getForward(GLFix *x, GLFix *z);
    void getRight(GLFix *x, GLFix *z);

    GLFix speed();

    //Player position and movement
    AABB aabb;
    bool can_jump = false, tp_had_contact = false;
    int tp_last_x = 0, tp_last_y = 0;
    GLFix vy = 0; //Y-Velocity for gravity and jumps
    bool in_water = false;
    bool speed_multiplier_held = false; // V key for 10x speed

    // --- Survival / combat (Minecraft-ish) ---
    static constexpr unsigned int max_hearts = 10;

    /** Hit points, 0..Survival::MaxHealth. Two points make one HUD heart. */
    int health = Survival::MaxHealth;

    /** Hunger drumsticks (0 = empty … max_food = full), HUD like MC food bar. */
    static constexpr unsigned int max_food = 10;
    Survival::HungerState hunger;

    /** Breath in survival ticks; full while the head is out of water. */
    int air = Survival::MaxAir;
    /** Ticks left burning, and the cadence of the damage it deals. */
    int fire_ticks = 0;
    /** Cadences for the periodic environmental hazards. */
    int drown_timer = Survival::DrownDamageInterval;
    int fire_timer = Survival::FireDamageInterval;
    int suffocate_timer = Survival::SuffocateInterval;
    int hunger_timer = 0;

    /** Active status effects and, per slot, the cadence of its periodic action. */
    Survival::EffectInstance effects[Survival::MaxActiveEffects];
    int effect_tick[Survival::MaxActiveEffects] = {};
    unsigned int effect_count = 0;

    /** Accumulated experience; the HUD level and bar are derived from it. */
    int total_xp = 0;

    /**
     * Cap on the survival ticks a single frame may advance by. One second is
     * generous (the CX logic loop runs at about three frames a second) and stops
     * a stall from being paid for all at once as a burst of damage.
     */
    static constexpr int MaxSurvivalStepsPerFrame = 20;

    /** Fractional 1/20-second survival ticks carried across frames. */
    GLFix survival_tick_accum = 0;
    /** Last position sampled for movement exhaustion. */
    GLFix exhaustion_x = 0, exhaustion_z = 0;
    bool exhaustion_tracking = false;

    // Accumulated downward travel since last time we touched ground.
    GLFix fall_distance = 0;

    // After reset we want to place the player on solid ground (1 block above it)
    // so the first fall doesn't instantly kill the player once fall damage is enabled.
    bool safe_spawn_pending = true;

    VECTOR3 mining_pos = {-1, -1, -1};
    int mining_progress = 0;
    int mining_duration = 0;

    static constexpr unsigned int blockselection_frames = 2;
    unsigned int blockselection_frame = 0, blockselection_frame_fraction = 0;

    VECTOR3 selection_pos; AABB::SIDE selection_side; VECTOR3 selection_pos_abs; bool do_test = true; //For intersectsRay

    char message[40]; unsigned int message_timeout = 0;

    bool draw_inventory = true;
    unsigned int frame_counter = 0; // Incremented after each render

    /** Fractional simulation ticks carried across frames (mobs / ground drops). */
    GLFix sim_tick_accum = 0;
    /** Fractional mining progress toward the next integer tick. */
    GLFix mining_tick_accum = 0;

    /** Accumulator for graph line-by-line reveal timing. */
    GLFix graph_line_tick_accum = 0;

    // --- audio ---
    /** Horizontal distance walked since the last footstep sound. */
    GLFix step_distance = 0;
    GLFix last_step_x = 0, last_step_z = 0;
    bool step_tracking = false;
    /** Ticks until the next cave ambience cue. */
    unsigned int ambience_timer = 0;
    /** Ambience bed currently requested (0 = none), so it is not restarted. */
    unsigned int current_ambience = 0;

    // --- weather ---
    /** The spell weather.h resolved for this frame (its last state is compared). */
    Weather::Spell weather;
    /** Sky darkening from the weather, 0..Weather::MaxIntensity. */
    int weather_darkness = 0;
    /** Rain strength, 0..Weather::MaxIntensity. */
    int weather_rain = 0;
    /** True while a thunder flash is lit. */
    bool weather_lightning = false;
    /** False when there is a block between the player's head and the sky. */
    bool weather_outdoors = true;
    /** Falling offset of the rain in screen pixels, so it moves with real time. */
    int rain_offset = 0;
};

extern WorldTask world_task;

#endif // WORLDTASK_H
