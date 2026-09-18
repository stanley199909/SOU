#pragma once

// Simulation of the iron being forged (self-built physics).
// It owns the workpiece STATE -- a coarse height field on the anvil, per-segment
// shaping progress, and per-cell damage -- and the physics that changes that
// state when the iron is struck (volume-conserving metal flow).
//
// Boundary: the player's ACTION (swinging the hammer) lives in the forge step;
// this class only answers "given a strike, how does the iron react". Game rules
// (scoring, rhythm, spoil, feedback) stay in the scene and are driven by the
// StrikeOutcome this returns.
//
// Grid: i = length (Z, 0 = tang .. NL-1 = tip), j = width (X, centre = ridge).
// One strike drops the hit cell toward its target height and pushes the displaced
// material into the neediest neighbours, so volume is conserved and the shape
// converges on the target the more you hit it.
class ForgingSim
{
public:
    static const int NL = 20;   // cells along the length (Z)
    static const int NW = 6;    // cells across the width (X)
    static const int NSEG = 5;  // coarse length segments (per-segment morph progress)

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

    // --- read-only views for rendering / aim / HUD ---
    float Height(int i, int j) const { return m_h[i][j]; }
    float Damage(int i, int j) const { return m_dmgF[i][j]; }
    float SegProg(int s)       const { return m_segProg[s]; }
    float Start()              const { return m_hStart; }
    bool  SegDone(int s)       const { return m_segProg[s] >= SEG_DONE; }
    bool  AllSegmentsDone()    const;
    float SegAverage()         const; // mean segment progress (display / morph preview)

private:
    // How strongly the iron reacts (physics magnitudes; the numbers you tune/defend).
    static constexpr float SEG_DONE     = 0.98f;  // a segment counts as finished at/above this
    static constexpr float FORGE_STEP   = 0.055f; // shaping progress from one ideal full strike
    static constexpr float FLOW_DROP    = 0.095f; // height pushed out of the hit cell (ideal strike)
    static constexpr float DMG_COLD_HIT = 0.35f;  // crack from one cold strike
    static constexpr float DMG_OVER_HIT = 0.25f;  // scorch from one overheated strike

    float m_hStart = 0.17f;     // uniform starting thickness = thickest part of the weapon
    float m_h[NL][NW];          // current height (thickness) field
    float m_hTgt[NL][NW];       // target (finished weapon) height field
    float m_dmgF[NL][NW];       // per-cell damage 0..1 (cold crack / overheat scorch)
    float m_segProg[NSEG] = {}; // per-segment shaping progress 0..1
};
