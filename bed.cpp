#include "bed.h"

namespace Bed
{
    bool isHorizontal(BLOCK_SIDE side)
    {
        return side == BLOCK_FRONT || side == BLOCK_BACK || side == BLOCK_LEFT || side == BLOCK_RIGHT;
    }

    bool isValidData(uint8_t data)
    {
        return isHorizontal(facing(data));
    }

    void sideOffset(BLOCK_SIDE side, int &dx, int &dy, int &dz)
    {
        dx = dy = dz = 0;

        // Kept in step with the offsets the world task uses when it places a
        // block against a face: BLOCK_FRONT is the face whose normal is -z, and
        // so on round the ring.
        switch(side)
        {
        case BLOCK_FRONT: dz = -1; break;
        case BLOCK_BACK:  dz = 1; break;
        case BLOCK_LEFT:  dx = -1; break;
        case BLOCK_RIGHT: dx = 1; break;
        default: break;
        }
    }

    void partnerOffset(uint8_t data, int &dx, int &dy, int &dz)
    {
        sideOffset(facing(data), dx, dy, dz);
    }

    BLOCK_SIDE facingForYaw(int yaw_degrees)
    {
        // Wrapped into one turn first, so a negative or a very large yaw (which
        // the player's angle can be before it is normalised) still lands inside
        // one of the four quadrants.
        int yaw = yaw_degrees % 360;
        if(yaw < 0)
            yaw += 360;

        // getForward() is (sin(yr), cos(yr)) for x and z, so yaw 0 looks along
        // +z: yaw 0 -> BLOCK_BACK (+z), 90 -> BLOCK_RIGHT (+x), and so on.
        if(yaw < 45 || yaw >= 315)
            return BLOCK_BACK;
        if(yaw < 135)
            return BLOCK_RIGHT;
        if(yaw < 225)
            return BLOCK_FRONT;
        return BLOCK_LEFT;
    }

    SleepResult canSleep(bool night, bool storming, bool bed_complete)
    {
        if(!bed_complete)
            return BedBroken;
        if(!night && !storming)
            return NotNight;
        return CanSleep;
    }

    const char *sleepMessage(SleepResult result)
    {
        switch(result)
        {
        case CanSleep: return "Good morning";
        case BedBroken: return "That bed is broken";
        case NotNight: return "You can only sleep at night";
        default: return "";
        }
    }

    bool isPartner(uint8_t data, uint8_t partner_data)
    {
        return isValidData(data)
            && isValidData(partner_data)
            && facing(data) == facing(partner_data)
            && isHead(data) != isHead(partner_data);
    }

    unsigned int fadeAlpha(unsigned int total_ms, unsigned int remaining_ms, unsigned int max_alpha)
    {
        // A sleep that has already finished (or has not started) is not dark at
        // all, which is also what the caller sees on the frame it ends.
        // The `total_ms < 2` guard is what keeps the halving below from being a
        // division by zero, which a one-millisecond fade would otherwise be.
        if(total_ms < 2 || remaining_ms == 0 || remaining_ms >= total_ms)
            return 0;

        const unsigned int half = total_ms / 2;
        const unsigned int progress = total_ms - remaining_ms;

        // Up to the middle and back down. The peak is clamped to `half` so that an
        // odd total (whose two halves are not the same length) cannot overshoot
        // and darken past `max_alpha`.
        unsigned int rise = progress < half ? progress : total_ms - progress;
        if(rise > half)
            rise = half;

        return rise * max_alpha / half;
    }

    unsigned int ticksToMorning(unsigned int time)
    {
        const unsigned int ticks = WorldClock::ticksUntil(time, WorldClock::TimeSunrise);

        // A sleep always skips to the *next* dawn. Lying down at dawn itself is
        // the one case where "the next dawn" is a whole day away, and answering
        // zero would leave the player asleep in the morning they are already in
        // -- with a night that never counted.
        return ticks == 0 ? WorldClock::TicksPerDay : ticks;
    }

    void footOf(int x, int y, int z, uint8_t data, int &foot_x, int &foot_y, int &foot_z)
    {
        foot_x = x;
        foot_y = y;
        foot_z = z;

        if(!isHead(data))
            return;

        int dx, dy, dz;
        partnerOffset(data, dx, dy, dz);
        foot_x -= dx;
        foot_y -= dy;
        foot_z -= dz;
    }

    bool isSpawnBed(const SpawnPoint &spawn, int x, int y, int z, uint8_t data)
    {
        if(!spawn.valid)
            return false;

        int foot_x, foot_y, foot_z;
        footOf(x, y, z, data, foot_x, foot_y, foot_z);

        return foot_x == spawn.x && foot_y == spawn.y && foot_z == spawn.z;
    }
}
