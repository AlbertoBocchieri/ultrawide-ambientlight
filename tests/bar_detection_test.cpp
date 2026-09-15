#include <windows.h>

#include "shaders/barStabilizer.h"
#include "shaders/detectCpu.h"

#include <cassert>

int main()
{
    BarStabilizer bar;
    assert(bar.Update(100) == 0);
    assert(bar.Update(100) == 0);
    assert(bar.Update(100) == 0);
    assert(bar.Update(100) == 100);
    assert(bar.Update(40) == 40);
    assert(bar.Update(80) == 40);
    assert(bar.Update(81) == 40); // A changing candidate restarts confirmation.
    assert(bar.Update(81, 4, false) == 40); // Extra frames cannot accelerate growth.
    assert(bar.Update(82, 4, false) == 40); // Intermediate disagreement resets consensus.
    assert(bar.confirmations == 0);
    assert(bar.Update(20, 4, false) == 20); // Shrink does not wait for the interval.

    UINT flags[101] = {};
    for (int i = 30; i <= 70; ++i)
        flags[i] = 1;
    assert(FindBarSizeCenterOutWithFlags(flags, 50, -1, 0) == 30);
    assert(FindBarSizeCenterOutWithFlags(flags, 50, 1, 100) == 30);

    flags[5] = 1; // Real content near the edge must never be covered.
    assert(FindBarSizeCenterOutWithFlags(flags, 50, -1, 0) == 0);

    float uniformBlack[10] = {};
    assert(isLineMostlyBlack(uniformBlack, 10, 1, 0.0003f, 0.7f, 0.000001f));

    float darkImage[10] = {};
    darkImage[7] = darkImage[8] = darkImage[9] = 0.01f;
    assert(!isLineMostlyBlack(darkImage, 10, 1, 0.0003f, 0.7f, 0.000001f));
}
