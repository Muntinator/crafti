#ifndef MOBMODEL_H
#define MOBMODEL_H

#include <stdint.h>

#include "gl.h"

/**
 * Vanilla Minecraft mob geometry, as data.
 *
 * Minecraft Java ships no entity models in its assets: the box models are
 * hardcoded in the client's model classes. So a clone cannot load them, it can
 * only transcribe them. These structures are that transcription, in the exact
 * terms the vanilla classes use, so a table here can be read against the
 * original line for line:
 *
 *  - coordinates are model units (1/16 of a block), the y axis grows downward
 *    and the feet sit on the ground at y = 24, which is how ModelRenderer
 *    numbers things,
 *  - a MobBox is one addBox(): its uv origin and size in texels, then the min
 *    corner it declares, all relative to its part's rotation point,
 *  - a MobPart is one ModelRenderer: the rotation point it is given, any fixed
 *    rotation angle, and the range of boxes it draws.
 *
 * The texture origins are the vanilla ones, so the boxes line up with the
 * official skin pixel for pixel -- which is the whole point of keeping the
 * numbers in the vanilla convention instead of pre-transforming them.
 */
namespace Mob
{
	struct MobBox
	{
		int8_t u, v;      ///< texture origin in the atlas, texels
		int8_t w, h, d;   ///< extent of the box, model units
		int8_t ox, oy, oz;///< min corner in the part's frame, model units
		/**
		 * Vanilla's CubeDeformation: how far the box is grown on every side without
		 * the texture unwrap growing with it, in *half* model units -- vanilla's usual
		 * deformation is 0.5, and the player's hat is one, so `grow = 1`. The unwrap is
		 * the same either way: a skin's overlay band is drawn for the box underneath.
		 *
		 * These two have no default initialiser on purpose: the tables that predate
		 * them leave both off, and an initialiser would make the struct a non-aggregate
		 * to the calculator's C++11 compiler. Aggregate initialisation already zeroes
		 * whatever is left out, so the older tables read as 0/0 either way.
		 */
		int8_t grow;
		/** vanilla's mirror(): the unwrap is flipped left to right, which is how the
		 *  second arm and leg sample the same sheet region as the first. */
		uint8_t mirror;
	};

	/** What animates a part. Static covers bodies, tails and manes. */
	enum class Pose : uint8_t
	{
		Static = 0,
		Head,
		LegFrontLeft,
		LegFrontRight,
		LegBackLeft,
		LegBackRight
	};

	struct MobPart
	{
		int8_t px, py, pz; ///< rotation point (y = 24 is the ground)
		int8_t rot_x;      ///< fixed rotation in degrees
		uint8_t first_box;
		uint8_t box_count;
		Pose pose;
	};

	struct MobModel
	{
		uint8_t tex_width, tex_height;
		uint8_t part_count;
		const MobPart *parts;
		uint8_t box_count;
		const MobBox *boxes;

		bool empty() const { return part_count == 0; }
	};

	/**
	 * A rotation read from a vanilla box table, restated in the frame nGL draws
	 * the boxes in.
	 *
	 * The tables are vanilla's numbers in vanilla's y-down frame, but nGL draws
	 * the same boxes in a y-up frame (the boxes themselves are emitted with the
	 * sign already flipped, see `draw()`). Flipping one axis is a mirror, and
	 * under a mirror a rotation R becomes M*R*M -- which for the X and Y axes is
	 * the *same* rotation by the opposite angle. So a table angle has to be
	 * negated on the way in, and it is named here so the renderer and the host
	 * test cannot disagree about it: tests/livestock_test.cc computes where each
	 * mob's torso actually lands through this same call.
	 *
	 * This is only for angles that come *out of a table*. An angle the caller
	 * supplies has already been turned into the frame being drawn and is used as
	 * given -- see the head yaw in `draw()`.
	 */
	inline GLFix angleInDrawnFrame(GLFix vanilla_degrees) { return -vanilla_degrees; }

	/**
	 * Emits one box in the current matrix, unwrapped the vanilla way. The chicken
	 * still draws itself, so this is shared with it.
	 */
	void drawBox(
		GLFix bx, GLFix by, GLFix bz,
		GLFix bw, GLFix bh, GLFix bd,
		int u0, int v0, int wp, int hp, int dp,
		bool mirror = false);

	/**
	 * Emits one model. `scale` is the size of a model unit on screen, `leg_swing`
	 * the current walk-cycle angle in degrees and `head_yaw` the head's turn.
	 * The skin must already be bound.
	 */
	void draw(const MobModel &model, GLFix scale, GLFix leg_swing, GLFix head_yaw);
}

#endif // MOBMODEL_H
