#include "RideSystem.h"

#include <algorithm>
#include <ctime>

namespace rs {
namespace {

int currentHour() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return tmv.tm_hour;
}

void validateName(const std::string& s, const char* field) {
    if (s.empty() || s.size() > 60) throw ApiError(400, std::string(field) + " must be 1-60 characters");
    for (char c : s)
        if (c == '|' || c == '\n' || c == '\r') throw ApiError(400, std::string(field) + " has invalid characters");
}
void validatePhone(const std::string& p) {
    if (p.size() < 7 || p.size() > 15 || !std::all_of(p.begin(), p.end(), ::isdigit))
        throw ApiError(400, "phone must be 7-15 digits");
}
void validatePassword(const std::string& p) {
    if (p.size() < 6) throw ApiError(400, "password must be at least 6 characters");
}

}  // namespace

RideSystem::RideSystem(Graph graph, const std::string& dataDir, std::function<int()> hourProvider)
    : graph_(std::move(graph)), storage_(dataDir), hour_(hourProvider ? hourProvider : currentHour) {
    storage_.load(data_);
    for (auto& [id, _] : data_.users) nextUserId_ = std::max(nextUserId_, id + 1);
    for (auto& [id, _] : data_.drivers) nextDriverId_ = std::max(nextDriverId_, id + 1);
    for (auto& [id, _] : data_.rides) nextRideId_ = std::max(nextRideId_, id + 1);
    for (auto& p : data_.payments) nextPaymentId_ = std::max(nextPaymentId_, p.id + 1);
}

void RideSystem::persist() { storage_.save(data_); }

User& RideSystem::findUser(int id) {
    auto it = data_.users.find(id);
    if (it == data_.users.end()) throw ApiError(404, "User not found");
    return it->second;
}
Driver& RideSystem::findDriver(int id) {
    auto it = data_.drivers.find(id);
    if (it == data_.drivers.end()) throw ApiError(404, "Driver not found");
    return it->second;
}
Ride& RideSystem::findRide(int id) {
    auto it = data_.rides.find(id);
    if (it == data_.rides.end()) throw ApiError(404, "Ride not found");
    return it->second;
}

void RideSystem::addPayment(int rideId, int userId, int driverId, double amount, const std::string& type) {
    Payment p;
    p.id = nextPaymentId_++;
    p.rideId = rideId;
    p.userId = userId;
    p.driverId = driverId;
    p.amount = amount;
    p.type = type;
    p.timestamp = (long)std::time(nullptr);
    data_.payments.push_back(p);
}

// ---------------- registration / login / wallet ----------------

User RideSystem::registerUser(const std::string& name, const std::string& phone, const std::string& gender,
                              const std::string& password) {
    validateName(name, "name");
    validatePhone(phone);
    validatePassword(password);
    Gender g = parseGender(gender);
    std::lock_guard<std::mutex> lock(m_);
    for (auto& [id, u] : data_.users)
        if (u.phone() == phone) throw ApiError(409, "Phone already registered");
    User u(nextUserId_++, name, phone, g, Person::hashPassword(password));
    data_.users.emplace(u.id(), u);
    persist();
    return u;
}

Driver RideSystem::registerDriver(const std::string& name, const std::string& phone, const std::string& gender,
                                  const std::string& password, const std::string& vehicle,
                                  const std::string& locationName) {
    validateName(name, "name");
    validateName(vehicle, "vehicle");
    validatePhone(phone);
    validatePassword(password);
    Gender g = parseGender(gender);
    int loc = graph_.nodeId(locationName);
    if (loc < 0) throw ApiError(400, "Unknown location: " + locationName);
    std::lock_guard<std::mutex> lock(m_);
    for (auto& [id, d] : data_.drivers)
        if (d.phone() == phone) throw ApiError(409, "Phone already registered");
    Driver d(nextDriverId_++, name, phone, g, Person::hashPassword(password), vehicle, loc);
    data_.drivers.emplace(d.id(), d);
    persist();
    return d;
}

int RideSystem::login(const std::string& role, const std::string& phone, const std::string& password) {
    std::lock_guard<std::mutex> lock(m_);
    if (role == "user") {
        for (auto& [id, u] : data_.users)
            if (u.phone() == phone && u.checkPassword(password)) return id;
    } else if (role == "driver") {
        for (auto& [id, d] : data_.drivers)
            if (d.phone() == phone && d.checkPassword(password)) return id;
    } else {
        throw ApiError(400, "role must be user or driver");
    }
    throw ApiError(401, "Invalid credentials");
}

User RideSystem::getUser(int id) {
    std::lock_guard<std::mutex> lock(m_);
    return findUser(id);
}

double RideSystem::addMoney(int userId, double amount) {
    if (!(amount > 0) || amount > 100000) throw ApiError(400, "amount must be between 0 and 100000");
    std::lock_guard<std::mutex> lock(m_);
    User& u = findUser(userId);
    u.wallet().deposit(amount);
    persist();
    return u.wallet().balance();
}

// ---------------- ride lifecycle ----------------

BookingResult RideSystem::bookRide(int userId, const std::string& source, const std::string& destination,
                                   const std::string& rideType, bool femaleOnly) {
    if (rideType != "normal" && rideType != "premium") throw ApiError(400, "rideType must be normal or premium");
    int src = graph_.nodeId(source), dst = graph_.nodeId(destination);
    if (src < 0) throw ApiError(400, "Unknown source: " + source);
    if (dst < 0) throw ApiError(400, "Unknown destination: " + destination);
    if (src == dst) throw ApiError(400, "Source and destination are the same");

    std::lock_guard<std::mutex> lock(m_);  // lock covers match+assign: no double-booking a driver
    User& user = findUser(userId);

    // Female-only safety rule: only female riders may request it, and only female drivers are matched.
    if (femaleOnly && user.gender() != Gender::Female)
        throw ApiError(403, "Female-only rides are available to female riders only");

    Graph::Path trip = graph_.shortestPath(src, dst);
    if (!trip.found()) throw ApiError(400, "No route between source and destination");

    // One Dijkstra from the pickup gives the distance to every driver location at once.
    Graph::Result fromPickup = graph_.dijkstra(src);
    Driver* best = nullptr;
    for (auto& [id, d] : data_.drivers) {
        if (!d.available()) continue;
        if (femaleOnly && d.gender() != Gender::Female) continue;
        double dist = fromPickup.dist[d.location()];
        if (dist == Graph::INF) continue;
        if (!best || dist < fromPickup.dist[best->location()] ||
            (dist == fromPickup.dist[best->location()] && d.id() < best->id()))
            best = &d;
    }
    if (!best) throw ApiError(404, femaleOnly ? "No female driver available" : "No driver available");

    int hour = hour_();
    double eta = EtaCalculator::etaMinutes(trip.distanceKm, hour);
    auto strategy = FareFactory::create(rideType, hour);
    double fare = strategy->calculate(trip.distanceKm, eta);

    user.wallet().deduct(fare);  // throws 402 if insufficient; nothing else mutated yet

    Ride r;
    r.id = nextRideId_++;
    r.userId = userId;
    r.driverId = best->id();
    r.source = src;
    r.destination = dst;
    r.route = trip.nodes;
    r.distanceKm = round2(trip.distanceKm);
    r.etaMin = round2(eta);
    r.fare = fare;
    r.fareType = strategy->name();
    r.femaleOnly = femaleOnly;
    r.status = RideStatus::Accepted;
    r.createdAt = (long)std::time(nullptr);
    data_.rides.emplace(r.id, r);

    best->setAvailable(false);
    addPayment(r.id, userId, best->id(), fare, "HOLD");
    persist();

    double pickupKm = fromPickup.dist[best->location()];
    return BookingResult{r, *best, round2(pickupKm), round2(EtaCalculator::etaMinutes(pickupKm, hour))};
}

Ride RideSystem::startRide(int rideId) {
    std::lock_guard<std::mutex> lock(m_);
    Ride& r = findRide(rideId);
    if (r.status != RideStatus::Accepted) throw ApiError(409, "Only an ACCEPTED ride can be started");
    r.status = RideStatus::Ongoing;
    persist();
    return r;
}

Ride RideSystem::completeRide(int rideId) {
    std::lock_guard<std::mutex> lock(m_);
    Ride& r = findRide(rideId);
    if (r.status != RideStatus::Ongoing) throw ApiError(409, "Only an ONGOING ride can be completed");
    Driver& d = findDriver(r.driverId);
    double driverShare = round2(r.fare * (1.0 - kCommission));
    double commission = round2(r.fare - driverShare);
    d.wallet().deposit(driverShare);
    d.setLocation(r.destination);
    d.setAvailable(true);
    d.setRidesCompleted(d.ridesCompleted() + 1);
    r.status = RideStatus::Completed;
    addPayment(r.id, r.userId, r.driverId, driverShare, "PAYOUT");
    addPayment(r.id, r.userId, r.driverId, commission, "COMMISSION");
    persist();
    return r;
}

Ride RideSystem::cancelRide(int rideId) {
    std::lock_guard<std::mutex> lock(m_);
    Ride& r = findRide(rideId);
    if (r.status != RideStatus::Requested && r.status != RideStatus::Accepted)
        throw ApiError(409, "Only REQUESTED/ACCEPTED rides can be cancelled");
    findUser(r.userId).wallet().deposit(r.fare);  // release the hold
    findDriver(r.driverId).setAvailable(true);
    r.status = RideStatus::Cancelled;
    addPayment(r.id, r.userId, r.driverId, r.fare, "REFUND");
    persist();
    return r;
}

Ride RideSystem::getRide(int rideId) {
    std::lock_guard<std::mutex> lock(m_);
    return findRide(rideId);
}

Report RideSystem::report() {
    std::lock_guard<std::mutex> lock(m_);
    Report rep;
    rep.totalUsers = (int)data_.users.size();
    rep.totalDrivers = (int)data_.drivers.size();
    for (auto& [id, d] : data_.drivers)
        if (d.available()) ++rep.availableDrivers;
    rep.totalRides = (int)data_.rides.size();
    for (auto& [id, r] : data_.rides) {
        if (r.status == RideStatus::Completed) { ++rep.completedRides; rep.totalFareCollected += r.fare; }
        else if (r.status == RideStatus::Cancelled) ++rep.cancelledRides;
        else ++rep.activeRides;
    }
    for (auto& p : data_.payments) {
        if (p.type == "COMMISSION") rep.platformRevenue += p.amount;
        if (p.type == "PAYOUT") rep.driverPayouts += p.amount;
    }
    rep.totalFareCollected = round2(rep.totalFareCollected);
    rep.platformRevenue = round2(rep.platformRevenue);
    rep.driverPayouts = round2(rep.driverPayouts);
    return rep;
}

}  // namespace rs
