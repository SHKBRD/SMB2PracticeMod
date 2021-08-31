#include "decomp.h"

#include <mkb.h>
#include "macro_utils.h"
#include "patch.h"

namespace decomp {

/*
 * Post-goal camera animation
 */
static void camera_goal(mkb::Camera* camera, mkb::Ball* ball) {
    Vec3f ball_delta_camera = VEC_SUB2D(ball->pos, camera->pos);
    f32 dot = VEC_DOT(ball_delta_camera, camera->vel);  // Positive if camera moving towards ball
    mkb::vec_normalize_len(&ball_delta_camera);

    f32 adjusted_dot = dot * -0.01f;

    // Adjust camera velocity

    camera->vel.y *= 0.97f;  // Slow down Y speed

    // If the camera is moving towards the ball, decrease cam vel towards ball,
    // otherwise increase cam vel towards ball. More drastic if camera is further from ball
    camera->vel.x += adjusted_dot * ball_delta_camera.x;
    camera->vel.z += adjusted_dot * ball_delta_camera.z;

    // If camera is moving towards the ball, add some perpendicular motion to the camera
    // vel to help the camera revolve around the ball rather than pass through it
    if (adjusted_dot < 0.f) {
        camera->vel.x += adjusted_dot * 0.5f * ball_delta_camera.z;
        camera->vel.z += adjusted_dot * 0.5f * ball_delta_camera.x;
    }
    camera->pos = VEC_ADD(camera->pos, camera->vel);  // Apply camera velocity to position

    // Adjust pivot velocity
    camera->pivot_vel = VEC_SCALE(0.3f, VEC_SUB(ball->pos, camera->pivot));
    camera->pivot = VEC_ADD(camera->pivot, camera->pivot_vel);  // Apply pivot vel to pivot

    // Adjust camera distance from pivot
    Vec3f camera_delta_pivot = VEC_SUB2D(camera->pos, camera->pivot);
    f32 dist = mkb::math_sqrt(VEC_LEN_SQ(camera_delta_pivot));
    constexpr f32 EPSILON = 1.192092895507813e-07f;
    if (dist > EPSILON) {
        // Coerce the camera towards being 2 units distance away from the pivot
        // in the XZ plane
        f32 scale = (dist + (2.f - dist) * 0.08f) / dist;
        camera->pos.x = camera->pivot.x + camera_delta_pivot.x * scale;
        camera->pos.z = camera->pivot.z + camera_delta_pivot.z * scale;

        // Before the ball starts blasting up, slowly make camera level with ball
        if (!(ball->phys_flags & mkb::PHYS_BLAST_UP)) {
            // Move camera Y towards pivot Y
            camera->pos.y += (camera->pivot.y - camera->pos.y) * 0.01f;
        }
    }

    // Rotate the camera so it's looking at the pivot point
    mkb::ray_to_euler(&camera->pos, &camera->pivot, &camera->rot);
}

/*
 * Post-fallout camera animation
 */
static void camera_fallout(mkb::Camera* camera, mkb::Ball* ball) {
    camera->g_some_flag = 2;  // ??

    // Slow camera to a halt, slowing Y faster than X and Z
    // Maybe to make ball seem like it's receding away faster?
    camera->vel.x *= 0.97f;
    camera->vel.y *= 0.955f;
    camera->vel.z *= 0.97f;

    camera->pos = VEC_ADD(camera->pos, camera->vel);  // Apply camera velocity
    camera->pivot = ball->pos;                        // Look directly at the ball's center
    mkb::ray_to_euler(&camera->pos, &camera->pivot, &camera->rot);  // Look at pivot point
}

void init() {
    // Replace game's functions with our decompiled functions
    patch::hook_function(mkb::g_camera_func15_goal, camera_goal);
    patch::hook_function(mkb::g_camera_func4_fallout, camera_fallout);
}

}  // namespace decomp