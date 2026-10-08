#pragma once
#include "WorkField.h"

// Simulation of the iron being forged (self-built physics).
// It owns the workpiece STATE -- a coarse height field on the anvil, per-segment
// shaping progress, and per-cell damage -- and the physics that changes that
// state when the iron is struck (volume-conserving metal flow).
//
// Shaping and grinding are both tracked as REMAINING WORK (WorkField), not accumulated progress. Each face starts
// with 100% of its work left, spread over the length cells (1 per cell, NL cells).
// Every valid strike subtracts exactly the same amount (100% / strikesPerFace) from
// that face -- the strike's grade (power, rhythm) only affects the score, never the
// work. So a face is finished after exactly strikesPerFace valid strikes: completion
// is guaranteed, nothing can stop at 99%.
// Where the work is taken from is local: mostly the cells under the hammer (Gaussian
// falloff), and what those cells cannot absorb (already finished) goes to the nearest
// unfinished cells. So the iron changes where it was hit. The NSEG segments are only a
// grouping read from the cells (HUD/F1), never what a strike writes.
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
    static const int NSEG = 5;   // coarse length segments (grouping of the length cells for HUD/done checks)
    static const int CELLS_PER_SEG = NL / NSEG;
    static_assert(NL % NSEG == 0, "length cells must split evenly into segments");
    static_assert(NL == WorkField::N, "the work fields use the same length cells as the height field");
    static const int NSIDES = 2; // the blade has two faces; each is forged independently

    // What one strike did to the iron. The scene switches on this for audio/score/
    // feedback (game rules), which are not this class's job.
    enum class StrikeOutcome { Shaped, ColdHit, OverHit, AlreadyDone };

    void  Reset();            // flat billet, no damage, zero progress, and rebuild the target
    void  BuildTarget();      // compute the target (finished-weapon) height field
    float ShapeMatch() const; // 0..1 match of current vs target (no-FBX fallback path)

    // Apply one strike at cell (ci,cj) of the height field. lenCoord is where the hammer
    // head landed along the weapon's length, in cells (0..NL, continuous; the same
    // local-length convention the renderer uses), and is the centre of the shaping
    // falloff. power/heatFactor/grooveMult are gameplay modifiers the scene already
    // computed; cold/over say the temperature was bad. Deforms the metal, marks damage
    // on a bad hit, advances shaping on a good one.
    StrikeOutcome ApplyStrike(int ci, int cj, float lenCoord,
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
    void  SetHeat(float heat);   // set directly (clamped 0..1). For the opening: the iron is already hot on the anvil
    float Heat() const { return m_heat; }
    float coolRate = 0.008f;     // natural cooling speed (/sec). ~65 s from burning (0.87) to too cold (0.35). Tunable
    // Valid strikes (hot enough, not overheated, on unfinished metal) one face needs. Every
    // such strike removes exactly 1/strikesPerFace of the face's work. Tunable
    // (the forge step was too long next to heat/grind/quench in the 2026-10-02 playtest).
    float strikesPerFace = 14.0f;
    // Seconds of valid grinding (pressed on a spinning wheel at a usable angle) one bevel
    // needs. Valid grinding removes work at a fixed rate; angle/speed only grade it. Tunable
    float grindSecondsPerSide = 15.0f;
    float grindSpread = 1.0f;    // Gaussian sigma (cells) of the wheel contact along the length
    // How far one strike's shaping spreads along the length: the standard deviation of
    // the Gaussian falloff, in length cells. Small = only the spot under the hammer moves
    // (precise but more strikes to cover the blade); large = feels like the old segments.
    float strikeSpread = 1.5f;

    // At this temperature the steel starts to throw sparks from its surface ("burning").
    // It is the visible signal a smith reads: the iron is ready (Heat step target).
    // Kept below the scene's OVERHEAT line, so there is a window to pull it out in time.
    static constexpr float BURN_TEMP = 0.80f;
    bool  IsBurning() const { return m_heat >= BURN_TEMP; }

    // --- edge sharpness (grinding) ---
    // Remaining work per bevel (each face of the blade gets its own bevel), along the same
    // length cells as shaping. Grinding only ever subtracts.
    enum class GrindOutcome { Sharpened, AlreadySharp };
    // Grind bevel `side` for dt seconds at lenCoord (contact along the length, cells 0..NL).
    // Call only while the grinding is valid (pressed, wheel spinning, usable angle).
    GrindOutcome ApplyGrind(int side, float lenCoord, float dt);
    float EdgeCellProgOf(int side, int i) const { return m_edge[side ? 1 : 0].CellProgress(i); } // renderer
    bool  EdgeCellDoneOf(int side, int i) const { return m_edge[side ? 1 : 0].CellDone(i); }
    bool  SharpDoneOf(int side, int s)  const { return m_edge[side ? 1 : 0].SegDone(s, NSEG); }     // F1 readout
    float SharpRatioOf(int side, int s) const { return m_edge[side ? 1 : 0].SegProgress(s, NSEG); } // F1 readout
    bool  AllSharp() const { return m_edge[0].Done() && m_edge[1].Done(); } // both bevels ground (ends the Grind step)

    // Debug step jump (F1): put the piece into the state "this step was already done".
    void  CompleteForging();  // both faces shaped to the target, no damage
    void  CompleteGrinding(); // every edge segment ground

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
    float CellProgOf(int side, int i) const { return m_shape[side ? 1 : 0].CellProgress(i); }  // shaping progress of one length cell = 1 - work left (renderer)
    bool  CellDoneOf(int side, int i) const { return m_shape[side ? 1 : 0].CellDone(i); }      // no work left (exact)
    float SegProgOf(int side, int s)  const { return m_shape[side ? 1 : 0].SegProgress(s, NSEG); } // F1 readout
    bool  SegDoneOf(int side, int s)  const { return m_shape[side ? 1 : 0].SegDone(s, NSEG); }
    float Start()              const { return m_hStart; }
    bool  BothSidesDone()      const; // every segment of BOTH faces is shaped (ends the Forge step)
    bool  SideDone(int side)     const { return m_shape[side ? 1 : 0].Done(); }     // ONE face is shaped (HUD: "flip it now")
    float SideProgress(int side) const { return m_shape[side ? 1 : 0].Progress(); } // 0..1 work done on one face (HUD bar)
    float SharpProgress(int side) const { return m_edge[side ? 1 : 0].Progress(); } // 0..1 work done on one bevel (HUD bar)
    float SegAverage()           const { return m_shape[m_side].Progress(); }       // up face (display / morph preview)

private:
    // How strongly the iron reacts (physics magnitudes; the numbers you tune/defend).
    static constexpr float FLOW_DROP    = 0.095f; // height pushed out of the hit cell (ideal strike)
    static constexpr float DMG_COLD_HIT = 0.35f;  // crack from one cold strike
    static constexpr float DMG_OVER_HIT = 0.25f;  // scorch from one overheated strike

    int   m_side = 0;                 // which face is up right now (0 = front, 1 = back)
    float m_heat = 0.0f;              // temperature 0..1 (one value for the whole piece)
    float m_hStart = 0.17f;           // uniform starting thickness = thickest part of the weapon
    float m_h[NSIDES][NL][NW];        // current height (thickness) field, per face
    float m_hTgt[NL][NW];             // target (finished weapon) height field (same shape for both faces)
    float m_dmgF[NSIDES][NL][NW];     // per-cell damage 0..1 (cold crack / overheat scorch), per face
    WorkField m_shape[NSIDES];        // shaping work left along the length, per face
    WorkField m_edge[NSIDES];         // grinding work left along the length, per bevel
};
