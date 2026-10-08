#pragma once

// Remaining work along the length of the blade (shared by forging and grinding).
//
// The job (shaping one face / grinding one bevel) starts with 100% of its work left,
// spread over N length cells (1 per cell). Work is only ever SUBTRACTED, by a fixed
// amount per valid action decided by the caller, so the job always ends after a known
// amount of valid work -- it cannot stop at 99%. How well the action was done (grade)
// is the caller's business (score), never the amount of work.
//
// Where the work is taken from is local: mostly the cells around the contact point
// (Gaussian falloff), and what finished cells cannot absorb goes to the nearest
// unfinished cells. So the metal changes where the tool touched it.
//
// Coordinates: coord is the contact position along the blade in cells (0..N,
// continuous), in the weapon's local-length convention (AimSystem::SegCoordLocal).
class WorkField
{
public:
    static const int N = 20;   // length cells

    enum class Result { Worked, AlreadyDone };

    void Reset()    { for (int i = 0; i < N; ++i) m_left[i] = 1.0f; }  // all work still to do
    void Complete() { for (int i = 0; i < N; ++i) m_left[i] = 0.0f; }  // debug: job already done

    // Take `amount` cell-units of work around `coord`. spread = Gaussian sigma in cells.
    // AlreadyDone (nothing taken) when no unfinished cell is really within reach.
    Result Apply(float coord, float amount, float spread);

    float CellProgress(int i) const { return 1.0f - m_left[i]; }   // 0..1
    bool  CellDone(int i)     const { return m_left[i] <= 0.0f; }  // exact: work is floored at 0
    float SegProgress(int s, int nseg) const;                      // mean of the segment's cells
    bool  SegDone(int s, int nseg)     const;                      // every cell of the segment done
    float Progress() const;                                        // work done / total work, 0..1
    bool  Done()     const;                                        // no work left anywhere

private:
    float m_left[N] = {};   // work left per cell, 1 (untouched) .. 0 (finished)
};
