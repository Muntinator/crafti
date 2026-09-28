// Host tests for bed.h: the two-block bed, sleeping and the respawn point.
//
// The bed is the first horizontal multi-block structure in the game (the door is
// two blocks stacked vertically), and its two halves have to agree on a facing
// that either one can use to find the other. That encoding, the yaw a placed bed
// takes its facing from, the decision to allow sleeping and the record of which
// bed the player wakes up at are all pure, so they are pinned down here rather
// than only inside the world task.
//
// The tests are deliberately unkind about the edges: a bed written straight into
// the world with a vertical facing, a spawn point whose bed has been replaced, a
// sleep at dawn, a yaw that puts the head on the wrong side of the player.
//
// Build and run with `make -C tests`.

#include "bed.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

using namespace Bed;

/** The four sides a bed may lie along, with the offset each one means. */
static const BLOCK_SIDE horizontal[4] = { BLOCK_FRONT, BLOCK_BACK, BLOCK_LEFT, BLOCK_RIGHT };

static void test_data_encoding()
{
    for(int i = 0; i < 4; ++i)
    {
        const BLOCK_SIDE side = horizontal[i];

        for(int head = 0; head <= 1; ++head)
        {
            const uint8_t data = dataFor(side, head != 0);
            CHECK(facing(data) == side);
            CHECK(isHead(data) == (head != 0));
            CHECK(isValidData(data));

            // The data byte has to stay below the redstone power flag in bit 15,
            // and below bit 7, so that a bed's data is never mistaken for one.
            CHECK(data < 128);
            CHECK((data & BLOCK_SIDE_BITS) == static_cast<uint8_t>(side));

            // The two halves of one bed differ only in the head bit.
            CHECK((dataFor(side, true) ^ dataFor(side, false)) == HeadBit);
        }
    }

    // A bed never lies vertically: a data byte naming the top or the bottom face
    // is not a bed this game placed, and every reader treats it as broken.
    CHECK(!isValidData(dataFor(BLOCK_TOP, false)));
    CHECK(!isValidData(dataFor(BLOCK_BOTTOM, true)));
    CHECK(!isHorizontal(BLOCK_TOP));
    CHECK(!isHorizontal(BLOCK_BOTTOM));
    CHECK(isHorizontal(BLOCK_FRONT));
    CHECK(isHorizontal(BLOCK_BACK));
    CHECK(isHorizontal(BLOCK_LEFT));
    CHECK(isHorizontal(BLOCK_RIGHT));
}

static void test_offsets()
{
    int dx, dy, dz;

    // The four facings of a flat bed, in the same spelling the world task uses
    // when it places a block against a face.
    sideOffset(BLOCK_FRONT, dx, dy, dz);
    CHECK(dx == 0 && dy == 0 && dz == -1);
    sideOffset(BLOCK_BACK, dx, dy, dz);
    CHECK(dx == 0 && dy == 0 && dz == 1);
    sideOffset(BLOCK_LEFT, dx, dy, dz);
    CHECK(dx == -1 && dy == 0 && dz == 0);
    sideOffset(BLOCK_RIGHT, dx, dy, dz);
    CHECK(dx == 1 && dy == 0 && dz == 0);

    // Anything vertical gives no offset at all, so a malformed bed cannot walk
    // off into the sky.
    sideOffset(BLOCK_TOP, dx, dy, dz);
    CHECK(dx == 0 && dy == 0 && dz == 0);
    sideOffset(BLOCK_BOTTOM, dx, dy, dz);
    CHECK(dx == 0 && dy == 0 && dz == 0);

    // Both halves carry the same facing, so the offset between them is the same
    // whether it is read from the foot or from the head.
    for(int i = 0; i < 4; ++i)
    {
        int fx, fy, fz, hx, hy, hz;
        partnerOffset(dataFor(horizontal[i], false), fx, fy, fz);
        partnerOffset(dataFor(horizontal[i], true), hx, hy, hz);
        CHECK(fx == hx && fy == hy && fz == hz);
        CHECK(fy == 0 && hy == 0); // a bed is always laid flat
    }
}

static void test_head_and_foot_find_each_other()
{
    for(int i = 0; i < 4; ++i)
    {
        const BLOCK_SIDE side = horizontal[i];
        int dx, dy, dz;
        sideOffset(side, dx, dy, dz);

        const int foot_x = 10, foot_y = 30, foot_z = -4;
        const int head_x = foot_x + dx, head_y = foot_y + dy, head_z = foot_z + dz;

        // The head is where the foot's facing says it is.
        int hx, hy, hz;
        partnerOffset(dataFor(side, false), hx, hy, hz);
        CHECK(foot_x + hx == head_x && foot_y + hy == head_y && foot_z + hz == head_z);

        // Walking back from either half lands on the foot.
        int fx, fy, fz;
        footOf(foot_x, foot_y, foot_z, dataFor(side, false), fx, fy, fz);
        CHECK(fx == foot_x && fy == foot_y && fz == foot_z);

        footOf(head_x, head_y, head_z, dataFor(side, true), fx, fy, fz);
        CHECK(fx == foot_x && fy == foot_y && fz == foot_z);

        // A malformed data byte still yields the cell it was given rather than a
        // cell outside the world.
        footOf(head_x, head_y, head_z, dataFor(BLOCK_TOP, true), fx, fy, fz);
        CHECK(fx == head_x && fy == head_y && fz == head_z);
    }
}

static void test_facing_follows_the_look_direction()
{
    // The head is placed in front of the player, so the offset it takes has to
    // agree with the direction the player is looking. getForward() is
    // (sin(yaw), cos(yaw)) for x and z, which is what this re-derives.
    for(int yaw = 0; yaw < 360; yaw += 5)
    {
        const BLOCK_SIDE facing_side = facingForYaw(yaw);
        CHECK(isHorizontal(facing_side));

        int dx, dy, dz;
        sideOffset(facing_side, dx, dy, dz);
        CHECK(dy == 0);

        const double radians = yaw * 3.14159265358979 / 180.0;
        const double forward_x = sin(radians);
        const double forward_z = cos(radians);

        // A positive dot product means the head is in the half-plane the player
        // is looking into. At the quadrant boundaries the bed is diagonal to the
        // look direction, hence the modest floor rather than "almost 1".
        const double dot = dx * forward_x + dz * forward_z;
        CHECK(dot > 0.3);
    }

    // The four cardinal directions, spelled out, because these are the ones a
    // player notices: yaw 0 looks along +z, 90 along +x, and so on.
    CHECK(facingForYaw(0) == BLOCK_BACK);
    CHECK(facingForYaw(89) == BLOCK_RIGHT);
    CHECK(facingForYaw(90) == BLOCK_RIGHT);
    CHECK(facingForYaw(180) == BLOCK_FRONT);
    CHECK(facingForYaw(270) == BLOCK_LEFT);
    CHECK(facingForYaw(359) == BLOCK_BACK);

    // A yaw that is negative or past a full turn is wrapped rather than trusted.
    CHECK(facingForYaw(-90) == BLOCK_LEFT);
    CHECK(facingForYaw(-270) == BLOCK_RIGHT);
    CHECK(facingForYaw(450) == BLOCK_RIGHT);
    CHECK(facingForYaw(360) == BLOCK_BACK);
    CHECK(facingForYaw(720) == BLOCK_BACK);
}

static void test_sleep_decision()
{
    // Night, or a thunderstorm in daylight, is what makes a bed usable.
    CHECK(canSleep(true, false, true) == CanSleep);
    CHECK(canSleep(true, true, true) == CanSleep);
    CHECK(canSleep(false, true, true) == CanSleep);

    CHECK(canSleep(false, false, true) == NotNight);
    CHECK(canSleep(true, false, false) == BedBroken);
    CHECK(canSleep(false, false, false) == BedBroken);

    // Every result has something to say, and the successful one is what the HUD
    // shows on waking.
    CHECK(strcmp(sleepMessage(CanSleep), "Good morning") == 0);
    CHECK(sleepMessage(NotNight)[0] != '\0');
    CHECK(sleepMessage(BedBroken)[0] != '\0');
}

static void test_morning_is_the_next_dawn()
{
    // Sleeping always lands on dawn of the following day, never on dawn of the
    // day it already is -- including when the player lies down at dawn.
    CHECK(ticksToMorning(WorldClock::TimeSunrise) == WorldClock::TicksPerDay);
    CHECK(ticksToMorning(100) == WorldClock::TicksPerDay - 100);
    CHECK(ticksToMorning(WorldClock::TimeMidnight) == WorldClock::TicksPerDay - WorldClock::TimeMidnight);
    CHECK(ticksToMorning(WorldClock::TicksPerDay - 1) == 1);

    // The skipped amount plus the time it started from is a whole day, which is
    // what makes the day counter advance exactly once.
    for(unsigned int time = 0; time < WorldClock::TicksPerDay; time += 337)
        CHECK(time + ticksToMorning(time) == WorldClock::TicksPerDay);

    // Midnight to morning is the long sleep, and it is a real part of the night.
    CHECK(ticksToMorning(WorldClock::TimeMidnight) == 6000);
    CHECK(ticksToMorning(WorldClock::TimeMidnight) > 0);
}

static void test_spawn_point()
{
    SpawnPoint spawn;
    CHECK(!spawn.valid);

    // Nothing is a spawn bed while there is no spawn point, whatever is broken.
    CHECK(!isSpawnBed(spawn, 5, 20, 5, dataFor(BLOCK_BACK, false)));

    spawn.valid = true;
    spawn.x = 5; spawn.y = 20; spawn.z = 5;

    // Both halves of the recorded bed forget the spawn point...
    CHECK(isSpawnBed(spawn, 5, 20, 5, dataFor(BLOCK_BACK, false)));  // the foot
    CHECK(isSpawnBed(spawn, 5, 20, 6, dataFor(BLOCK_BACK, true)));   // its head, one block +z

    // ...and nothing else does.
    CHECK(!isSpawnBed(spawn, 5, 20, 6, dataFor(BLOCK_BACK, false)));
    CHECK(!isSpawnBed(spawn, 5, 20, 4, dataFor(BLOCK_BACK, true)));
    CHECK(!isSpawnBed(spawn, 6, 20, 5, dataFor(BLOCK_BACK, false)));
    CHECK(!isSpawnBed(spawn, 5, 21, 5, dataFor(BLOCK_BACK, false)));

    // A bed in the same cell facing the other way is the same cell: the spawn
    // point is a position, so its foot matches however the bed is turned, while
    // its head is somewhere else entirely.
    CHECK(isSpawnBed(spawn, 5, 20, 5, dataFor(BLOCK_FRONT, false))); // the foot is the foot
    CHECK(isSpawnBed(spawn, 5, 20, 5, dataFor(BLOCK_LEFT, false)));
    // A head walking back to a foot in the recorded cell is that bed, whichever
    // way it is turned; a head whose foot is somewhere else is not.
    CHECK(isSpawnBed(spawn, 5, 20, 4, dataFor(BLOCK_FRONT, true)));  // foot back at +z
    CHECK(!isSpawnBed(spawn, 5, 20, 4, dataFor(BLOCK_BACK, true)));  // foot at z = 3

    // A malformed half resolves to its own cell, so it can only match a bed
    // recorded in the same cell.
    CHECK(!isSpawnBed(spawn, 5, 20, 6, dataFor(BLOCK_TOP, true)));
    CHECK(isSpawnBed(spawn, 5, 20, 5, dataFor(BLOCK_TOP, true)));
}

static void test_the_bed_and_the_clock_agree_about_night()
{
    unsigned int sleepable = 0, samples = 0;

    for(unsigned int time = 0; time < WorldClock::TicksPerDay; time += 25)
    {
        WorldClock::restore(time, 0);

        // The bed's "is it night" question is the clock's, so over a whole day the
        // two may never disagree: a player told to come back at night must not be
        // turned away at midnight, nor able to sleep at noon.
        const bool allowed = canSleep(WorldClock::isNight(), false, true) == CanSleep;
        CHECK(allowed == WorldClock::isNight());

        // A thunderstorm is the other way in, whatever the time of day.
        CHECK(canSleep(WorldClock::isNight(), true, true) == CanSleep);

        ++samples;
        if(allowed)
            ++sleepable;
    }

    // Night is a real part of the day, and the shorter, darker half of it.
    CHECK(sleepable > 0);
    CHECK(sleepable < samples);
}

static void test_sleeping_lands_on_the_next_dawn()
{
    // The whole point of a bed: whatever time the player lies down at, the clock
    // ends up on a dawn with the day counter moved on by exactly one, because the
    // night that was slept through is a night that happened.
    for(unsigned int time = 0; time < WorldClock::TicksPerDay; time += 250)
    {
        WorldClock::restore(time, 3);

        const unsigned int skipped = ticksToMorning(WorldClock::time());
        CHECK(skipped > 0);
        CHECK(skipped <= WorldClock::TicksPerDay);

        WorldClock::advance(skipped);

        CHECK(WorldClock::time() == WorldClock::TimeSunrise);
        CHECK(WorldClock::dayCount() == 4);
    }

    // Lying down at dawn itself crosses a whole day rather than none of one.
    WorldClock::restore(WorldClock::TimeSunrise, 0);
    WorldClock::advance(ticksToMorning(WorldClock::time()));
    CHECK(WorldClock::time() == WorldClock::TimeSunrise);
    CHECK(WorldClock::dayCount() == 1);
}

static void test_one_bed_from_two_halves()
{
    for(int i = 0; i < 4; ++i)
    {
        const uint8_t foot = dataFor(horizontal[i], false);
        const uint8_t head = dataFor(horizontal[i], true);

        // The two halves of a bed are partners, whichever way round they are
        // asked about: either one can be used to lie down.
        CHECK(isPartner(foot, head));
        CHECK(isPartner(head, foot));

        // A half is not its own partner, and two feet are two beds that touch.
        CHECK(!isPartner(foot, foot));
        CHECK(!isPartner(head, head));

        // Two beds at right angles are not one bed even if their halves are the
        // two ends of *a* bed each.
        const uint8_t other_foot = dataFor(horizontal[(i + 1) % 4], false);
        const uint8_t other_head = dataFor(horizontal[(i + 1) % 4], true);
        CHECK(!isPartner(foot, other_head));
        CHECK(!isPartner(head, other_foot));

        // A malformed half is nobody's partner, so half a broken bed cannot be
        // slept in and is not drawn as a joint.
        CHECK(!isPartner(foot, dataFor(BLOCK_TOP, true)));
        CHECK(!isPartner(dataFor(BLOCK_BOTTOM, false), head));
        CHECK(!isPartner(dataFor(BLOCK_TOP, true), dataFor(BLOCK_TOP, false)));
    }
}

static void test_sleep_fade()
{
    constexpr unsigned int total = FadeTotalMs;

    // Nothing is drawn before the fade starts, once it has finished, and never
    // for a fade of no length (which is what the guard against the halving is for).
    CHECK(fadeAlpha(total, total, FadeMaxAlpha) == 0);
    CHECK(fadeAlpha(total, 0, FadeMaxAlpha) == 0);
    CHECK(fadeAlpha(total, total + 1, FadeMaxAlpha) == 0);
    CHECK(fadeAlpha(0, 0, FadeMaxAlpha) == 0);
    CHECK(fadeAlpha(1, 1, FadeMaxAlpha) == 0);

    // Darkest exactly halfway through, and no darker than asked for.
    CHECK(fadeAlpha(total, total / 2, FadeMaxAlpha) == FadeMaxAlpha);

    // It only ever rises to the middle and falls again, and a full night cannot
    // make the screen darker than the ceiling.
    unsigned int previous = 0;
    for(unsigned int remaining = total; remaining > 0; --remaining)
    {
        const unsigned int alpha = fadeAlpha(total, remaining, FadeMaxAlpha);
        CHECK(alpha <= FadeMaxAlpha);

        // Symmetric about the middle: the same amount of dark at n ms in as at
        // n ms before the end.
        CHECK(fadeAlpha(total, remaining, FadeMaxAlpha) == fadeAlpha(total, total - remaining, FadeMaxAlpha));

        // Rising towards the middle of the sleep and falling away from it again,
        // with the middle itself the one frame that is at the ceiling.
        if(remaining < total / 2)
            CHECK(alpha <= previous);
        else if(remaining > total / 2)
            CHECK(alpha >= previous);
        previous = alpha;
    }

    // A different ceiling scales the whole curve, and a fade of one millisecond
    // cannot divide by zero.
    CHECK(fadeAlpha(1000, 500, 100) == 100);
    CHECK(fadeAlpha(1000, 750, 100) == 50);
    CHECK(fadeAlpha(1000, 250, 100) == 50);
    CHECK(fadeAlpha(3, 2, 255) <= 255);
}

int main()
{
    test_data_encoding();
    test_offsets();
    test_head_and_foot_find_each_other();
    test_facing_follows_the_look_direction();
    test_sleep_decision();
    test_morning_is_the_next_dawn();
    test_the_bed_and_the_clock_agree_about_night();
    test_sleeping_lands_on_the_next_dawn();
    test_one_bed_from_two_halves();
    test_sleep_fade();
    test_spawn_point();

    printf("bed_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
