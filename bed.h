#ifndef BED_H
#define BED_H

#include <stdint.h>

#include "terrain.h"
#include "worldclock.h"

/**
 * The rules of a bed: how its two halves are encoded, which way it points, when
 * it can be slept in and where the player wakes up.
 *
 * A bed is two blocks, a foot and a head, laid flat in the same cell plane and
 * sharing one facing, which is what makes it the first *horizontal* multi-block
 * structure in the game -- the door is two blocks stacked vertically. The facing
 * is stored as a BLOCK_SIDE in the low three bits of the data byte and the
 * head/foot flag in bit 3, exactly the way doorrenderer.h packs the door's own
 * flags, so the two follow the same conventions:
 *
 *   data = facing | (head ? HeadBit : 0)      facing is BLOCK_FRONT .. BLOCK_RIGHT
 *
 * `facing` is the direction *from the foot to the head*, so the head is always
 * found at `offset(facing)` from the foot and the foot at `offset(facing)` back
 * from the head. Both halves carry the same value, which means either one can
 * find the other without a lookup table, and the renderer can tell which end of
 * the bed it is drawing from the block it was handed.
 *
 * Nothing here includes nGL or touches the world, so the encoding, the offsets,
 * the yaw mapping and the sleep decision are all unit tested on the host
 * (tests/bed_test.cc). The block ids are passed in as terrain.h constants, the
 * same way blocklight.h and biomegen.h do it.
 */
namespace Bed
{
    /** Data bit 3: this half is the head, not the foot. */
    constexpr uint8_t HeadBit = 1 << 3;

    /** True for the four sides a bed may lie along (a bed is never vertical). */
    bool isHorizontal(BLOCK_SIDE side);

    /** The data byte for one half. `head` selects which half it is. */
    constexpr uint8_t dataFor(BLOCK_SIDE facing, bool head)
    {
        return static_cast<uint8_t>((static_cast<uint8_t>(facing) & BLOCK_SIDE_BITS) | (head ? HeadBit : 0));
    }

    constexpr bool isHead(uint8_t data) { return (data & HeadBit) != 0; }

    /** The facing stored in a data byte, with the head flag removed. */
    constexpr BLOCK_SIDE facing(uint8_t data)
    {
        return static_cast<BLOCK_SIDE>(data & BLOCK_SIDE_BITS);
    }

    /**
     * Whether a data byte could have come from a placed bed. A bed that was
     * written straight into the world (by /setblock, or by an older save) can
     * carry a vertical facing, which is meaningless for it, so anything that
     * reads the data checks this first and treats a failure as a broken half
     * rather than as a bed pointing into the ground.
     */
    bool isValidData(uint8_t data);

    /** The block offset of `side`. Vertical sides give a zero offset. */
    void sideOffset(BLOCK_SIDE side, int &dx, int &dy, int &dz);

    /**
     * The offset from a bed half to its partner. It is the same offset either
     * way round, because both halves carry the same facing.
     */
    void partnerOffset(uint8_t data, int &dx, int &dy, int &dz);

    /**
     * Which way the head of a bed points when a player looking along `yaw`
     * places one.
     *
     * The head goes *in front* of the player, which is where a bed's pillow ends
     * up in Minecraft, and yaw 0 looks along +z in this engine (the convention
     * getForward() uses). The placement side the world task derives from the yaw
     * is not reused here on purpose: that value names the face of the block the
     * player is looking at, which is the opposite direction, and the two
     * spellings of "which way" are exactly the sort of thing that goes wrong
     * silently.
     */
    BLOCK_SIDE facingForYaw(int yaw_degrees);

    // --- sleeping ----------------------------------------------------------

    enum SleepResult
    {
        CanSleep = 0,
        /** It is day: there is nothing to skip to. */
        NotNight,
        /** The other half of the bed is missing or does not match. */
        BedBroken,
    };

    /**
     * Whether the player may sleep now. `storming` allows it in daylight, the
     * same way a thunderstorm does in Minecraft, and it is passed in rather than
     * read from weather.h so that this stays a pure decision.
     */
    SleepResult canSleep(bool night, bool storming, bool bed_complete);

    /** The line the HUD shows for a sleep result (also used for the refusal). */
    const char *sleepMessage(SleepResult result);

    /**
     * True when two data bytes are the two halves of *one* bed: the same facing
     * and opposite ends. Two feet side by side are two beds that happen to touch,
     * and treating those as one pair would drop a face that is really there (the
     * renderer) and let a player sleep in a bed that does not exist (the world).
     */
    bool isPartner(uint8_t data, uint8_t partner_data);

    // --- the sleep fade ----------------------------------------------------

    /** How long the fade lasts, in real milliseconds. */
    constexpr unsigned int FadeTotalMs = 900;
    /** How dark the middle of the fade gets, out of 256. */
    constexpr unsigned int FadeMaxAlpha = 232;
    /**
     * The darkening of a fade with `remaining_ms` left of `total_ms`: nothing at
     * either end, `max_alpha` in the middle, and never more than `max_alpha`. The
     * night itself is skipped when the player lies down, so the fade is only what
     * carries them across it -- and it is measured in real time rather than in
     * frames, since the CX's logic loop runs at about three steps a second.
     */
    unsigned int fadeAlpha(unsigned int total_ms, unsigned int remaining_ms, unsigned int max_alpha);

    /** Ticks from `time` to the next dawn, i.e. how long sleeping skips. */
    unsigned int ticksToMorning(unsigned int time);

    // --- the respawn point -------------------------------------------------

    /**
     * Where the player respawns after dying, as the *foot* cell of the bed that
     * was slept in. Storing the foot rather than "whichever half was clicked" is
     * what makes the record comparable with the half being broken later.
     */
    struct SpawnPoint
    {
        bool valid = false;
        int x = 0, y = 0, z = 0;
    };

    /**
     * The foot cell of a bed half, given the half's own position and data. Used
     * both when a bed is slept in (from the clicked half) and when one is broken
     * (from the half being removed).
     */
    void footOf(int x, int y, int z, uint8_t data, int &foot_x, int &foot_y, int &foot_z);

    /**
     * True when the bed half at this position belongs to the recorded spawn bed,
     * so breaking either half forgets the spawn point. The half's own data is
     * needed because a head has to be walked back to its foot first.
     */
    bool isSpawnBed(const SpawnPoint &spawn, int x, int y, int z, uint8_t data);
}

#endif // BED_H
