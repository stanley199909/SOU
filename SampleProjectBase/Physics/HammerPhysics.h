#pragma once

// Spring-damper motion of the smith's hammer head (self-built physics).
// The forge step drives it: Hold() while the player charges, Release() to swing,
// Update() integrates it each frame. It owns ONLY the vertical motion of the head;
// placement/orientation/recoil visuals stay in the renderer.
//
// Three phases (2026-10-27: the head now really travels down and touches the steel):
//   1. Held    : Hold(charge) lifts the head by hand (rest + charge*raise), no velocity.
//   2. Swing   : Release(charge) throws it down. The arm + gravity accelerate it at a
//                constant rate until it reaches the steel (lift 0) = the CONTACT event.
//                Impact speed v = sqrt(v0^2 + 2*a*h): a higher, harder swing hits faster.
//   3. Rebound : the caller reads the contact (ConsumeContact), works out what was hit,
//                and calls Rebound(e): the head leaves at e x impact speed
//                (coefficient of restitution: soft hot steel absorbs, hard cold steel /
//                the bare anvil throws it back). From there the spring-damper (same model
//                as the school's SceneSpring) carries it back to rest:
//                  net force = restoring force (Hooke) + damping -> acceleration,
//                  integrated with semi-implicit Euler.
// Search: "coefficient of restitution", "semi-implicit Euler", "damped spring".
class HammerPhysics
{
public:
    // --- Tunable coefficients (bound to the F5 sliders and saved to
    //     forge_tuning.txt). Public so the UI/tuning code can edit them directly.
    float restLift    = 0.40f;  // spring natural length = rest height above anvil
    float chargeRaise = 0.55f;  // extra lift at full charge (held by hand)
    float stiffness   = 220.0f; // k: stiffness (bigger = snappier, faster return)
    float damping     = 8.0f;   // c: damping (bigger = settles sooner; critical = 2*sqrt(k*m))
    float mass        = 1.0f;   // m: head mass (bigger = heavier, more sluggish)
    // swing (phase 2)
    float swingSpeedMin  = 2.0f;  // downward speed the arm gives at release, no charge (m/s)
    float swingSpeedGain = 3.0f;  // extra release speed at full charge (m/s)
    float swingAccel     = 12.0f; // arm push + gravity while falling (m/s^2)
    // rebound (phase 3): e = rebound speed / impact speed, per what the head hit
    float restitutionHot   = 0.35f; // hot (soft) steel absorbs the blow ("dead" hit)
    float restitutionCold  = 0.55f; // cold (hard) steel throws the head back
    float restitutionAnvil = 0.60f; // missed the steel: the bare anvil face

    void  Reset();                  // sit at rest, zero velocity, no swing
    void  Hold(float charge);       // held while charging: lift = rest + charge*raise, v = 0
    void  Release(float charge);    // start the downswing from the held height
    void  Update(float dt);         // swing (until contact) or spring-damper (afterwards)
    bool  ConsumeContact(float& impactSpeed); // true once, on the frame the head reached the steel
    void  Rebound(float restitution);         // after a contact: leave upward at e x impact speed

    float Lift()        const { return m_lift; } // current head height
    float Velocity()    const { return m_vel;  } // current vertical velocity (spring state)
    bool  Swinging()    const { return m_swinging; }
    float ReboundSpeed() const { return m_rebound; } // speed it left the last contact with (recoil visuals)
    float ImpactSpeedFor(float charge) const;    // impact speed of a swing released at that charge
    float ImpactPower(float impactSpeed) const;  // impact speed -> 0..1 (no charge .. full charge)

private:
    float m_lift = 0.40f;     // head height (spring position)
    float m_vel  = 0.0f;      // head vertical velocity (spring state)
    bool  m_swinging = false; // phase 2
    bool  m_contact  = false; // reached the steel this frame, not yet consumed
    float m_impact   = 0.0f;  // speed at the contact
    float m_rebound  = 0.0f;  // speed it left the last contact with
};
