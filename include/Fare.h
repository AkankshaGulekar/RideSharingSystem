#pragma once
// Strategy pattern: interchangeable fare algorithms + a Factory choosing one.
#include <cmath>
#include <memory>
#include <string>

namespace rs {

inline double round2(double x) { return std::round(x * 100.0) / 100.0; }

class FareStrategy {
public:
    virtual ~FareStrategy() = default;
    virtual double calculate(double distanceKm, double durationMin) const = 0;
    virtual std::string name() const = 0;
};

class NormalFare : public FareStrategy {
public:
    double calculate(double km, double min) const override { return round2(40 + 12 * km + 1.5 * min); }
    std::string name() const override { return "NORMAL"; }
};

class SurgeFare : public FareStrategy {
    double multiplier_;
public:
    explicit SurgeFare(double m) : multiplier_(m) {}
    double calculate(double km, double min) const override {
        return round2(multiplier_ * (40 + 12 * km + 1.5 * min));
    }
    std::string name() const override { return "SURGE"; }
};

class PremiumFare : public FareStrategy {
public:
    double calculate(double km, double min) const override { return round2(60 + 18 * km + 2.0 * min); }
    std::string name() const override { return "PREMIUM"; }
};

struct FareFactory {
    static bool isPeak(int hour) { return (hour >= 8 && hour < 11) || (hour >= 17 && hour < 21); }
    // premium requested -> PremiumFare; peak hours -> SurgeFare(1.5); otherwise NormalFare
    static std::unique_ptr<FareStrategy> create(const std::string& rideType, int hour) {
        if (rideType == "premium") return std::make_unique<PremiumFare>();
        if (isPeak(hour)) return std::make_unique<SurgeFare>(1.5);
        return std::make_unique<NormalFare>();
    }
};

}  // namespace rs
