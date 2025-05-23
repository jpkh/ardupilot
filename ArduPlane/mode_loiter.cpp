#include "mode.h"
#include "Plane.h"

bool ModeLoiter::_enter()
{
    plane.do_loiter_at_location();
    plane.setup_terrain_target_alt(plane.next_WP_loc);

    // make sure the local target altitude is the same as the nav target used for loiter nav
    // this allows us to do FBWB style stick control
    /*IGNORE_RETURN(plane.next_WP_loc.get_alt_cm(Location::AltFrame::ABSOLUTE, plane.target_altitude.amsl_cm));*/
    if (plane.stick_mixing_enabled() && (plane.flight_option_enabled(FlightOptions::ENABLE_LOITER_ALT_CONTROL))) {
        plane.set_target_altitude_current();
    }

    plane.loiter_angle_reset();

    return true;
}

void ModeLoiter::update()
{
    plane.calc_nav_roll();
    if (plane.stick_mixing_enabled() && plane.flight_option_enabled(FlightOptions::ENABLE_LOITER_ALT_CONTROL)) {
        plane.update_fbwb_speed_height();
    } else {
        plane.calc_nav_pitch();
        plane.calc_throttle();
    }

#if AP_SCRIPTING_ENABLED
    if (plane.nav_scripting_active()) {
        // while a trick is running we reset altitude
        plane.set_target_altitude_current();
        plane.next_WP_loc.set_alt_cm(plane.target_altitude.amsl_cm, Location::AltFrame::ABSOLUTE);
    }
#endif
}

bool ModeLoiter::isHeadingLinedUp(const Location loiterCenterLoc, const Location targetLoc)
{
    // Return true if current heading is aligned to vector to targetLoc.
    // Tolerance is initially 10 degrees and grows at 10 degrees for each loiter circle completed.

    // Corrected radius for altitude
    const float loiter_radius = plane.nav_controller->loiter_radius(fabsf(plane.loiter.radius));
    if (!is_positive(loiter_radius)) {
        // Zero is invalid, protect against divide by zero for destination inside loiter radius case
        return true;
    }

    // Calculate relative position of the vehicle relative to loiter center projected onto the closest point of the loiter circle
    // This removes error due to radial position as the nav controller attempts to track the circle
    const Vector2f projected_pos = loiterCenterLoc.get_distance_NE(plane.current_loc).normalized() * loiter_radius;

    // Target position relative to loiter center
    const Vector2f target_pos = loiterCenterLoc.get_distance_NE(targetLoc);

    // Distance between loiter circle and target
    const float target_dist = target_pos.length();
    if (!is_positive(target_dist)) {
        // Target is coincident with loiter center, no heading will be closer than any other
        return true;
    }

    // Target bearing in centi-degrees
    int32_t target_bearing_cd;

    if (target_dist >= loiter_radius) {
        // Destination outside loiter radius, heading will always line up with destination

        // Vector from between projected vehicle position and target position
        const Vector2f pos_to_target = target_pos - projected_pos;
        target_bearing_cd = degrees(pos_to_target.angle()) * 100;

    } else {
        // Destination is inside loiter, heading will never line up with destination

        // Advance turn point by the angle of a segment with chord "a"
        // This results in turning earlier as the target point approaches the center
        // If target is on radius angle of 0 and angle of 60 deg if target is on center 
        const float a = loiter_radius - target_dist;
        const float segment_angle = 2.0 * asinf(a / (2.0 * loiter_radius));

        // Pick the intersection point that will be hit first for the current loiter direction, add 90 deg to get the tangent angle
        target_bearing_cd = degrees(wrap_PI(target_pos.angle() + (M_PI_2 - segment_angle) * plane.loiter.direction)) * 100;

    }

    // Ideal heading in centi-degrees, +- 90 to get tangent to loiter circle at closest point
    const int32_t current_heading_cd = degrees(wrap_PI(projected_pos.angle() + M_PI_2 * plane.loiter.direction)) * 100;

    return isHeadingLinedUp_cd(target_bearing_cd, current_heading_cd);
}


bool ModeLoiter::isHeadingLinedUp_cd(const int32_t bearing_cd) {

    // get current heading.
    const int32_t heading_cd = (wrap_360(degrees(ahrs.groundspeed_vector().angle())))*100;

    return isHeadingLinedUp_cd(bearing_cd, heading_cd);
}


bool ModeLoiter::isHeadingLinedUp_cd(const int32_t bearing_cd, const int32_t heading_cd)
{
    // Return true if current heading is aligned to bearing_cd.
    // Tolerance is initially 10 degrees and grows at 10 degrees for each loiter circle completed.
    const int32_t heading_err_cd = wrap_180_cd(bearing_cd - heading_cd);

    /*
      Check to see if the the plane is heading toward the land
      waypoint. We use 20 degrees (+/-10 deg) of margin so that
      we can handle 200 degrees/second of yaw.

      After every full circle, extend acceptance criteria to ensure
      aircraft will not loop forever in case high winds are forcing
      it beyond 200 deg/sec when passing the desired exit course
    */

    // Use integer division to get discrete steps
    const int32_t expanded_acceptance = 1000 * (labs(plane.loiter.sum_cd) / 36000);

    if (labs(heading_err_cd) <= 1000 + expanded_acceptance) {
        // Want to head in a straight line from _here_ to the next waypoint instead of center of loiter wp

        // 0 to xtrack from center of waypoint, 1 to xtrack from tangent exit location
        if (plane.next_WP_loc.loiter_xtrack) {
            plane.next_WP_loc = plane.current_loc;
        }
        return true;
    }
    return false;
}

void ModeLoiter::navigate()
{
    if (plane.flight_option_enabled(FlightOptions::ENABLE_LOITER_ALT_CONTROL)) {
        // update the WP alt from the global target adjusted by update_fbwb_speed_height
        plane.next_WP_loc.set_alt_cm(plane.target_altitude.amsl_cm, Location::AltFrame::ABSOLUTE);
    }

#if AP_SCRIPTING_ENABLED
    if (plane.nav_scripting_active()) {
        // don't try to navigate while running trick
        return;
    }
#endif

    // Zero indicates to use WP_LOITER_RAD
    plane.update_loiter(0);
}

void ModeLoiter::update_target_altitude()
{
    if (plane.stick_mixing_enabled() && (plane.flight_option_enabled(FlightOptions::ENABLE_LOITER_ALT_CONTROL))) {
        return;
    }
    Mode::update_target_altitude();
}
