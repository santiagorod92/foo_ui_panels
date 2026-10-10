#include "test.h"
#include "core/meter_math.h"

using namespace pui;

static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

TEST("amp_to_db / meter_fill") {
    CHECK(near(amp_to_db(1.0), 0.0));
    CHECK(near(amp_to_db(0.5), 20.0 * std::log10(0.5)));
    CHECK(near(amp_to_db(0.0), kMeterFloorDb));
    CHECK(near(amp_to_db(-1.0), kMeterFloorDb));
    CHECK(near(amp_to_db(4.0), 0.0));
    CHECK(near(amp_to_db(1e-9), kMeterFloorDb));
    CHECK(near(meter_fill(0.0), 1.0));
    CHECK(near(meter_fill(-30.0), 0.5));
    CHECK(near(meter_fill(-90.0), 0.0));
}

TEST("MeterBallistics: instant attack, steady fall") {
    MeterBallistics m;
    m.update(-6.0, 0.016);
    CHECK(near(m.level, -6.0));
    m.update(-60.0, 0.5, 24.0);
    CHECK(near(m.level, -18.0));
    m.update(-60.0, 10.0, 24.0);
    CHECK(near(m.level, kMeterFloorDb));
}

TEST("MeterBallistics: peak holds, then falls; a new high re-arms it") {
    MeterBallistics m;
    m.update(-3.0, 0.0);
    m.update(-60.0, 0.5, 24.0, 1.0);
    CHECK(near(m.peak, -3.0));
    m.update(-60.0, 0.6, 24.0, 1.0);
    CHECK(near(m.peak, -3.0));
    m.update(-60.0, 0.5, 24.0, 1.0);
    CHECK(near(m.peak, -15.0));
    m.update(-1.0, 0.016, 24.0, 1.0);
    CHECK(near(m.peak, -1.0) && near(m.hold_left, 1.0));
}
