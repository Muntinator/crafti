#ifndef AABB_H
#define AABB_H

#include "gl.h"

class AABB
{
public:
    enum SIDE
    {
        FRONT=0,
        BACK,
        LEFT,
        RIGHT,
        TOP,
        BOTTOM,
        NONE
    };

    AABB() : AABB(0, 0, 0, 0, 0, 0) {}
    AABB(VERTEX *v1, VERTEX *v2);
    AABB(GLFix low_x, GLFix low_y, GLFix low_z, GLFix high_x, GLFix high_y, GLFix high_z);
    AABB(VERTEX *list, unsigned int size);

    void set(VERTEX *v1, VERTEX *v2);
    void set(GLFix low_x, GLFix low_y, GLFix low_z, GLFix high_x, GLFix high_y, GLFix high_z);
    void set(VERTEX *list, unsigned int size);

    // Trivial overlap test, called tens of millions of times per tour from
    // entity collision: keep it inline in the header so it costs compares only.
    bool intersects(AABB &other)
    {
        return high_x >= other.low_x && high_y >= other.low_y && high_z >= other.low_z
                && low_x <= other.high_x && low_y <= other.high_y && low_z <= other.high_z;
    }
    SIDE intersectsRay(GLFix x, GLFix y, GLFix z, GLFix dx, GLFix dy, GLFix dz, GLFix &dist);

    void render();
    void print();

    GLFix low_x, low_y, low_z, high_x, high_y, high_z;
};

#endif // AABB_H
