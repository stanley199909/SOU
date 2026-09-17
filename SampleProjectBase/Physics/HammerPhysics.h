#pragma once

// Spring-damper motion of the smith's hammer head (self-built physics).
// The forge step drives it: Hold() while the player charges, Strike() to swing,
// Update() integrates it back to rest each frame. It owns ONLY the vertical
// motion of the head; placement/orientation/recoil visuals stay in the renderer.
//
// Model (same as the school's SceneSpring): a mass m on a spring of natural
// length restLift. Every frame the net force = restoring force (Hooke) + damping
// gives acceleration, integrated with semi-implicit Euler. Overshoot past the top
// and the settle back to rest both fall out of the k/c/m coefficients.
class HammerPhysics
{
public:
    // --- Tunable coefficients (bound to the F1 sliders and saved to
    //     forge_tuning.txt). Public so the UI/tuning code can edit them directly.
    float restLift    = 0.40f;  // spring natural length = rest height above anvil
    float chargeRaise = 0.55f;  // extra lift at full charge (held by hand)
    float stiffness   = 220.0f; // k: stiffness (bigger = snappier, faster return)
    float damping     = 8.0f;   // c: damping (bigger = settles sooner; critical = 2*sqrt(k*m))
    float mass        = 1.0f;   // m: head mass (bigger = heavier, more sluggish)
    float impulse     = 2.2f;   // J: upward impulse on strike (launch speed v0 = J/m)

    void  Reset();              // sit at rest, zero velocity
    void  Hold(float charge);   // held while charging: lift = rest + charge*raise, v = 0
    void  Strike();             // swing: sink to contact (lift 0) and launch up (v0 = J/m)
    void  Update(float dt);     // integrate the spring-damper toward rest

    float Lift()        const { return m_lift; } // current head height
    float Velocity()    const { return m_vel;  } // current vertical velocity (spring state)
    float LaunchSpeed() const;                    // v0 = J/m, for normalizing the recoil phase

private:
    float m_lift = 0.40f;  // head height (spring position)
    float m_vel  = 0.0f;   // head vertical velocity (spring state)
};
