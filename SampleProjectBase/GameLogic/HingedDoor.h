#pragma once

// A door that swings on a hinge between closed and open (gameplay state only).
//
// Knows nothing about models or rendering: it just owns "is it meant to be open" and
// "how far has it swung". The scene turns Angle() into a hinge rotation matrix, draws
// the door with it and moves the door's collision hull with it -- so the picture, the
// collision and the interaction all read the same single value.
//
// Toggle() may be pressed again mid-swing: the door simply turns around from where it
// is (progress runs back), it never snaps.
class HingedDoor
{
public:
    void  Reset();                  // closed and still (new game)
    void  Toggle();                 // start opening if closed(-ing), closing if open(-ing)
    void  Update(float dt);         // advance the swing
    bool  IsOpen() const { return m_open; }     // the state it is heading to
    float Angle() const;            // current swing angle (rad). 0 = closed, openAngle = fully open

    // Tunables (public, like HammerPhysics, so F1 sliders can bind to them).
    float openAngle = 1.5708f;      // how far it opens (rad, = pi/2: square to the wall; more swings the leaf into the stones beside the frame)
    float swingTime = 1.2f;         // seconds for a full swing (slow = heavy wooden door)

private:
    bool  m_open     = false;
    float m_progress = 0.0f;        // 0 = closed .. 1 = open (linear in time; eased in Angle())
};
