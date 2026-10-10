#pragma once
#include <algorithm>
#include <cmath>

namespace pui {

constexpr double kMeterFloorDb = -60.0;

inline double amp_to_db(double amp, double floorDb = kMeterFloorDb) {
    if (!(amp > 0)) return floorDb;
    return std::clamp(20.0 * std::log10(amp), floorDb, 0.0);
}

inline double meter_fill(double db, double floorDb = kMeterFloorDb) {
    return std::clamp((db - floorDb) / -floorDb, 0.0, 1.0);
}

struct MeterBallistics {
    double level = kMeterFloorDb, peak = kMeterFloorDb, hold_left = 0;

    void update(double in_db, double dt, double fall = 24.0, double hold = 1.0,
                double floorDb = kMeterFloorDb) {
        dt = std::max(0.0, dt);
        level = std::max(in_db, level - fall * dt);
        if (level >= peak) { peak = level; hold_left = hold; }
        else if (hold_left > 0) hold_left = std::max(0.0, hold_left - dt);
        else peak = std::max(level, peak - fall * dt);
        level = std::max(level, floorDb);
        peak = std::max(peak, floorDb);
    }
};

}
