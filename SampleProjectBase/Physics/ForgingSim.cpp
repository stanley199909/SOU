#include "ForgingSim.h"
#include <cmath>

void ForgingSim::Reset()
{
    // Start as a uniform thick billet (progress 0) with no damage.
    for (int i = 0; i < NL; ++i)
    for (int j = 0; j < NW; ++j) { m_h[i][j] = m_hStart; m_dmgF[i][j] = 0.0f; }
    for (int s = 0; s < NSEG; ++s) m_segProg[s] = 0.0f;
    BuildTarget();
}

// Define the finished weapon's target height field (a short sword).
//   i = length (0 = tang .. tip), j = width (centre = ridge). Inside the weapon
//   is high (the ridge is highest); outside is flash that is beaten down to ~0.
//   Beating the surrounding cells down makes the tall region (the weapon) stand out.
void ForgingSim::BuildTarget()
{
    const float flashH = 0.012f; // very thin height of the flash (waste beaten off)
    for (int i = 0; i < NL; ++i)
    {
        float u = (i + 0.5f) / NL; // cell centre 0 = tang .. 1 = tip

        // Fraction of the half-width the weapon occupies here, and the ridge height.
        float wfrac, ridgeH;
        if (u < 0.20f)          // tang (grip core): narrow / thick
        {
            wfrac  = 0.34f;
            ridgeH = 0.90f;
        }
        else if (u < 0.30f)     // guard / blade root (shoulder): widest
        {
            float v = (u - 0.20f) / 0.10f;
            wfrac  = 0.34f + (0.95f - 0.34f) * v;
            ridgeH = 0.90f + (0.72f - 0.90f) * v;
        }
        else                    // blade: width and height taper toward the tip
        {
            float v = (u - 0.30f) / 0.70f;
            wfrac  = 0.95f + (0.05f - 0.95f) * v; // wide root -> pointed tip
            ridgeH = 0.72f + (0.14f - 0.72f) * v; // thick root -> thin tip
        }

        for (int j = 0; j < NW; ++j)
        {
            float cv = fabsf((j + 0.5f) / NW - 0.5f) * 2.0f; // 0 = centre .. 1 = edge
            float h;
            if (cv <= wfrac)
            {
                // Inside the weapon: ridge (centre) high, thinning toward the edge.
                float e = cv / (wfrac > 1e-4f ? wfrac : 1.0f); // 0 = ridge .. 1 = edge
                float bevel = 1.0f - 0.75f * e;                // ridge 1.0 -> edge 0.25
                h = ridgeH * bevel;
            }
            else h = flashH; // outside the weapon = flash

            m_hTgt[i][j] = h * m_hStart; // scale ratio into real thickness
        }
    }

    // Volume conservation: match the target's total volume to the billet's
    // (all cells at hStart), so the target is reachable by redistribution alone.
    float sumT = 0.0f;
    for (int i = 0; i < NL; ++i)
    for (int j = 0; j < NW; ++j) sumT += m_hTgt[i][j];
    float sumS = (float)(NL * NW) * m_hStart;
    if (sumT > 1e-6f)
    {
        float k = sumS / sumT;
        for (int i = 0; i < NL; ++i)
        for (int j = 0; j < NW; ++j) m_hTgt[i][j] *= k;
    }
}

// Shape match 0..1: smaller L1 error between current and target height = closer to 1.
float ForgingSim::ShapeMatch() const
{
    float err = 0.0f;
    for (int i = 0; i < NL; ++i)
    for (int j = 0; j < NW; ++j) err += fabsf(m_h[i][j] - m_hTgt[i][j]);
    // Reference: the error of the flat starting billet. We move from there toward 0.
    float ref = 0.0f;
    for (int i = 0; i < NL; ++i)
    for (int j = 0; j < NW; ++j) ref += fabsf(m_hStart - m_hTgt[i][j]);
    if (ref < 1e-6f) return 1.0f;
    float m = 1.0f - err / ref;
    if (m < 0.0f) m = 0.0f; if (m > 1.0f) m = 1.0f;
    return m;
}

ForgingSim::StrikeOutcome ForgingSim::ApplyStrike(int ci, int cj, int seg,
    float power, float heatFactor, float grooveMult, bool cold, bool over)
{
    // Guided flow (volume-conserving, outcome-locked): the hit cell only drops
    // toward its target (never below), and the displaced material flows to the
    // neighbours the design wants more material at (smaller surplus e = h - target).
    float want = FLOW_DROP * power * heatFactor * grooveMult;
    float eC = m_h[ci][cj] - m_hTgt[ci][cj]; // surplus at the hit cell
    float delta = want;
    if (delta > eC)   delta = eC;   // don't go below target (result can't be ruined)
    if (delta < 0.0f) delta = 0.0f; // already at/below target = nothing to shave

    if (delta > 0.0f)
    {
        int ni[4], nj[4], n = 0; // in-bounds 4-neighbours (edges keep material on the plate)
        if (ci > 0)      { ni[n] = ci - 1; nj[n] = cj;     ++n; }
        if (ci < NL - 1) { ni[n] = ci + 1; nj[n] = cj;     ++n; }
        if (cj > 0)      { ni[n] = ci;     nj[n] = cj - 1; ++n; }
        if (cj < NW - 1) { ni[n] = ci;     nj[n] = cj + 1; ++n; }
        if (n > 0)
        {
            // Weight each neighbour by max(0, eC - eK): neediest neighbour gets most.
            float w[4], wsum = 0.0f;
            for (int k = 0; k < n; ++k)
            {
                float eK = m_h[ni[k]][nj[k]] - m_hTgt[ni[k]][nj[k]];
                float ww = eC - eK;
                if (ww < 0.0f) ww = 0.0f;
                w[k] = ww;
                wsum += ww;
            }
            if (wsum < 1e-6f) { for (int k = 0; k < n; ++k) w[k] = 1.0f; wsum = (float)n; } // all surplus -> even
            m_h[ci][cj] -= delta;                    // hit cell moves toward its target
            for (int k = 0; k < n; ++k)              // displaced material flows to needy neighbours
                m_h[ni[k]][nj[k]] += delta * (w[k] / wsum);
        }
    }

    // Cold = crack, overheat = scorch: mark the damage and report the bad strike.
    if (cold)
    {
        m_dmgF[ci][cj] += DMG_COLD_HIT;
        if (m_dmgF[ci][cj] > 1.0f) m_dmgF[ci][cj] = 1.0f;
        return StrikeOutcome::ColdHit;
    }
    if (over)
    {
        m_dmgF[ci][cj] += DMG_OVER_HIT;
        if (m_dmgF[ci][cj] > 1.0f) m_dmgF[ci][cj] = 1.0f;
        return StrikeOutcome::OverHit;
    }
    if (SegDone(seg)) return StrikeOutcome::AlreadyDone; // striking a finished segment wastes it

    // Good strike on an unfinished segment: advance its shaping (forward only).
    //   FORGE_STEP is "how much of the whole progresses"; a segment is 1/NSEG of the
    //   length, so scale by NSEG to keep hits-per-segment near the old whole-bar count.
    m_segProg[seg] += FORGE_STEP * NSEG * power * grooveMult;
    if (m_segProg[seg] > 1.0f) m_segProg[seg] = 1.0f;
    return StrikeOutcome::Shaped;
}

void ForgingSim::BurnAll(float amount)
{
    for (int i = 0; i < NL; ++i)
    for (int j = 0; j < NW; ++j)
    {
        m_dmgF[i][j] += amount;
        if (m_dmgF[i][j] > 1.0f) m_dmgF[i][j] = 1.0f;
    }
}

bool ForgingSim::AllSegmentsDone() const
{
    for (int s = 0; s < NSEG; ++s) if (m_segProg[s] < SEG_DONE) return false;
    return true;
}

float ForgingSim::SegAverage() const
{
    float sum = 0.0f;
    for (int s = 0; s < NSEG; ++s) sum += m_segProg[s];
    return sum / NSEG;
}
