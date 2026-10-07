// Dependency-free tests for the core (no Crow needed). Run: ctest or ./rs_tests
#include <cmath>
#include <filesystem>
#include <iostream>

#include "RideSystem.h"

using namespace rs;
static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)
#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 1e-6)
#define CHECK_STATUS(expr, code) do { ++checks; try { expr; ++failures; std::cerr << "FAIL line " << __LINE__ << ": no throw\n"; } \
    catch (const ApiError& e) { if (e.status != (code)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": got " << e.status << "\n"; } } } while (0)

static Graph city() {
    Graph g;
    int a = g.addNode("A"), b = g.addNode("B"), c = g.addNode("C"), d = g.addNode("D"), e = g.addNode("E");
    g.addNode("Island");  // disconnected
    g.addEdge(a, b, 4); g.addEdge(a, c, 1); g.addEdge(c, b, 2); g.addEdge(b, d, 5); g.addEdge(c, d, 8); g.addEdge(d, e, 3);
    return g;
}

static void testDijkstra() {
    Graph g = city();
    auto p = g.shortestPath(g.nodeId("A"), g.nodeId("D"));  // A-C-B-D = 8
    CHECK(p.found()); CHECK_NEAR(p.distanceKm, 8.0);
    CHECK((p.nodes == std::vector<int>{0, 2, 1, 3}));
    CHECK_NEAR(g.shortestPath(0, 4).distanceKm, 11.0);
    CHECK(!g.shortestPath(0, g.nodeId("Island")).found());
    CHECK_NEAR(g.shortestPath(0, 0).distanceKm, 0.0);
    bool threw = false;
    try { g.addEdge(0, 1, -1); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

static void testFareEta() {
    CHECK_NEAR(NormalFare().calculate(10, 20), 40 + 120 + 30);
    CHECK_NEAR(SurgeFare(1.5).calculate(10, 20), 1.5 * 190);
    CHECK_NEAR(PremiumFare().calculate(10, 20), 60 + 180 + 40);
    CHECK(FareFactory::create("normal", 12)->name() == "NORMAL");
    CHECK(FareFactory::create("normal", 18)->name() == "SURGE");
    CHECK(FareFactory::create("premium", 18)->name() == "PREMIUM");
    CHECK_NEAR(EtaCalculator::etaMinutes(15, 12), 30.0);
    CHECK_NEAR(EtaCalculator::etaMinutes(15, 9), 50.0);
}

static void testWallet() {
    Wallet w;
    w.deposit(100);
    CHECK_STATUS(w.deposit(-5), 400);
    CHECK_STATUS(w.deduct(150), 402);
    w.deduct(40);
    CHECK_NEAR(w.balance(), 60.0);
}

static void testSystem() {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "rs_test_store";
    fs::remove_all(dir);
    int hour = 12;
    {
        RideSystem sys(city(), dir.string(), [&] { return hour; });
        User alice = sys.registerUser("Alice", "9000000001", "female", "secret1");
        User bob = sys.registerUser("Bob", "9000000002", "male", "secret2");
        CHECK_STATUS(sys.registerUser("Dup", "9000000001", "male", "secret3"), 409);
        CHECK_STATUS(sys.registerUser("X", "abc", "male", "secret3"), 400);
        Driver dm = sys.registerDriver("Dave", "9100000001", "male", "secret3", "Swift", "A");   // closest to A
        Driver df = sys.registerDriver("Fiona", "9100000002", "female", "secret4", "WagonR", "D");
        CHECK(sys.login("user", "9000000001", "secret1") == alice.id());
        CHECK_STATUS(sys.login("user", "9000000001", "wrong"), 401);

        // insufficient funds -> no state change
        CHECK_STATUS(sys.bookRide(alice.id(), "A", "E", "normal", false), 402);
        sys.addMoney(alice.id(), 2000);
        sys.addMoney(bob.id(), 2000);

        // female-only rules
        CHECK_STATUS(sys.bookRide(bob.id(), "A", "B", "normal", true), 403);
        auto fb = sys.bookRide(alice.id(), "A", "B", "normal", true);
        CHECK(fb.driver.id() == df.id());  // only the female driver qualifies, even though she is farther
        CHECK(fb.ride.status == RideStatus::Accepted);
        CHECK_NEAR(fb.ride.distanceKm, 3.0);
        sys.cancelRide(fb.ride.id);
        CHECK_NEAR(sys.getUser(alice.id()).wallet().balance(), 2000.0);  // refunded

        // nearest-driver matching (Bob normal ride, midday -> NORMAL fare)
        auto b1 = sys.bookRide(bob.id(), "A", "D", "normal", false);
        CHECK(b1.driver.id() == dm.id());
        CHECK(b1.ride.fareType == "NORMAL");
        double fare = round2(NormalFare().calculate(8.0, EtaCalculator::etaMinutes(8.0, 12)));
        CHECK_NEAR(b1.ride.fare, fare);
        CHECK_NEAR(sys.getUser(bob.id()).wallet().balance(), 2000.0 - fare);

        // driver busy -> next booking goes to the other driver
        auto b2 = sys.bookRide(alice.id(), "A", "B", "normal", false);
        CHECK(b2.driver.id() == df.id());
        CHECK_STATUS(sys.bookRide(alice.id(), "A", "B", "normal", false), 404);  // none left

        // state machine
        CHECK_STATUS(sys.completeRide(b1.ride.id), 409);
        sys.startRide(b1.ride.id);
        CHECK_STATUS(sys.cancelRide(b1.ride.id), 409);
        sys.completeRide(b1.ride.id);
        CHECK_STATUS(sys.completeRide(b1.ride.id), 409);

        Report rep = sys.report();
        CHECK(rep.completedRides == 1); CHECK(rep.cancelledRides == 1); CHECK(rep.activeRides == 1);
        CHECK_NEAR(rep.platformRevenue + rep.driverPayouts, fare);
        CHECK_NEAR(rep.platformRevenue, round2(fare - round2(fare * 0.8)));

        // surge hour
        hour = 18;
        sys.startRide(b2.ride.id);
        sys.completeRide(b2.ride.id);
        auto b3 = sys.bookRide(bob.id(), "A", "B", "normal", false);
        CHECK(b3.ride.fareType == "SURGE");
        CHECK_STATUS(sys.bookRide(bob.id(), "A", "Nowhere", "normal", false), 400);
        CHECK_STATUS(sys.bookRide(bob.id(), "A", "A", "normal", false), 400);
        CHECK_STATUS(sys.bookRide(bob.id(), "A", "Island", "normal", false), 400);
    }
    // persistence: reload from disk
    {
        RideSystem sys(city(), dir.string(), [] { return 12; });
        CHECK(sys.report().totalUsers == 2);
        CHECK(sys.report().totalRides == 4);
        CHECK(sys.getRide(1).status == RideStatus::Cancelled);
        User c = sys.registerUser("Carol", "9000000003", "female", "secret5");
        CHECK(c.id() == 3);  // id counters restored
    }
    fs::remove_all(dir);
}

int main() {
    testDijkstra(); testFareEta(); testWallet(); testSystem();
    std::cout << (checks - failures) << "/" << checks << " checks passed\n";
    return failures ? 1 : 0;
}
