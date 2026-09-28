// Player survival for WorldTask: hunger, health, breath, burning, status
// effects, eating and experience.
//
// These are WorldTask members, but they are kept out of worldtask.cpp because
// that file is already large and this is a self-contained rules layer over the
// member state. Everything here defers the actual numbers to survival.h, which
// is pure and host-tested, so this file is the thin part that must exist on the
// calculator.

#include "worldtask.h"

#include "audio_manager.h"
#include "deathtask.h"
#include "font.h"
#include "inventory.h"
#include "settingstask.h"
#include "survival.h"

void WorldTask::resetSurvivalState()
{
    health = Survival::MaxHealth;
    Survival::resetHunger(hunger);
    air = Survival::MaxAir;
    fire_ticks = 0;
    drown_timer = Survival::DrownDamageInterval;
    fire_timer = Survival::FireDamageInterval;
    suffocate_timer = Survival::SuffocateInterval;
    hunger_timer = 0;
    survival_tick_accum = 0;
    exhaustion_tracking = false;
    clearEffects();
    total_xp = 0;
}

int WorldTask::effectAmplifier(uint8_t effect) const
{
    for(unsigned int i = 0; i < effect_count; ++i)
        if(effects[i].effect == effect)
            return static_cast<int>(effects[i].amplifier);
    return -1;
}

void WorldTask::removeEffect(unsigned int index)
{
    if(index >= effect_count)
        return;

    for(unsigned int i = index; i + 1 < effect_count; ++i)
    {
        effects[i] = effects[i + 1];
        effect_tick[i] = effect_tick[i + 1];
    }
    --effect_count;
}

void WorldTask::addEffect(uint8_t effect, uint16_t duration, uint8_t amplifier)
{
    if(effect >= Survival::EffectCount)
        return;

    // Instant effects act once and are not stored.
    if(Survival::effectIsInstant(effect))
    {
        if(effect == Survival::InstantHealth)
        {
            health += Survival::absorptionHealth(amplifier);
            if(health > Survival::MaxHealth)
                health = Survival::MaxHealth;
        }
        else if(health > Survival::StarvationHealthFloor)
            applyDamage(4 * (static_cast<int>(amplifier) + 1), Survival::Damage::Magic);
        return;
    }

    for(unsigned int i = 0; i < effect_count; ++i)
    {
        if(effects[i].effect != effect)
            continue;
        // Refresh, keeping whichever of the two is stronger.
        if(amplifier >= effects[i].amplifier)
        {
            effects[i].amplifier = amplifier;
            effects[i].duration = duration;
            effect_tick[i] = Survival::effectDamageInterval(effect);
            if(effect_tick[i] <= 0)
                effect_tick[i] = Survival::regenerationIntervalForEffect(amplifier);
        }
        else if(duration > effects[i].duration)
            effects[i].duration = duration;
        return;
    }

    if(effect_count >= Survival::MaxActiveEffects)
        return; // A full list simply keeps what it has rather than dropping one.

    effects[effect_count].effect = effect;
    effects[effect_count].duration = duration;
    effects[effect_count].amplifier = amplifier;
    effect_tick[effect_count] = Survival::effectDamageInterval(effect);
    if(effect_tick[effect_count] <= 0)
        effect_tick[effect_count] = Survival::regenerationIntervalForEffect(amplifier);
    ++effect_count;
}

void WorldTask::clearEffects()
{
    effect_count = 0;
}

void WorldTask::applyDamage(int amount, Survival::Damage source, const char *msg)
{
    if(amount <= 0 || health <= 0)
        return;

    // Fire resistance stops fire and lava completely; resistance scales the rest,
    // except for the sources vanilla lets bypass armour (starvation, drowning,
    // the void, poison, magic).
    if(Survival::blockedByFireResistance(source) && effectAmplifier(Survival::FireResistance) >= 0)
        return;

    const int resistance = effectAmplifier(Survival::Resistance);
    if(resistance >= 0 && !Survival::ignoresResistance(source))
    {
        amount = static_cast<int>(static_cast<float>(amount) * Survival::resistanceMultiplier(static_cast<uint8_t>(resistance)) + 0.5f);
        if(amount < 1)
            amount = 1;
    }

    // Absorption is a shield that is spent before health.
    const int absorption = effectAmplifier(Survival::Absorption);
    if(absorption >= 0)
    {
        const int pool = Survival::absorptionHealth(static_cast<uint8_t>(absorption));
        if(amount > pool)
            amount -= pool;
        else
            amount = 0;
    }

    if(amount > 0)
        health -= amount;

    GameAudio::play(GameAudio::EventPlayerDamage);

    if(health <= 0)
    {
        health = 0;
        // The death screen owns the player from here; the caller must not keep
        // simulating damage or hunger against a dead player.
        death_task.makeCurrent();
        return;
    }

    if(msg && msg[0])
        setMessage(msg);
}

void WorldTask::hurtPlayer(unsigned int dmg, const char *msg)
{
    // Mob and explosion damage: reduced by resistance, but not fire resistance.
    applyDamage(static_cast<int>(dmg), Survival::Damage::Mob, msg);
}

int WorldTask::survivalSteps(GLFix dt)
{
    // The rules module counts intervals in 20-a-second ticks. dt is in units of
    // the platform's nominal tick, so convert through real milliseconds and carry
    // the remainder, exactly like the day/night clock does.
    survival_tick_accum += dt * GLFix(static_cast<int>(simulation_tick_ms)) / GLFix(50);

    const int steps = survival_tick_accum.toInteger<int>();
    if(steps <= 0)
        return 0;

    survival_tick_accum -= GLFix(steps);

    // A hitch (or a debugger pause) must not deal a burst of damage at once.
    return steps > MaxSurvivalStepsPerFrame ? MaxSurvivalStepsPerFrame : steps;
}

bool WorldTask::headInsideBlock() const
{
    const BLOCK_WDATA head = world.getBlock((x / BLOCK_SIZE).floor(),
                                           ((y + eye_pos) / BLOCK_SIZE).floor(),
                                           (z / BLOCK_SIZE).floor());
    const BLOCK type = getBLOCK(head);
    return type != BLOCK_AIR && type != BLOCK_WATER && type != BLOCK_WATER_FAST;
}

void WorldTask::updateSurvival(GLFix dt)
{
    // A graph view is a plot of a function: no survival, no hunger, no damage.
    if(world.worldType() == World::WorldType::Graph)
        return;

    const int steps = survivalSteps(dt);
    if(steps <= 0)
        return;

    // --- Status effects: count down, then act on their own cadence ---
    for(unsigned int i = 0; i < effect_count; )
    {
        Survival::EffectInstance &e = effects[i];

        // 0xFFFF means "infinite" (a duration that never decays).
        if(e.duration != 0xFFFF)
        {
            if(e.duration <= static_cast<uint16_t>(steps))
            {
                removeEffect(i);
                continue; // the next effect has moved into this slot
            }
            e.duration = static_cast<uint16_t>(e.duration - steps);
        }

        const int damage_interval = Survival::effectDamageInterval(e.effect);
        if(damage_interval > 0)
        {
            effect_tick[i] -= steps;
            if(effect_tick[i] <= 0)
            {
                effect_tick[i] = damage_interval;
                const int amount = Survival::effectDamageAmount(e.effect, e.amplifier);
                const Survival::Damage source = e.effect == Survival::Wither ? Survival::Damage::Wither
                                                                             : Survival::Damage::Poison;
                // Vanilla poison cannot kill, so it stops at the health floor.
                if(amount > 0 && (!Survival::stopsAtOneHeart(e.effect) || health > Survival::StarvationHealthFloor))
                    applyDamage(amount, source, e.effect == Survival::Wither ? "Withering!" : "Poisoned!");
                if(health <= 0)
                    return;
            }
        }
        else if(e.effect == Survival::Regeneration)
        {
            effect_tick[i] -= steps;
            if(effect_tick[i] <= 0)
            {
                effect_tick[i] = Survival::regenerationIntervalForEffect(e.amplifier);
                if(health < Survival::MaxHealth)
                    ++health;
            }
        }

        ++i;
    }

    // --- Breath ---
    if(in_water)
    {
        if(air > 0)
            air -= steps;
        if(air <= 0)
        {
            air = 0;
            drown_timer -= steps;
            if(drown_timer <= 0)
            {
                drown_timer = Survival::DrownDamageInterval;
                applyDamage(Survival::DrownDamage, Survival::Damage::Drown, "Drowning!");
                if(health <= 0)
                    return;
            }
        }
    }
    else if(air < Survival::MaxAir)
    {
        air += steps * Survival::AirRecoveryPerTick;
        if(air > Survival::MaxAir)
            air = Survival::MaxAir;
        drown_timer = Survival::DrownDamageInterval;
    }

    // --- Burning ---
    if(fire_ticks > 0)
    {
        fire_ticks -= steps;
        if(in_water)
            fire_ticks = 0; // stepping into water puts it out
        else
        {
            fire_timer -= steps;
            if(fire_timer <= 0)
            {
                fire_timer = Survival::FireDamageInterval;
                applyDamage(Survival::FireDamage, Survival::Damage::Fire, "Burning!");
                if(health <= 0)
                    return;
            }
        }
    }

    // --- Suffocation (standing inside a block) ---
    if(headInsideBlock())
    {
        suffocate_timer -= steps;
        if(suffocate_timer <= 0)
        {
            suffocate_timer = Survival::SuffocateInterval;
            applyDamage(Survival::SuffocateDamage, Survival::Damage::Suffocate, "Suffocating!");
            if(health <= 0)
                return;
        }
    }
    else
        suffocate_timer = Survival::SuffocateInterval;

    // --- Falling out of the world ---
    if(y < GLFix(0))
    {
        applyDamage(Survival::VoidDamage, Survival::Damage::Void, "The void!");
        if(health <= 0)
            return;
    }

    // --- Hunger: regeneration while well fed, starvation when empty ---
    hunger_timer -= steps;
    for(int guard = 0; hunger_timer <= 0 && guard < 8; ++guard)
    {
        const int regen_interval = Survival::regenerationIntervalTicks(hunger);
        if(regen_interval > 0 && health < Survival::MaxHealth)
        {
            ++health;
            // Healing draws on the same food the player ate.
            Survival::addExhaustion(hunger, 6.0f);
            hunger_timer += regen_interval;
        }
        else
        {
            const int starve_interval = Survival::starvationIntervalTicks(hunger);
            if(starve_interval > 0)
            {
                if(health > Survival::StarvationHealthFloor)
                    applyDamage(Survival::StarvationDamage, Survival::Damage::Starve, "Starving!");
                hunger_timer += starve_interval;
            }
            else
            {
                // Nothing to do: look again in four seconds. This is what makes
                // hunger drain visible without checking it every frame.
                hunger_timer += 80;
            }
        }

        if(health <= 0)
            return;
        if(hunger_timer > 0)
            break;
    }
}

bool WorldTask::tryEat()
{
    const BLOCK_WDATA held = current_inventory.currentSlot();
    if(getBLOCK(held) != BLOCK_ITEM)
        return false;

    // getITEMDATA: a food id of 128 or more would otherwise look like another
    // item entirely (see terrain.h).
    const Survival::FoodValue *food = Survival::foodValue(getITEMDATA(held));
    if(food == nullptr)
        return false;

    // A full player simply keeps the food rather than wasting it.
    if(!Survival::canEat(hunger))
    {
        setMessage("Not hungry");
        return true;
    }

    Survival::eat(hunger, *food);
    current_inventory.removeFromCurrentSlot();

    // Food effects are part of the food table, so eating and its consequences
    // cannot disagree.
    if(food->effect.effect != Survival::NoEffect)
    {
        const uint16_t duration = Survival::secondsToTicks(food->effect.seconds);
        addEffect(food->effect.effect, duration, food->effect.amplifier);
        setMessage(Survival::effectName(food->effect.effect));
    }
    if(food->extra.effect != Survival::NoEffect)
        addEffect(food->extra.effect, Survival::secondsToTicks(food->extra.seconds), food->extra.amplifier);

    GameAudio::play(GameAudio::EventMenuSelect);
    return true;
}

void WorldTask::addExperience(int amount)
{
    if(amount <= 0)
        return;
    total_xp += amount;
}
