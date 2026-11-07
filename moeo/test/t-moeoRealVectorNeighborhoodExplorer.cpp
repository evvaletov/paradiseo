#include <eo>
#include <moeo>
#include <neighborhood/moRealVectorNeighbor.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
struct Traits : moeoObjectiveVectorTraits {
    static unsigned nObjectives() { return 2; }
    static bool minimizing(unsigned) { return true; }
    static bool maximizing(unsigned) { return false; }
};
using Objective = moeoRealObjectiveVector<Traits>;
using Individual = moeoRealVector<Objective>;
using Neighbor = moRealVectorNeighbor<Individual>;

struct Evaluate : eoEvalFunc<Individual> {
    explicit Evaluate(bool turn = false) : turning(turn) {}
    bool turning;
    unsigned calls = 0;
    void operator()(Individual& x) override {
        ++calls;
        const double square = x[0] * x[0];
        Objective value;
        value[0] = value[1] = turning ? -square / (1. + square * square / .0016) : -square;
        x.objectiveVector(value);
    }
};

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void checkScaling(unsigned minimum, unsigned maximum, unsigned expected,
                  bool turning = false, bool perVisitMinimum = false)
{
    eoPop<Individual> source;
    source.resize(1);
    source[0].resize(1);
    source[0][0] = 0.;
    source[0].flag(0);
    Evaluate eval(turning);
    eval(source[0]);
    eoRealVectorBounds bounds(1, -10., 10.);
    eoPop<Individual> dest;
    if (perVisitMinimum) {
        RealVectorNeighborhoodExplorer<Neighbor> explorer(
            eval, bounds, .01, 1, false, std::vector<unsigned>{minimum}, maximum, 4);
        explorer(source, {0}, dest);
    } else {
        RealVectorNeighborhoodExplorer<Neighbor> explorer(
            eval, bounds, .01, 1, false, minimum, maximum, 4);
        explorer(source, {0}, dest);
    }

    // In one dimension both possible directions have the same objective.
    require(eval.calls == expected + 1, "unexpected number of evaluated candidates");
    require(dest.size() == expected, "an improving candidate was discarded");
    require(source[0][0] == 0. && source[0].flag() == 1, "source was not preserved/marked visited");
    double distance = .2;
    for (const auto& x : dest) {
        require(std::abs(std::abs(x[0]) - distance) < 1e-10, "wrong scaled position");
        require(bounds.isInBounds(x), "stored candidate is out of bounds");
        require(x.objectiveVector()[0] < 0. && x.flag() == 0, "invalid stored candidate");
        distance *= 4.;
    }
}
}

int main()
{
    try {
        checkScaling(0, 0, 1); // Initial neighbor only.
        checkScaling(0, 1, 2); // Final allowed scaling must be retained.
        checkScaling(0, 2, 3); // Continue while each scaled candidate improves.
        checkScaling(0, 4, 3); // Next step would exceed the bounds.
        checkScaling(0, 4, 2, true); // Stop on deterioration, even with minimum zero.
        checkScaling(2, 4, 3, true); // Honor a minimum before stopping.
        checkScaling(0, 1, 2, false, true);
        checkScaling(0, 4, 2, true, true);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
