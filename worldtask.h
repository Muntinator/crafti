#ifndef WORLDTASK_H
#define WORLDTASK_H

#include "task.h"
#include "world.h"
#include "gl.h"
#include "aabb.h"
#include "bed.h"
#include "enchanting.h"
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

    /**
     * Damage from a blast (the creeper's). It is the same path as hurtPlayer except
     * for what it is called, which is what lets Blast Protection answer it: an
     * explosion is not a hit, and vanilla's armour enchantments care about the
     * difference.
     */
    void hurtPlayerByExplosion(unsigned int dmg, const char *msg = "Kaboom!");

    unsigned int frameCount() { return frame_counter; }

    // --- melee and hunger (enchanting.h, worldsurvival.cpp) ------------------

    /**
     * What one melee hit with the held stack is worth, in half-hearts: the weapon's
     * own damage (ItemRules::attackDamage, vanilla's material table), plus what its
     * Sharpness is worth and, against the kind of target it names, its Smite or
     * Bane of Arthropods, all scaled by the Strength effect. This is the number
     * worldtask.cpp hands to a mob when the player hits it.
     */
    int heldMeleeDamage(Enchanting::TargetKind target) const;

    /** Charges the hunger for one action (vanilla's exhaustion per attack). */
    void addExhaustion(float amount) { Survival::addExhaustion(hunger, amount); }

    // --- experience (enchanting.h, command.cpp) -----------------------------

    /** Adds experience and nothing else; the HUD derives the level from it. */
    void addExperience(int amount);
    /** The running experience total, which the HUD turns into a level and a bar. */
    int experience() const { return total_xp; }
    /**
     * Replaces the total. A table or an anvil charges whole levels, and a level is
     * not a fixed number of points, so spending one has to go through the level
     * curve rather than by subtracting experience (`Survival::totalXpForLevel`).
     */
    void setExperience(int amount) { total_xp = amount < 0 ? 0 : amount; }

    // --- the score (deathtask.cpp, worldcommands.cpp) -----------------------

    /**
     * The player's score, which is vanilla's `Player.getScore()` -- the value the
     * death screen's "Score:" line shows. It is *not* experience: vanilla keeps
     * the two apart, and its score is the scoreboard's, which starts at zero in a
     * world that has no scoreboard. This engine has no scoreboard either, so the
     * score starts at zero, is left alone by a respawn (vanilla keeps a player's
     * score across death) and is only reset when a new world is made.
     */
    int score() const { return player_score; }
    void setScore(int amount) { player_score = amount < 0 ? 0 : amount; }

    // --- debug commands (worldcommands.cpp, commandtask.cpp) ----------------

    /**
     * Play mode, numbered as Command::parseGamemode does: 0 survival, 1 creative.
     * Creative is the debug mode from /gamemode -- nothing hurts, nothing is
     * consumed and blocks break at a touch -- and, unlike the weather override, it
     * is part of the save file (format version 11), because a player who switched
     * it on expects it to still be on when the world is opened tomorrow.
     */
    int gamemode = 0;
    bool isCreative() const { return gamemode != 0; }
    void setGamemode(int mode);

    /**
     * Puts the player at a block position, with the movement state a teleport
     * needs: no leftover velocity, no fall distance carried over from the old
     * place, and no pending down-ray that would move them again immediately.
     */
    void teleportTo(int block_x, int block_y, int block_z);

    /**
     * Forces the weather for `seconds` of real time, after which the natural spell
     * from weather.h resumes. `state` is a Weather::State. This is deliberately
     * *not* saved: it is a look-at-the-rain switch, and a world opened tomorrow
     * should have the weather its own seed and clock imply.
     */
    void setWeatherOverride(int state, unsigned int seconds);
    void clearWeatherOverride();
    int weatherState() const { return weather.state; }
    int weatherOverrideState() const { return weather_override_state; }
    unsigned int weatherOverrideTicks() const { return weather_override_ticks; }

    // --- beds (bed.h, bedrenderer.cpp, worldbed.cpp) -------------------------

    /**
     * Tries to sleep in the bed at this block. `data` is the data byte of the half
     * that was used and `complete` says whether the other half is really there,
     * which the renderer already worked out (bedrenderer.cpp).
     *
     * The outcome is reported through setMessage(). The click is always consumed:
     * a bed answers when it is used even when the answer is "you can only sleep at
     * night", and returning false would make the world task try to place a block
     * into it instead.
     */
    bool trySleepInBed(int x, int y, int z, uint8_t data, bool complete);

    /**
     * Forgets the respawn bed when the half at this position belongs to it. Broken
     * beds are not respawn points, so breaking either half -- or replacing one
     * with /setblock -- drops the record.
     */
    void forgetBedSpawn(int x, int y, int z, uint8_t data);

    /** The bed the player last slept in, which is where they come back after dying. */
    Bed::SpawnPoint bedSpawn() const { return bed_spawn; }
    /** Puts a saved respawn bed back into place (save format version 12). */
    void restoreBedSpawn(bool valid, int x, int y, int z);

    /** True while the player is asleep and the screen is fading. */
    bool isSleeping() const { return sleeping; }

    /**
     * The player's own box. The world task refreshes it at the top of render(),
     * and blocks that place more than one cell (the bed) ask for it so that they
     * can take back a placement that would trap whoever made it.
     */
    AABB playerBox() const { return aabb; }

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
    /**
     * The debug screen, drawn when "Show FPS" is on (worldhud.cpp): the clock, the
     * position, the weather and the frame rate, stacked in the top-left corner.
     */
    void renderDebugOverlay();
    /** Single pixel of the sky, in coordinates relative to the screen centre. */
    void skyPixel(int x, int y, unsigned short color);
    /**
     * A round celestial body centred on a celestial position. `phase` is the
     * moon's phase, or `SkyBodyFull` for a body that is always whole (the sun).
     */
    void skyBody(int azimuth, int elevation, int radius, unsigned short color, int phase = SkyBodyFull);
    /** A dim disc drawn behind a body, for the glow around a rising or setting sun. */
    void skyHalo(int azimuth, int elevation, int radius, unsigned short color);
    static constexpr int SkyBodyFull = -1;

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
     * Draws the rain streaks, the snow flakes and a lightning flash straight into
     * the framebuffer, after the 3D scene and before the HUD. Nothing is drawn
     * when it is clear or when the player is under a roof.
     */
    void renderWeather();

    /**
     * The weather touching the world: the snow that settles around the player and
     * the strike that has just landed. Both are in worldweathersnow.cpp; the part
     * that must be decided with the rest of the frame (what is falling, and
     * whether it is freezing where the player stands) is passed in.
     */
    void updateSnowCover(unsigned long long total_ticks, bool snowing, bool freezing);
    void resolveLightningStrike(unsigned long long total_ticks);

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

    /**
     * Armour points' worth of enchantment protection the worn pieces add against
     * one kind of damage. Vanilla counts the general Protection for every kind and
     * the enchantment named for the kind on top of it (enchanting.h), and the four
     * pieces' contributions add up; the result goes to ItemRules with the armour's
     * own points, so an enchanted suit reduces a hit the way it does in Minecraft.
     */
    int armorProtectionPoints(Survival::Damage source) const;

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

    /** The player's score, which the death screen shows. See score(). */
    int player_score = 0;

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

    // --- beds ---
    /**
     * Runs the short fade a sleep plays. The night itself is skipped the moment
     * the player lies down (the clock jumps to the next dawn), so this is only the
     * screen going dark and coming back, with the "good morning" at the end of it.
     */
    void updateSleep(GLFix dt);
    /** Paints the sleep fade over the whole screen, HUD included. */
    void renderSleepFade();

    /** Where the player wakes up after dying: the foot cell of the bed slept in. */
    Bed::SpawnPoint bed_spawn;
    bool sleeping = false;
    /**
     * Milliseconds left of the fade, counted down from Bed::FadeTotalMs. Its
     * length and its darkness are bed.h's, because they are the same on every
     * platform and the host tests pin them down.
     */
    unsigned int sleep_ms = 0;

    // --- weather ---
    /**
     * A forced weather state (-1 when there is none) and how many game ticks it
     * has left. Both live here rather than in weather.h because the module is a
     * pure function of the clock, and an override is by definition not.
     */
    int weather_override_state = -1;
    unsigned int weather_override_ticks = 0;

    /** The spell weather.h resolved for this frame (its last state is compared). */
    Weather::Spell weather;
    /** Sky darkening from the weather, 0..Weather::MaxIntensity. */
    int weather_darkness = 0;
    /**
     * Precipitation strength, 0..Weather::MaxIntensity, whether it is rain or
     * snow: the renderer scales the number of drops and of flakes alike.
     */
    int weather_rain = 0;
    /** True while a thunder flash is lit. */
    bool weather_lightning = false;
    /**
     * What is falling this frame, a Weather::Precipitation, and whether the place
     * the player stands is cold enough for water to freeze there. Both are decided
     * from the biome temperature field (biomegen.h) once a frame, which is what
     * makes a shower rain over a plains and snow over a cold forest.
     */
    int weather_precipitation = Weather::NoPrecipitation;
    bool weather_freezing = false;
    /**
     * The flash of the previous frame. A strike is resolved on the rising edge of
     * the flash only, so the five ticks a flash lasts land one bolt, not five.
     */
    bool weather_lightning_previous = false;
    /**
     * The last looping weather sound started, so it is started once per change
     * instead of once per frame (starting a stream rewinds it).
     */
    unsigned int weather_audio = 0;
    /**
     * The snow step last applied. The step index is derived from the world clock,
     * so a reloaded world carries on with the snowfall it was having rather than
     * replaying it, and a world with the clock stopped has no snowfall at all.
     */
    unsigned long long snow_step_index = 0;
    /** False when there is a block between the player's head and the sky. */
    bool weather_outdoors = true;
    /** Falling offset of the rain in screen pixels, so it moves with real time. */
    int rain_offset = 0;
};

extern WorldTask world_task;

#endif // WORLDTASK_H
