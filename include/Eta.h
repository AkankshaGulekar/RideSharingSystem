#pragma once
// Heuristic ETA: time = distance / (baseSpeed * trafficFactor(hour)). Not machine learning.
namespace rs {

struct EtaCalculator {
    static constexpr double kBaseSpeedKmph = 30.0;

    static double trafficFactor(int hour) {
        if ((hour >= 8 && hour < 11) || (hour >= 17 && hour < 21)) return 0.6;  // rush hour
        if (hour >= 22 || hour < 5) return 1.2;                                  // night, free roads
        return 1.0;
    }
    static double etaMinutes(double km, int hour) {
        return km / (kBaseSpeedKmph * trafficFactor(hour)) * 60.0;
    }
};

}  // namespace rs
