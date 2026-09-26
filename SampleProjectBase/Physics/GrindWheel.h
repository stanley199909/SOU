#pragma once

// Pedal-driven grindstone wheel (self-built physics).
//
// A medieval treadle wheel does not keep spinning while you hold the pedal: every
// stroke of the foot pushes it once, and between strokes it slows down on its own.
// So the model is "impulse + exponential decay":
//   - Pedal():  one stroke adds a fixed angular speed (an impulse). A human leg can
//               only stroke so fast, so strokes closer than PEDAL_MIN_INTERVAL are ignored.
//   - Update(): friction removes speed in proportion to speed:  d(omega)/dt = -k * omega
//               Solved exactly per frame: omega *= exp(-k * dt)  (frame-rate independent).
//               Pressing the blade on the stone adds drag (k grows) -> you must keep pedalling.
// The result: tap rhythm -> steady speed. Steady state ~= impulse * tapsPerSecond / k.
class GrindWheel
{
public:
    void  Reset();
    bool  Pedal();                                 // one foot stroke; false if the leg is still recovering
    void  Update(float dt, bool bladePressed);     // decay + angle integration
    float Speed() const { return m_speed; }        // angular speed (rad/s)
    float Speed01() const;                         // Speed normalised by maxSpeed (0..1)
    float Angle() const { return m_angle; }        // accumulated rotation (rad), for drawing the wheel

    // Tunables (public, like HammerPhysics, so F1 sliders can bind to them).
    float pedalImpulse = 2.2f;   // rad/s added by one stroke
    float maxSpeed     = 10.0f;  // physical top speed of the wheel (rad/s)
    float friction     = 0.45f;  // bearing friction decay rate k (1/s) while free-spinning
    float bladeDrag    = 0.90f;  // extra decay rate while the blade is pressed on the stone

    static constexpr float PEDAL_MIN_INTERVAL = 0.2f; // fastest possible stroke = 5 per second

private:
    float m_speed = 0.0f;
    float m_angle = 0.0f;
    float m_sincePedal = PEDAL_MIN_INTERVAL;  // time since the last accepted stroke
};
