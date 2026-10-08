#include "WorkField.h"
#include <cmath>

WorkField::Result WorkField::Apply(float coord, float amount, float spread)
{
    const float MIN_SPREAD = 0.1f;   // guard: sigma 0 would divide by zero
    const float sigma = fmaxf(spread, MIN_SPREAD);
    auto falloff = [&](int i) { const float d = (i + 0.5f) - coord; return expf(-(d * d) / (2.0f * sigma * sigma)); };

    // Wasted = every cell the tool really reaches is already finished. Checking only the
    // cell right under the contact was a trap: an unfinished end cell the tool cannot get
    // right above (tip / tang end) next to a finished cell could never be completed.
    //   "Really reaches" = falloff weight >= WASTE_WEIGHT (0.5 = within ~1.18 sigma).
    const float WASTE_WEIGHT = 0.5f;
    bool reachesUnfinished = false;
    for (int i = 0; i < N && !reachesUnfinished; ++i)
        if (falloff(i) >= WASTE_WEIGHT && m_left[i] > 0.0f) reachesUnfinished = true;
    if (!reachesUnfinished) return Result::AlreadyDone;

    // 1) Share the work among the UNFINISHED cells by falloff weight (normalised, so the
    //    shares sum to the whole amount). A cell never goes below 0.
    float wsum = 0.0f;
    for (int i = 0; i < N; ++i) if (m_left[i] > 0.0f) wsum += falloff(i);
    float taken = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        if (m_left[i] <= 0.0f) continue;
        const float take = fminf(m_left[i], amount * falloff(i) / wsum);
        m_left[i] -= take;
        taken     += take;
    }
    // 2) What full cells could not absorb goes to the nearest unfinished cell, then the next
    //    nearest... so every valid action removes exactly `amount` (until the job is done).
    float rest = amount - taken;
    const float REST_EPS = 1e-6f;
    while (rest > REST_EPS)
    {
        int best = -1; float bestD = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            if (m_left[i] <= 0.0f) continue;
            const float d = fabsf((i + 0.5f) - coord);
            if (best < 0 || d < bestD) { best = i; bestD = d; }
        }
        if (best < 0) break;   // the whole job is done
        const float take = fminf(m_left[best], rest);
        m_left[best] -= take;
        rest         -= take;
    }
    // Snap float dust to exactly 0 so "done" is exact.
    const float DONE_EPS = 1e-4f;
    for (int i = 0; i < N; ++i) if (m_left[i] < DONE_EPS) m_left[i] = 0.0f;
    return Result::Worked;
}

float WorkField::SegProgress(int s, int nseg) const
{
    const int per = N / nseg;
    float sum = 0.0f;
    for (int i = s * per; i < (s + 1) * per; ++i) sum += CellProgress(i);
    return sum / per;
}

bool WorkField::SegDone(int s, int nseg) const
{
    const int per = N / nseg;
    for (int i = s * per; i < (s + 1) * per; ++i) if (!CellDone(i)) return false;
    return true;
}

float WorkField::Progress() const
{
    float left = 0.0f;
    for (int i = 0; i < N; ++i) left += m_left[i];
    return 1.0f - left / N;
}

bool WorkField::Done() const
{
    for (int i = 0; i < N; ++i) if (!CellDone(i)) return false;
    return true;
}
