#include "playermodel.h"

#include <cmath>

#include "gl.h"
#include "textures/steve.h"

/**
 * The player's model and the projection the inventory window draws it with.
 *
 * The geometry is `PlayerModel.createMesh` (the classic, non-slim branch) laid
 * over `HumanoidModel.createMesh`, transcribed into the Mob tables so it is the
 * same `Mob::draw` the world's mobs use -- see mobmodel.h for why the numbers
 * stay in vanilla's own convention. Boxes are vanilla model units, 1/16 of a
 * block, with y = 24 the ground; texture origins are the vanilla ones, so the
 * official Steve skin lands on the right faces without any per-box tweaking.
 *
 * The only thing that is not the model is the camera. nGL projects a vertex by
 * `near_plane / z` and then centres it on the screen, so a model hung off the
 * near plane is drawn at 1:1 with the screen's centre as origin: translating
 * that origin to the feet places the whole figure exactly where the window wants
 * it, with no separate projection matrix to write.
 */
namespace PlayerModel
{
	namespace
	{
		// The classic Steve boxes, read against PlayerModel.createMesh and
		// HumanoidModel.createMesh:
		//
		//   head        texOffs(0,0)   head + hat overlaid
		//   body        texOffs(16,16) torso + jacket
		//   arms        right texOffs(40,16), left texOffs(32,48) -- the classic
		//               model gives each arm its own region instead of mirroring
		//   legs        right texOffs(0,16), left texOffs(16,48), likewise
		//
		// The overlays -- hat, jacket, sleeves and pants -- are the same boxes
		// under a CubeDeformation. Vanilla grows the hat by 0.5 and the rest by
		// 0.25; the table's `grow` is in half units and cannot hold the 0.25, so
		// they all take the one half-unit. At the 30px-per-block the window draws
		// at, the two differ by under a tenth of a pixel.
		const Mob::MobBox player_boxes[] = {
			{  0,  0, 8,  8, 8, -4, -8, -4 },    // head
			{ 32,  0, 8,  8, 8, -4, -8, -4, 1 }, // hat
			{ 16, 16, 8, 12, 4, -4,  0, -2 },    // body
			{ 16, 32, 8, 12, 4, -4,  0, -2, 1 }, // jacket
			{ 40, 16, 4, 12, 4, -3, -2, -2 },    // right arm
			{ 40, 32, 4, 12, 4, -3, -2, -2, 1 }, // right sleeve
			{ 32, 48, 4, 12, 4, -1, -2, -2 },    // left arm
			{ 48, 48, 4, 12, 4, -1, -2, -2, 1 }, // left sleeve
			{  0, 16, 4, 12, 4, -2,  0, -2 },    // right leg
			{  0, 32, 4, 12, 4, -2,  0, -2, 1 }, // right pants
			{ 16, 48, 4, 12, 4, -2,  0, -2 },    // left leg
			{  0, 48, 4, 12, 4, -2,  0, -2, 1 }, // left pants
		};

		// The vanilla setPos() offsets. Vanilla hangs the legs at +/-1.9; the
		// nearest whole model unit stands in, a tenth of a unit being well under a
		// pixel here. The arms and legs are Static: the window draws the player
		// standing with the arms at their sides, which is how an unposed humanoid
		// model already sits.
		const Mob::MobPart player_parts[] = {
			{  0,  0, 0, 0, 0, 2, Mob::Pose::Head },   // head and hat
			{  0,  0, 0, 0, 2, 2, Mob::Pose::Static }, // body and jacket
			{ -5,  2, 0, 0, 4, 2, Mob::Pose::Static }, // right arm and sleeve
			{  5,  2, 0, 0, 6, 2, Mob::Pose::Static }, // left arm and sleeve
			{ -2, 12, 0, 0, 8, 2, Mob::Pose::Static }, // right leg and pants
			{  2, 12, 0, 0, 10, 2, Mob::Pose::Static },// left leg and pants
		};

		const Mob::MobModel player_model = { 64, 64, 6, player_parts, 12, player_boxes };

		/**
		 * Vanilla's `Math.atan(v / 40)` with the result kept in radians, the way
		 * `renderEntityInInventory` uses it: its `f` is a radian value that it then
		 * multiplies by 20 or 40 and treats as degrees. All this has to reproduce is
		 * that oddity, so the number is computed the same way.
		 */
		GLFix mouseAtan(int v)
		{
			return GLFix(std::atan(static_cast<float>(v) / 40.0f));
		}
	}

	const Mob::MobModel &model()
	{
		return player_model;
	}

	const TEXTURE &skin()
	{
		return steve_tex;
	}

	void drawInGui(int feet_x, int feet_y, int pixels_per_block, GLFix yaw, GLFix pitch)
	{
		// The GUI is blitted straight into the framebuffer, so the world's depth
		// values are still sitting behind the window and would swallow the model.
		// Only the depth buffer is cleared: the window's own pixels must stay.
		glClear(GL_DEPTH_BUFFER_BIT);

		// Vanilla draws the window with an orthographic projection, so the player's
		// depth does not change its size. nGL always divides by z, so the way to get
		// the same picture is to push the model far enough out that the divide is
		// effectively flat: at ten times the usual near plane the model's own depth
		// bends its projection by well under a pixel. The camera is put back
		// afterwards.
		const GLFix saved_near = nglGetNearPlane();
		const GLFix near = saved_near * GLFix(10);
		nglSetNearPlane(near);

		glPushMatrix();
		glLoadIdentity();

		// Hang the model off the near plane, where nGL projects it 1:1 with the
		// screen's centre as the origin, and slide that origin to the feet. The
		// screen's centre sits at (SCREEN_WIDTH/2, SCREEN_HEIGHT/2) and y is flipped
		// on the way out, which is where the `SCREEN_HEIGHT/2 - 1` comes from.
		glTranslatef(GLFix(feet_x - SCREEN_WIDTH / 2),
		             GLFix(SCREEN_HEIGHT / 2 - 1 - feet_y),
		             near);

		// The model is built facing -z, the direction the camera looks, so at zero
		// yaw the player already faces the window. yaw is the turn the mouse rule
		// asks for and pitch the matching lean about the model's side-to-side axis.
		// nGL indexes its sine table by the angle's raw value, so the mouse rule's
		// negative angles (the pointer off to one side) have to be wrapped first.
		yaw.normaliseAngle();
		pitch.normaliseAngle();
		if(yaw != GLFix(0))
			nglRotateY(yaw);
		if(pitch != GLFix(0))
			nglRotateX(pitch);

		glBindTexture(&steve_tex);
		glBegin(GL_QUADS);
		// Vanilla turns the head twice as far as the body, and hands the whole
		// model the body's turn. Mob::draw's head yaw is the extra turn on top, so
		// passing the body's yaw again gives the head its doubled one.
		Mob::draw(player_model, GLFix(pixels_per_block) / GLFix(16), GLFix(0), yaw);
		glEnd();

		glPopMatrix();
		nglSetNearPlane(saved_near);
	}

	GLFix yawForMouse(int dx)
	{
		// Vanilla's body turn is half the head's: it sets yRot = 180 + f*40 and the
		// head's relative turn is the remaining f*20, so the body's share is f*20.
		return mouseAtan(dx) * GLFix(20);
	}

	GLFix pitchForMouse(int dy)
	{
		// Vanilla leans the model by -f1*20 for f1 = atan(dy/40).
		return -mouseAtan(dy) * GLFix(20);
	}
}
