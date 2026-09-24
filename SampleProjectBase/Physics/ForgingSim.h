#pragma once

// Simulation of the iron being forged (self-built physics).
// It owns the workpiece STATE -- a coarse height field on the anvil, per-segment
// shaping progress, and per-cell damage -- and the physics that changes that
// state when the iron is struck (volume-conserving metal flow).
//
// Boundary: the player's ACTION (swinging the hammer) lives in the forge step;
// this class only answers "given a strike, how does the iron react". Game rules
// (scoring, rhythm, feedback) stay in the scene and are driven by the
// StrikeOutcome this returns.
//
// Grid: i = length (Z, 0 = tang .. NL-1 = tip), j = width (X, centre = ridge).
// One strike drops the hit cell toward its target height and pushes the displaced
// material into the neediest neighbours, so volume is conserved and the shape
// converges on the target the more you hit it.
class ForgingSim
{
public:
    static const int NL = 20;    // cells along the length (Z)
    static const int NW = 6;     // cells across the width (X)
    static const int NSEG = 5;   // coarse length segments (per-segment morph progress)
    static const int NSIDES = 2; // the blade has two faces; each is forged independently

    // What one strike did to the iron. The scene switches on this for audio/score/
    // feedback (game rules), which are not this class's job.
    enum class StrikeOutcome { Shaped, ColdHit, OverHit, AlreadyDone };

    void  Reset();            // flat billet, no damage, zero progress, and rebuild the target
    void  BuildTarget();      // compute the target (finished-weapon) height field
    float ShapeMatch() const; // 0..1 match of current vs target (no-FBX fallback path)

    // Apply one strike at cell (ci,cj) / segment seg. power/heatFactor/grooveMult are
    // gameplay modifiers the scene already computed; cold/over say the temperature was
    // bad. Deforms the metal, marks damage on a bad hit, advances shaping on a good one.
    StrikeOutcome ApplyStrike(int ci, int cj, int seg,
                              float power, float heatFactor, float grooveMult,
                              bool cold, bool over);

    void  BurnAll(float amount); // overheating scorches every cell (adds damage, clamped)

    // --- temperature of the iron (0 = cold .. 1 = white hot) ---
    // Temperature belongs to the iron, not to what the player is doing: it keeps
    // cooling every frame of play, whether the player is at the anvil or walking.
    // Heat SOURCES (the forge, a strike soaking heat into the anvil) live outside
    // and push heat in/out through AddHeat.
    void  Cool(float dt);        // natural cooling over time (call every simulated frame)
    void  AddHeat(float amount); // +heat from the forge, -heat lost to a strike (clamped 0..1)
    float Heat() const { return m_heat; }
    float coolRate = 0.03f;      // natural cooling speed (/sec). Tunable, like Hammer's public params

    // Turn the workpiece over so the other face is up. The player triggers this in
    // the flip step (tongs); after it, strikes and every read below refer to the
    // newly-up face. State is untouched -- flipping only swaps which side is active.
    void  Flip() { m_side ^= 1; }
    void  SetSide(int s) { m_side = s ? 1 : 0; } // set which face is up directly (flip UI commits a face)
    int   Side() const { return m_side; }        // which face is up (0 = front, 1 = back)

    // --- read-only views for rendering / aim / HUD ---
    // These always report the face that is currently up (m_side), so callers that
    // draw / aim at "the visible face" need no change when the piece is flipped.
    float Height(int i, int j) const { return m_h[m_side][i][j]; }
    float Damage(int i, int j) const { return m_dmgF[m_side][i][j]; }
    float SegProg(int s)       const { return m_segProg[m_side][s]; }
    float SegProgOf(int side, int s) const { return m_segProg[side ? 1 : 0][s]; } // a specific face (renderer needs both)
    float Start()              const { return m_hStart; }
    bool  SegDone(int s)       const { return m_segProg[m_side][s] >= SEG_DONE; }
    bool  BothSidesDone()      const; // every segment of BOTH faces is shaped (ends the Forge step)
    float SegAverage()         const; // mean segment progress of the up face (display / morph preview)

private:
    // How strongly the iron reacts (physics magnitudes; the numbers you tune/defend).
    static constexpr float SEG_DONE     = 0.98f;  // a segment counts as finished at/above this
    static constexpr float FORGE_STEP   = 0.055f; // shaping progress from one ideal full strike
    static constexpr float FLOW_DROP    = 0.095f; // height pushed out of the hit cell (ideal strike)
    static constexpr float DMG_COLD_HIT = 0.35f;  // crack from one cold strike
    static constexpr float DMG_OVER_HIT = 0.25f;  // scorch from one overheated strike

    int   m_side = 0;                 // which face is up right now (0 = front, 1 = back)
    float m_heat = 0.0f;              // temperature 0..1 (one value for the whole piece)
    float m_hStart = 0.17f;           // uniform starting thickness = thickest part of the weapon
    float m_h[NSIDES][NL][NW];        // current height (thickness) field, per face
    float m_hTgt[NL][NW];             // target (finished weapon) height field (same shape for both faces)
    float m_dmgF[NSIDES][NL][NW];     // per-cell damage 0..1 (cold crack / overheat scorch), per face
    float m_segProg[NSIDES][NSEG] = {}; // per-segment shaping progress 0..1, per face
};
