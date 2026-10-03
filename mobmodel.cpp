#include "mobmodel.h"

#include "gl.h"
#include "texturetools.h"

/**
 * Draws the vanilla mob models described in mobmodel.h.
 *
 * The geometry is already the vanilla one; all this has to add is the vanilla
 * transform: every part is drawn in a frame whose origin is its rotation point,
 * with the y axis flipped (the tables count downward from the head, nGL counts
 * upward from the feet) and any fixed rotation applied. A box drawn in that
 * frame uses its own texture unwrap, so the official skin lands on the right
 * faces without any per-mob special-casing.
 */
namespace Mob
{
	namespace
	{
		TextureAtlasEntry skinArea(int u, int v, int w, int h)
		{
			return { static_cast<unsigned>(u), static_cast<unsigned>(u + w),
			         static_cast<unsigned>(v), static_cast<unsigned>(v + h) };
		}

		TextureAtlasEntry mirrorU(TextureAtlasEntry t)
		{
			const unsigned tmp = t.left;
			t.left = t.right;
			t.right = tmp;
			return t;
		}

		void emitQuad(
			GLFix ax, GLFix ay, GLFix az,
			GLFix bx, GLFix by, GLFix bz,
			GLFix cx, GLFix cy, GLFix cz,
			GLFix dx, GLFix dy, GLFix dz,
			const TextureAtlasEntry &tex)
		{
			const COLOR flags = TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE;
			nglAddVertex({ ax, ay, az, GLFix(static_cast<int>(tex.left)),  GLFix(static_cast<int>(tex.bottom)), flags });
			nglAddVertex({ bx, by, bz, GLFix(static_cast<int>(tex.left)),  GLFix(static_cast<int>(tex.top)),    flags });
			nglAddVertex({ cx, cy, cz, GLFix(static_cast<int>(tex.right)), GLFix(static_cast<int>(tex.top)),    flags });
			nglAddVertex({ dx, dy, dz, GLFix(static_cast<int>(tex.right)), GLFix(static_cast<int>(tex.bottom)), flags });
		}
	}

	void drawBox(
		GLFix bx, GLFix by, GLFix bz,
		GLFix bw, GLFix bh, GLFix bd,
		int u0, int v0, int wp, int hp, int dp,
		bool mirror)
	{
		TextureAtlasEntry top  = skinArea(u0 + dp,           v0,      wp, dp);
		TextureAtlasEntry bot  = skinArea(u0 + dp + wp,      v0,      wp, dp);
		TextureAtlasEntry rgt  = skinArea(u0,                v0 + dp, dp, hp);
		TextureAtlasEntry frt  = skinArea(u0 + dp,           v0 + dp, wp, hp);
		TextureAtlasEntry lft  = skinArea(u0 + dp + wp,      v0 + dp, dp, hp);
		TextureAtlasEntry bck  = skinArea(u0 + dp + wp + dp, v0 + dp, wp, hp);

		if(mirror)
		{
			top = mirrorU(top);
			bot = mirrorU(bot);
			TextureAtlasEntry tmp = mirrorU(rgt);
			rgt = mirrorU(lft);
			lft = tmp;
			frt = mirrorU(frt);
			bck = mirrorU(bck);
		}

		const GLFix x0 = bx, x1 = bx + bw;
		const GLFix y0 = by, y1 = by + bh;
		const GLFix z0 = bz, z1 = bz + bd;

		emitQuad(x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0, frt);
		emitQuad(x1, y0, z1, x1, y1, z1, x0, y1, z1, x0, y0, z1, bck);
		emitQuad(x0, y0, z1, x0, y1, z1, x0, y1, z0, x0, y0, z0, rgt);
		emitQuad(x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1, lft);
		emitQuad(x0, y1, z0, x0, y1, z1, x1, y1, z1, x1, y1, z0, top);
		emitQuad(x1, y0, z0, x1, y0, z1, x0, y0, z1, x0, y0, z0, bot);
	}

	void draw(const MobModel &model, GLFix scale, GLFix leg_swing, GLFix head_yaw)
	{
		for(uint8_t p = 0; p < model.part_count; ++p)
		{
			const MobPart &part = model.parts[p];

			// The tables hold vanilla's angles in vanilla's y-down frame, but
			// nGL draws the same boxes in a y-up frame, so every angle goes
			// through `angleInDrawnFrame` (see mobmodel.h for why that is a
			// negation, and for what it looks like when it is skipped).
			GLFix rot_x(angleInDrawnFrame(GLFix(part.rot_x)));
			bool yaw = false;
			switch(part.pose)
			{
			case Pose::Head:
				yaw = true;
				break;
			case Pose::LegFrontLeft:
			case Pose::LegBackRight:
				rot_x += leg_swing;
				break;
			case Pose::LegFrontRight:
			case Pose::LegBackLeft:
				rot_x -= leg_swing;
				break;
			default:
				break;
			}

			// nGL indexes its sine table by the angle itself, so an angle outside one
			// turn has to be wrapped before it is used: a leg swinging the other way
			// gives a negative pitch, which used to read off the end of that table.
			// The camera wraps its own pitch and yaw the same way (worldtask.cpp).
			rot_x.normaliseAngle();
			// `head_yaw` is deliberately *not* negated. It does not come from a
			// table: the caller hands it over already turned into the frame being
			// drawn (playermodel.cpp rotates the whole model itself and passes the
			// same angle again so the head ends up facing twice as far as the
			// body, which is vanilla's rule). Negating it here would cancel that
			// outer turn instead of reinforcing it, and would freeze the head.
			head_yaw.normaliseAngle();

			glPushMatrix();
			// The tables use vanilla model space (y = 24 is the ground); nGL puts
			// the feet on y = 0, so the flip happens here, once.
			glTranslatef(GLFix(part.px) * scale,
			             GLFix(24 - part.py) * scale,
			             GLFix(part.pz) * scale);
			// Vanilla applies Y before X (ModelRenderer.render).
			if(yaw && head_yaw != GLFix(0))
				nglRotateY(head_yaw);
			if(rot_x != GLFix(0))
				nglRotateX(rot_x);

			for(uint8_t b = 0; b < part.box_count; ++b)

			{
				const MobBox &box = model.boxes[part.first_box + b];
				// CubeDeformation grows the geometry on every side; the unwrap is
				// still the one for the box underneath, which is why the grown
				// extents are passed separately from the texture extents. The
				// table's `grow` is in half units.
				const GLFix g = GLFix(box.grow) / GLFix(2);
				drawBox((GLFix(box.ox) - g) * scale,
				        // The box reaches `h` further down in model space, which is
				        // `h` lower in nGL, so its nGL bottom is -(oy + h).
				        (GLFix(-(box.oy + box.h)) - g) * scale,
				        (GLFix(box.oz) - g) * scale,
				        (GLFix(box.w) + g * 2) * scale,
				        (GLFix(box.h) + g * 2) * scale,
				        (GLFix(box.d) + g * 2) * scale,
				        box.u, box.v, box.w, box.h, box.d, box.mirror != 0);
			}

			glPopMatrix();
		}
	}
}
