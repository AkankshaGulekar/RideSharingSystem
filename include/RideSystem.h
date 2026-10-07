#pragma once
// Service layer: all business rules live here (no HTTP / Crow code).
#include <functional>
#include <mutex>
#include <string>

#include "Eta.h"
#include "Fare.h"
#include "Graph.h"
#include "Storage.h"
#include "models.h"

namespace rs {

struct BookingResult {
    Ride ride;
    Driver driver;
    double pickupDistanceKm;
    double pickupEtaMin;
};

struct Report {
    int totalUsers = 0, totalDrivers = 0, availableDrivers = 0;
    int totalRides = 0, activeRides = 0, completedRides = 0, cancelledRides = 0;
    double totalFareCollected = 0;  // fares of completed rides
    double platformRevenue = 0;     // commission
    double driverPayouts = 0;
};

class RideSystem {
public:
    static constexpr double kCommission = 0.20;  // platform keeps 20%

    // hourProvider is injectable so tests can fix the "current hour" (surge/ETA logic).
    RideSystem(Graph graph, const std::string& dataDir, std::function<int()> hourProvider = nullptr);

    const Graph& graph() const { return graph_; }  // immutable after construction

    User registerUser(const std::string& name, const std::string& phone, const std::string& gender,
                      const std::string& password);
    Driver registerDriver(const std::string& name, const std::string& phone, const std::string& gender,
                          const std::string& password, const std::string& vehicle,
                          const std::string& locationName);
    int login(const std::string& role, const std::string& phone, const std::string& password);

    User getUser(int id);
    double addMoney(int userId, double amount);  // returns new balance

    BookingResult bookRide(int userId, const std::string& source, const std::string& destination,
                           const std::string& rideType, bool femaleOnly);
    Ride startRide(int rideId);
    Ride completeRide(int rideId);
    Ride cancelRide(int rideId);
    Ride getRide(int rideId);

    Report report();

private:
    Graph graph_;
    Storage storage_;
    Data data_;
    std::function<int()> hour_;
    std::mutex m_;  // one coarse lock: Crow runs handlers on multiple threads
    int nextUserId_ = 1, nextDriverId_ = 1, nextRideId_ = 1, nextPaymentId_ = 1;

    void persist();  // caller must hold m_
    void addPayment(int rideId, int userId, int driverId, double amount, const std::string& type);
    User& findUser(int id);
    Driver& findDriver(int id);
    Ride& findRide(int id);
};

}  // namespace rs
