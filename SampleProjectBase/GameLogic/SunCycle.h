#pragma once
#include <DirectXMath.h>

// The sun moving around the cottage (time of day). Gameplay/world state only: knows nothing about
// rendering. The scene asks it for "where is the sun" and "how much daylight is there" each frame.
//
// The path: the sun turns once per day on a circle tilted toward the window side (+Z = south here):
//     dir(angle) = cos(angle) * EAST + sin(angle) * (cos(tilt) * UP + sin(tilt) * SOUTH)
//   angle 0 = sunrise in the east, 90 deg = noon (highest, toward the window), 180 = sunset in the
//   west, 180..360 = below the horizon (night). Same idea as the school sample's MoveLight
//   (spherical angles -> a direction), but driven by time instead of keys.
// Daylight fades in/out around the horizon instead of switching off at once (dawn / dusk).
// Search: "day night cycle", "sun position hour angle" / JP: chuuya saikuru, taiyou no ichi
class SunCycle
{
public:
    void  Update(float dt);                     // advances the clock while Running
    DirectX::XMFLOAT3 Direction() const;        // unit vector TOWARD the sun (same convention as the lights)
    float Daylight() const;                     // 0 (night) .. 1 (sun clearly above the horizon)
    float Hour() const;                         // clock for the UI: 6 = sunrise, 12 = noon, 18 = sunset

    // Tunables (public, like HingedDoor, so UI sliders can bind to them).
    float time01      = 0.22f;   // 0..1 through one day. 0 = sunrise, 0.25 = noon, 0.5 = sunset (start: morning)
    float dayLengthSec = 1200.0f; // real seconds for one full day (20 min: daytime lasts ~10 min = one forging session; 240 s felt too fast, 2026-10-17)
    float tiltRad     = 0.925f;  // how far the sun's circle leans toward the window side (rad, 53 deg -> noon elevation 37 deg)
    bool  running     = true;    // false = time stands still (look at one moment)
};
