#include "SunCycle.h"
#include <cmath>

using namespace DirectX;

namespace
{
    const float TWO_PI        = 6.28318531f;
    const float HORIZON_FADE  = 0.08f;   // daylight ramps over this band of sin(elevation) around the horizon (dawn/dusk)
    const float HOURS_PER_DAY = 24.0f;
    const float SUNRISE_HOUR  = 6.0f;    // time01 = 0 shown as 6:00
    const float MIN_DAY_SEC   = 1.0f;    // guard against a zero day length
    // world axes of the path (the window wall faces +Z)
    const XMFLOAT3 EAST (1.0f, 0.0f, 0.0f);
    const XMFLOAT3 UP   (0.0f, 1.0f, 0.0f);
    const XMFLOAT3 SOUTH(0.0f, 0.0f, 1.0f);
}

void SunCycle::Update(float dt)
{
    if (!running) return;
    time01 += dt / fmaxf(dayLengthSec, MIN_DAY_SEC);
    time01 -= floorf(time01);   // wrap to 0..1
}

XMFLOAT3 SunCycle::Direction() const
{
    const float a = time01 * TWO_PI;
    const float c = cosf(a), s = sinf(a);
    const float up = cosf(tiltRad), south = sinf(tiltRad);
    return XMFLOAT3(c * EAST.x + s * (up * UP.x + south * SOUTH.x),
                    c * EAST.y + s * (up * UP.y + south * SOUTH.y),
                    c * EAST.z + s * (up * UP.z + south * SOUTH.z));
}

float SunCycle::Daylight() const
{
    const float elevationSin = Direction().y;
    const float t = (elevationSin + HORIZON_FADE) / (2.0f * HORIZON_FADE);
    const float k = fminf(fmaxf(t, 0.0f), 1.0f);
    return k * k * (3.0f - 2.0f * k);   // smoothstep: no visible kink at dawn/dusk
}

float SunCycle::Hour() const
{
    const float h = SUNRISE_HOUR + time01 * HOURS_PER_DAY;
    return h - floorf(h / HOURS_PER_DAY) * HOURS_PER_DAY;
}
