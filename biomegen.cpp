/**
 * Biomes, mountains, rivers, caves and ravines (Stage 2 world generation).
 *
 * See biomegen.h for the contract. Two things are worth repeating here because
 * they are what keep this cheap on a 150 MHz CX:
 *
 *  - the noise is value noise on a lattice whose spacing is a power of two, so
 *    every lattice lookup is a shift and every interior weight is exact, and
 *  - nothing is stored between calls, so a chunk can be generated alone and in
 *    any order and still produce exactly the same world.
 *
 * The module is deliberately free of engine headers: it only reports numbers,
 * and chunk.cpp turns those into blocks. tests/biomegen_test.cc pins the
 * interesting properties down (determinism, biome coverage, cave/ravine
 * coverage, surface materials, and that a ravine never breaks the surface).
 */

#include "biomegen.h"

namespace
{
    // --- Value noise ---------------------------------------------------------

    /** One lattice point, hashed into 32 bits. */
    uint32_t lattice(uint32_t seed, uint32_t salt, int x, int y, int z)
    {
        uint32_t h = seed ^ salt;
        h += static_cast<uint32_t>(x) * 0x9E3779B1u;
        h ^= h >> 15;
        h *= 0x85EBCA6Bu;
        h += static_cast<uint32_t>(y) * 0xC2B2AE35u;
        h ^= h >> 13;
        h *= 0x27D4EB2Fu;
        h += static_cast<uint32_t>(z) * 0x165667B1u;
        h ^= h >> 16;
        return h;
    }

    /** A lattice point as 0..1023. */
    inline int latticeValue(uint32_t seed, uint32_t salt, int x, int y, int z)
    {
        return static_cast<int>((lattice(seed, salt, x, y, z) & 0xFFFFu) >> 6);
    }

    /**
     * Smoothstep weight (0..256) of a position `f` inside a cell of 2^shift
     * blocks. 3t^2 - 2t^3 scaled to 0..256; the CX does it with shifts only
     * because the whole expression stays below 2^25.
     */
    inline int smoothWeight(int f, int shift)
    {
        const int w = (f << 8) >> shift; // 0..256
        return (w * w * (768 - 2 * w)) >> 16;
    }

    /** Linear interpolation with an 0..256 weight. */
    inline int lerpWeight(int a, int b, int w)
    {
        return a + (((b - a) * w) >> 8);
    }

    /** Value noise in 0..1023 on a 2D lattice of 2^shift blocks. */
    int noise2(uint32_t seed, uint32_t salt, int x, int z, int shift)
    {
        const int mask = (1 << shift) - 1;
        const int gx = x >> shift, gz = z >> shift;
        const int wx = smoothWeight(x & mask, shift);
        const int wz = smoothWeight(z & mask, shift);

        const int c00 = latticeValue(seed, salt, gx, 0, gz);
        const int c10 = latticeValue(seed, salt, gx + 1, 0, gz);
        const int c01 = latticeValue(seed, salt, gx, 0, gz + 1);
        const int c11 = latticeValue(seed, salt, gx + 1, 0, gz + 1);

        return lerpWeight(lerpWeight(c00, c10, wx), lerpWeight(c01, c11, wx), wz);
    }

    /** Value noise in 0..1023 on a 3D lattice of 2^shift_xz by 2^shift_y blocks. */
    int noise3(uint32_t seed, uint32_t salt, int x, int y, int z, int shift_xz, int shift_y)
    {
        const int mask_xz = (1 << shift_xz) - 1;
        const int mask_y = (1 << shift_y) - 1;
        const int gx = x >> shift_xz, gy = y >> shift_y, gz = z >> shift_xz;
        const int wx = smoothWeight(x & mask_xz, shift_xz);
        const int wy = smoothWeight(y & mask_y, shift_y);
        const int wz = smoothWeight(z & mask_xz, shift_xz);

        const int c000 = latticeValue(seed, salt, gx, gy, gz);
        const int c100 = latticeValue(seed, salt, gx + 1, gy, gz);
        const int c010 = latticeValue(seed, salt, gx, gy + 1, gz);
        const int c110 = latticeValue(seed, salt, gx + 1, gy + 1, gz);
        const int c001 = latticeValue(seed, salt, gx, gy, gz + 1);
        const int c101 = latticeValue(seed, salt, gx + 1, gy, gz + 1);
        const int c011 = latticeValue(seed, salt, gx, gy + 1, gz + 1);
        const int c111 = latticeValue(seed, salt, gx + 1, gy + 1, gz + 1);

        const int x00 = lerpWeight(c000, c100, wx);
        const int x10 = lerpWeight(c010, c110, wx);
        const int x01 = lerpWeight(c001, c101, wx);
        const int x11 = lerpWeight(c011, c111, wx);
        const int y0 = lerpWeight(x00, x10, wy);
        const int y1 = lerpWeight(x01, x11, wy);
        return lerpWeight(y0, y1, wz);
    }

    /**
     * Two-octave field in 0..1023. The detail octave sits on a lattice four
     * times finer and only carries an eighth of the weight, which keeps the
     * broad shape of the field while giving its edges somewhere to wiggle.
     */
    int field2(uint32_t seed, uint32_t salt, int x, int z, int shift, int detail_shift)
    {
        const int broad = noise2(seed, salt, x, z, shift);
        const int detail = noise2(seed, salt ^ 0x5BD1E995u, x, z, detail_shift);
        return (broad * 7 + detail) / 8;
    }

    int field3(uint32_t seed, uint32_t salt, int x, int y, int z,
               int shift_xz, int shift_y, int detail_xz, int detail_y)
    {
        const int broad = noise3(seed, salt, x, y, z, shift_xz, shift_y);
        const int detail = noise3(seed, salt ^ 0x5BD1E995u, x, y, z, detail_xz, detail_y);
        return (broad * 7 + detail) / 8;
    }

    // --- Field salts. Distinct constants keep the fields independent, so the
    // --- temperature of a place never correlates with its rivers.
    constexpr uint32_t SaltTemperature = 0x54454D50u; // "TEMP"
    constexpr uint32_t SaltHumidity = 0x48554D49u;    // "HUMI"
    constexpr uint32_t SaltMountain = 0x4D4F554Eu;    // "MOUN"
    constexpr uint32_t SaltRiver = 0x52495652u;       // "RIVR"
    constexpr uint32_t SaltCaveA = 0x43415645u;       // "CAVE"
    constexpr uint32_t SaltCaveB = 0x54554E4Eu;       // "TUNN"
    constexpr uint32_t SaltRavine = 0x5241564Eu;      // "RAVN"
    constexpr uint32_t SaltRavineShape = 0x52415653u; // "RAVS"

    // --- Field scales --------------------------------------------------------
    constexpr int BiomeShift = 8;       // 256 blocks
    constexpr int BiomeDetailShift = 6; // 64 blocks
    constexpr int MountainShift = 8;
    constexpr int MountainDetailShift = 6;
    constexpr int RiverShift = 8;
    constexpr int RiverDetailShift = 6;
    constexpr int CaveShiftXZ = 5;      // 32 blocks
    constexpr int CaveShiftY = 4;       // 16 blocks
    constexpr int CaveDetailXZ = 4;
    constexpr int CaveDetailY = 3;

    // --- Biome thresholds. A bilinear-interpolated value noise field is much
    // --- more concentrated than a uniform one (its standard deviation is about
    // --- 130 of the 0..1023 range, not 295), so these thresholds were measured
    // --- rather than guessed: each of them is a z-score against that spread.
    // The cold threshold lives in the header now: the weather asks the same
    // question (does precipitation freeze here?) and the two answers must be the
    // same number, not two constants that happen to agree.
    constexpr int ColdLevel = BiomeGen::ColdTemperature;
    constexpr int HotLevel = 677;
    constexpr int WetLevel = 616;
    /** Mountainous terrain: roughly a tenth of the surface, like vanilla. */
    constexpr int MountainLevel = 750;
    /**
     * Ramp of the mountain lift, per 100 field units above the level. It starts
     * at zero on purpose: a constant offset would put a cliff along the contour
     * where a column enters the mountain field, and a mountain should rise out
     * of its surroundings instead.
     */
    constexpr int MountainBoostPer100 = 20;
    constexpr int MountainBoostMax = 16;
    /** Above this the mountain is bare rock instead of grass. */
    constexpr int MountainBareRockHeight = 24;

    // --- River shape ---------------------------------------------------------
    constexpr int RiverCentre = 512;
    constexpr int RiverHalfWidth = 26;
    constexpr int RiverMaxDepth = 4;

    // --- Cave shape ----------------------------------------------------------
    constexpr int CaveCentre = 512;
    /** How close to the centre both fields have to be to carve. */
    constexpr int CaveBand = 62;

    // --- Ravine shape --------------------------------------------------------
    constexpr int RavineCellShift = 8; // RavineCellBlocks == 256
    constexpr int RavineStartJitter = 64;
    constexpr int RavineMinLength = 96;
    constexpr int RavineMaxLength = 240;
    constexpr int RavineMinHalfWidth = 4;
    constexpr int RavineMaxHalfWidth = 9;
    constexpr int RavineMinDepth = 16;
    constexpr int RavineMaxDepth = 26;
    constexpr int RavineMinFloor = 5;
    constexpr int RavineFloorSpread = 5;
    /** Below this surface height there is no room for a fissure worth having. */
    constexpr int RavineMinSurfaceHeight = 10;

    static_assert((1 << RavineCellShift) == BiomeGen::RavineCellBlocks, "ravine cell size must match the header");
    static_assert(RavineStartJitter + RavineMaxLength / 2 <= BiomeGen::RavineCellBlocks + BiomeGen::RavineReach,
                  "a ravine segment must not escape the 3x3 cell neighbourhood ravineAt() checks");

    /** Distinct biome needs a distinct material, or the biome is invisible. */
    void surfaceFor(int biome, int height, uint8_t &surface, uint8_t &subsurface)
    {
        switch(biome)
        {
        case BiomeGen::BiomeOcean:
        case BiomeGen::BiomeBeach:
        case BiomeGen::BiomeDesert:
            // Sandy everywhere the water reaches or the desert is dry. River
            // beds land here too, because a river lowers the column below the
            // sea line and the biome is then recomputed from the final height.
            surface = BiomeGen::BlockSand;
            subsurface = BiomeGen::BlockSand;
            break;
        case BiomeGen::BiomeMountains:
            if(height >= MountainBareRockHeight)
            {
                surface = BiomeGen::BlockStone;
                subsurface = BiomeGen::BlockStone;
            }
            else
            {
                surface = BiomeGen::BlockGrass;
                subsurface = BiomeGen::BlockDirt;
            }
            break;
        default:
            surface = BiomeGen::BlockGrass;
            subsurface = BiomeGen::BlockDirt;
            break;
        }
    }

    int temperatureField(uint32_t world_seed, int world_x, int world_z)
    {
        return field2(world_seed, SaltTemperature, world_x, world_z, BiomeShift, BiomeDetailShift);
    }

    int landBiome(uint32_t world_seed, int world_x, int world_z, bool mountainous)
    {
        if(mountainous)
            return BiomeGen::BiomeMountains;

        const int temperature = temperatureField(world_seed, world_x, world_z);
        const int humidity = field2(world_seed, SaltHumidity, world_x, world_z, BiomeShift, BiomeDetailShift);

        if(temperature >= HotLevel)
            return humidity >= WetLevel ? BiomeGen::BiomeSavanna : BiomeGen::BiomeDesert;
        if(temperature <= ColdLevel)
            return BiomeGen::BiomeForest; // no snow blocks exist, so "cold" is a dense forest
        return humidity >= WetLevel ? BiomeGen::BiomeForest : BiomeGen::BiomePlains;
    }

    bool ravineCellHas(uint32_t world_seed, int cell_x, int cell_z)
    {
        return (lattice(world_seed, SaltRavine, cell_x, 0, cell_z) % 3u) == 0u;
    }

    /**
     * The ravine of one cell, as a segment. A segment is what turns a noise
     * blob into a fissure: it is straight over its whole length, so the result
     * reads as a canyon instead of as another cave.
     */
    void ravineSegment(uint32_t world_seed, int cell_x, int cell_z,
                       int &ax, int &az, int &bx, int &bz, int &half_width)
    {
        const uint32_t h = lattice(world_seed, SaltRavineShape, cell_x, 0, cell_z);

        static const int direction[8][2] = {
            {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
        };

        const int centre_x = (cell_x << RavineCellShift) + (BiomeGen::RavineCellBlocks / 2);
        const int centre_z = (cell_z << RavineCellShift) + (BiomeGen::RavineCellBlocks / 2);

        const int jitter_x = static_cast<int>(h % (RavineStartJitter * 2 + 1)) - RavineStartJitter;
        const int jitter_z = static_cast<int>((h >> 7) % (RavineStartJitter * 2 + 1)) - RavineStartJitter;

        ax = centre_x + jitter_x;
        az = centre_z + jitter_z;

        const int dir = static_cast<int>((h >> 14) & 7u);
        const int length = RavineMinLength
            + static_cast<int>((h >> 17) % static_cast<uint32_t>(RavineMaxLength - RavineMinLength + 1));
        bx = ax + direction[dir][0] * length;
        bz = az + direction[dir][1] * length;

        half_width = RavineMinHalfWidth
            + static_cast<int>((h >> 24) % static_cast<uint32_t>(RavineMaxHalfWidth - RavineMinHalfWidth + 1));
    }

    /** Squared distance from a point to a segment, in blocks. No square root. */
    int distanceToSegmentSq(int px, int pz, int ax, int az, int bx, int bz)
    {
        const int vx = bx - ax, vz = bz - az;
        const int wx = px - ax, wz = pz - az;
        const int length_sq = vx * vx + vz * vz;

        int t = 0;
        if(length_sq > 0)
        {
            const int projection = wx * vx + wz * vz;
            t = (projection << 8) / length_sq;
            if(t < 0)
                t = 0;
            else if(t > 256)
                t = 256;
        }

        const int cx = ax + ((vx * t) >> 8);
        const int cz = az + ((vz * t) >> 8);
        const int dx = px - cx, dz = pz - cz;
        return dx * dx + dz * dz;
    }
}

const char *BiomeGen::biomeName(int biome)
{
    switch(biome)
    {
    case BiomeOcean: return "Ocean";
    case BiomeBeach: return "Beach";
    case BiomeDesert: return "Desert";
    case BiomePlains: return "Plains";
    case BiomeForest: return "Forest";
    case BiomeSavanna: return "Savanna";
    case BiomeMountains: return "Mountains";
    default: return "Unknown";
    }
}

int BiomeGen::temperatureAt(uint32_t world_seed, int world_x, int world_z)
{
    return temperatureField(world_seed, world_x, world_z);
}

bool BiomeGen::isColdAt(uint32_t world_seed, int world_x, int world_z)
{
    return temperatureField(world_seed, world_x, world_z) <= ColdLevel;
}

int BiomeGen::riverDepth(uint32_t world_seed, int world_x, int world_z)
{
    // A river is the iso-line of a low frequency field: wherever the field
    // crosses its centre value there is a winding channel, and the two-octave
    // field makes that line wander instead of running straight.
    const int value = field2(world_seed, SaltRiver, world_x, world_z, RiverShift, RiverDetailShift);
    const int distance = value > RiverCentre ? value - RiverCentre : RiverCentre - value;
    if(distance >= RiverHalfWidth)
        return 0;

    // The bank is depth 0 and the middle of the channel is the deepest, so a
    // river blends into the terrain instead of cutting a square trench.
    const int remaining = RiverHalfWidth - distance;
    return (remaining * RiverMaxDepth) / RiverHalfWidth;
}

void BiomeGen::columnAt(uint32_t world_seed, int world_x, int world_z, int base_height, Column &out)
{
    const int mountain = field2(world_seed, SaltMountain, world_x, world_z, MountainShift, MountainDetailShift);
    const bool mountainous = mountain >= MountainLevel;

    int height = base_height;
    if(mountainous)
    {
        int boost = ((mountain - MountainLevel) * MountainBoostPer100) / 100;
        if(boost > MountainBoostMax)
            boost = MountainBoostMax;
        height += boost;
    }

    height -= riverDepth(world_seed, world_x, world_z);

    if(height < MinHeight)
        height = MinHeight;
    else if(height > MaxHeight)
        height = MaxHeight;

    // The land biome comes from the fields alone; the water line is decided by
    // the height the column actually ends up with, so a river bed or a flooded
    // valley is sand rather than grass under water.
    int biome;
    if(height < SeaLevel)
        biome = BiomeOcean;
    else if(height <= SeaLevel + 1)
        biome = BiomeBeach;
    else
        biome = landBiome(world_seed, world_x, world_z, mountainous);

    uint8_t surface = BlockGrass, subsurface = BlockDirt;
    surfaceFor(biome, height, surface, subsurface);

    out.biome = biome;
    out.height = height;
    out.surface = surface;
    out.subsurface = subsurface;
}

int BiomeGen::biomeAt(uint32_t world_seed, int world_x, int world_z, int base_height)
{
    Column column;
    columnAt(world_seed, world_x, world_z, base_height, column);
    return column.biome;
}

int BiomeGen::treeDensityPercent(int biome)
{
    // A canopy is 7x7 blocks, so a forest wants roughly one tree per 20 columns:
    // an eighth of the columns, i.e. about five trees in an 8x8 chunk. Anything
    // denser is a solid roof of leaves with no ground left to walk on, and the
    // old uniform pass planted barely a quarter of a tree per chunk.
    switch(biome)
    {
    case BiomeForest: return 7;
    case BiomePlains: return 2;
    case BiomeSavanna: return 1;
    // Desert, ocean, beach and bare mountains stay bare. Terrain generation also
    // refuses to plant on anything but grass, so this is a density, not a rule.
    default: return 0;
    }
}

bool BiomeGen::caveAt(uint32_t world_seed, int world_x, int world_y, int world_z)
{
    // Two fields, both sampled between their own lattices, and a carve only
    // where they are simultaneously near the middle. The intersection of two
    // smooth sheets is a curve, which is exactly what a tunnel is; carving a
    // single field would give round blobs instead. The first field is checked
    // first, so the common case costs one field and not two.
    const int first = field3(world_seed, SaltCaveA, world_x, world_y, world_z,
                             CaveShiftXZ, CaveShiftY, CaveDetailXZ, CaveDetailY);
    const int first_offset = first - CaveCentre;
    if(first_offset > CaveBand || first_offset < -CaveBand)
        return false;

    const int second = field3(world_seed, SaltCaveB, world_x, world_y, world_z,
                              CaveShiftXZ, CaveShiftY, CaveDetailXZ, CaveDetailY);
    const int second_offset = second - CaveCentre;
    return second_offset <= CaveBand && second_offset >= -CaveBand;
}

void BiomeGen::ravineAt(uint32_t world_seed, int world_x, int world_z, int surface_height, Ravine &out)
{
    out.present = false;
    out.lowest = 0;
    out.highest = -1;
    out.strength = 0;

    if(surface_height <= RavineMinSurfaceHeight)
        return;

    const int cell_x = world_x >> RavineCellShift;
    const int cell_z = world_z >> RavineCellShift;

    bool found = false;
    int best_distance_sq = 0;
    int best_half_width = 0;
    int best_cell_x = 0, best_cell_z = 0;

    // A segment can reach about one cell out of its own, so the ravine touching
    // this column always belongs to one of the nine cells around it.
    for(int dz = -1; dz <= 1; ++dz)
        for(int dx = -1; dx <= 1; ++dx)
        {
            const int cx = cell_x + dx, cz = cell_z + dz;
            if(!ravineCellHas(world_seed, cx, cz))
                continue;

            int ax, az, bx, bz, half_width;
            ravineSegment(world_seed, cx, cz, ax, az, bx, bz, half_width);

            const int distance_sq = distanceToSegmentSq(world_x, world_z, ax, az, bx, bz);
            if(distance_sq >= half_width * half_width)
                continue;
            if(found && distance_sq >= best_distance_sq)
                continue;

            found = true;
            best_distance_sq = distance_sq;
            best_half_width = half_width;
            best_cell_x = cx;
            best_cell_z = cz;
        }

    if(!found)
        return;

    // Cross section: full height on the centre line, a sliver at the rim. That
    // lens shape is what makes a ravine look carved rather than drilled.
    const int radius_sq = best_half_width * best_half_width;
    const int strength = ((radius_sq - best_distance_sq) << 10) / radius_sq;

    const uint32_t shape = lattice(world_seed, SaltRavineShape, best_cell_x, 0, best_cell_z);
    const int depth = RavineMinDepth
        + static_cast<int>((shape >> 8) % static_cast<uint32_t>(RavineMaxDepth - RavineMinDepth + 1));
    const int floor_y = RavineMinFloor
        + static_cast<int>((shape >> 16) % static_cast<uint32_t>(RavineFloorSpread + 1));
    const int middle = floor_y + depth / 2;
    const int half_height = ((depth / 2) * strength) >> 10;

    int lowest = middle - half_height;
    int highest = middle + half_height;
    if(lowest < RavineMinFloor)
        lowest = RavineMinFloor;
    // Never the surface block itself: a ravine must not open the sky or drain a
    // lake into the caves below it.
    if(highest > surface_height - 1)
        highest = surface_height - 1;
    if(highest < lowest)
        return;

    out.present = true;
    out.lowest = lowest;
    out.highest = highest;
    out.strength = strength;
}
