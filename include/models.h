#pragma once
// Domain models: Wallet, Person -> User/Driver, Ride, Payment (+ ApiError, enums)
#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace rs {

// Exception carrying an HTTP status; the REST layer maps it to a response.
struct ApiError : std::runtime_error {
    int status;
    ApiError(int s, const std::string& msg) : std::runtime_error(msg), status(s) {}
};

enum class Gender { Male, Female, Other };

inline std::string toString(Gender g) {
    switch (g) {
        case Gender::Male: return "male";
        case Gender::Female: return "female";
        default: return "other";
    }
}
inline Gender parseGender(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    if (s == "male") return Gender::Male;
    if (s == "female") return Gender::Female;
    if (s == "other") return Gender::Other;
    throw ApiError(400, "gender must be male, female or other");
}

// ---------- Wallet (encapsulation: balance only changes via validated methods) ----------
class Wallet {
    double balance_ = 0.0;
public:
    double balance() const { return balance_; }
    void deposit(double amount) {
        if (!(amount > 0)) throw ApiError(400, "Amount must be positive");
        balance_ += amount;
    }
    void deduct(double amount) {
        if (!(amount > 0)) throw ApiError(400, "Amount must be positive");
        if (amount > balance_ + 1e-9) throw ApiError(402, "Insufficient wallet balance");
        balance_ -= amount;
    }
    void restore(double b) { balance_ = b; }  // used only when loading from disk
};

// ---------- Person hierarchy (inheritance + polymorphism) ----------
class Person {
protected:
    int id_;
    std::string name_, phone_;
    Gender gender_;
    std::size_t pwHash_;
    Wallet wallet_;
public:
    Person(int id, std::string name, std::string phone, Gender g, std::size_t pwHash)
        : id_(id), name_(std::move(name)), phone_(std::move(phone)), gender_(g), pwHash_(pwHash) {}
    virtual ~Person() = default;  // virtual: safe deletion via base pointer
    virtual std::string role() const = 0;

    // NOTE: std::hash is NOT secure. Production: bcrypt/argon2 with salt.
    static std::size_t hashPassword(const std::string& p) { return std::hash<std::string>{}(p); }
    bool checkPassword(const std::string& p) const { return hashPassword(p) == pwHash_; }

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& phone() const { return phone_; }
    Gender gender() const { return gender_; }
    std::size_t passwordHash() const { return pwHash_; }
    Wallet& wallet() { return wallet_; }
    const Wallet& wallet() const { return wallet_; }
};

class User : public Person {
public:
    using Person::Person;
    std::string role() const override { return "user"; }
};

class Driver : public Person {
    std::string vehicle_;
    int location_;
    bool available_ = true;
    double rating_ = 5.0;
    int ridesCompleted_ = 0;
public:
    Driver(int id, std::string name, std::string phone, Gender g, std::size_t pw,
           std::string vehicle, int location)
        : Person(id, std::move(name), std::move(phone), g, pw),
          vehicle_(std::move(vehicle)), location_(location) {}
    std::string role() const override { return "driver"; }

    const std::string& vehicle() const { return vehicle_; }
    int location() const { return location_; }
    bool available() const { return available_; }
    double rating() const { return rating_; }
    int ridesCompleted() const { return ridesCompleted_; }
    void setLocation(int l) { location_ = l; }
    void setAvailable(bool a) { available_ = a; }
    void setRating(double r) { rating_ = r; }
    void setRidesCompleted(int n) { ridesCompleted_ = n; }
};

// ---------- Ride & Payment ----------
enum class RideStatus { Requested, Accepted, Ongoing, Completed, Cancelled };

inline std::string toString(RideStatus s) {
    switch (s) {
        case RideStatus::Requested: return "REQUESTED";
        case RideStatus::Accepted: return "ACCEPTED";
        case RideStatus::Ongoing: return "ONGOING";
        case RideStatus::Completed: return "COMPLETED";
        default: return "CANCELLED";
    }
}
inline RideStatus parseStatus(const std::string& s) {
    for (auto st : {RideStatus::Requested, RideStatus::Accepted, RideStatus::Ongoing,
                    RideStatus::Completed, RideStatus::Cancelled})
        if (toString(st) == s) return st;
    throw std::runtime_error("Unknown ride status: " + s);
}

struct Ride {
    int id = 0, userId = 0, driverId = 0, source = 0, destination = 0;
    std::vector<int> route;
    double distanceKm = 0, etaMin = 0, fare = 0;
    std::string fareType;
    bool femaleOnly = false;
    RideStatus status = RideStatus::Requested;
    long createdAt = 0;
};

// type: HOLD (rider charged at booking), REFUND, PAYOUT (driver share), COMMISSION (platform share)
struct Payment {
    int id = 0, rideId = 0, userId = 0, driverId = 0;
    double amount = 0;
    std::string type;
    long timestamp = 0;
};

}  // namespace rs
