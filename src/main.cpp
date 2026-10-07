// REST layer (controller): parses JSON, calls RideSystem, maps ApiError -> HTTP status.
#include <cstdlib>
#include <iostream>

#include "crow_all.h"
#include "RideSystem.h"

using namespace rs;

// ---------- response helpers ----------
static crow::response jsonResp(int code, crow::json::wvalue& j) {
    crow::response r(code, j.dump());
    r.set_header("Content-Type", "application/json");
    return r;
}
static crow::response errorResp(int code, const std::string& msg) {
    crow::json::wvalue j;
    j["error"] = msg;
    return jsonResp(code, j);
}

// Runs a handler body; converts exceptions into JSON error responses.
template <class F>
static crow::response guarded(F&& f) {
    try {
        return f();
    } catch (const ApiError& e) {
        return errorResp(e.status, e.what());
    } catch (const std::exception& e) {
        return errorResp(500, std::string("Internal error: ") + e.what());
    }
}

// ---------- request helpers ----------
static crow::json::rvalue parseBody(const crow::request& req) {
    auto body = crow::json::load(req.body);
    if (!body) throw ApiError(400, "Invalid JSON body");
    return body;
}
static std::string reqStr(crow::json::rvalue& b, const char* key) {
    if (!b.has(key) || b[key].t() != crow::json::type::String)
        throw ApiError(400, std::string("Missing or invalid string field: ") + key);
    return std::string(b[key].s());
}
static double reqNum(crow::json::rvalue& b, const char* key) {
    if (!b.has(key) || b[key].t() != crow::json::type::Number)
        throw ApiError(400, std::string("Missing or invalid number field: ") + key);
    return b[key].d();
}
static bool optBool(crow::json::rvalue& b, const char* key, bool def) {
    if (!b.has(key)) return def;
    auto t = b[key].t();
    if (t == crow::json::type::True) return true;
    if (t == crow::json::type::False) return false;
    throw ApiError(400, std::string("Field must be true/false: ") + key);
}

// ---------- JSON serializers ----------
static crow::json::wvalue userJson(const User& u) {
    crow::json::wvalue j;
    j["id"] = u.id();
    j["name"] = u.name();
    j["phone"] = u.phone();
    j["gender"] = toString(u.gender());
    j["walletBalance"] = u.wallet().balance();
    return j;
}
static crow::json::wvalue driverJson(const Driver& d, const Graph& g) {
    crow::json::wvalue j;
    j["id"] = d.id();
    j["name"] = d.name();
    j["vehicle"] = d.vehicle();
    j["gender"] = toString(d.gender());
    j["location"] = g.nodeName(d.location());
    j["rating"] = d.rating();
    return j;
}
static crow::json::wvalue rideJson(const Ride& r, const Graph& g) {
    crow::json::wvalue j;
    j["id"] = r.id;
    j["userId"] = r.userId;
    j["driverId"] = r.driverId;
    j["source"] = g.nodeName(r.source);
    j["destination"] = g.nodeName(r.destination);
    for (std::size_t i = 0; i < r.route.size(); ++i) j["route"][i] = g.nodeName(r.route[i]);
    j["distanceKm"] = r.distanceKm;
    j["etaMinutes"] = r.etaMin;
    j["fare"] = r.fare;
    j["fareType"] = r.fareType;
    j["femaleOnly"] = r.femaleOnly;
    j["status"] = toString(r.status);
    return j;
}

int main(int argc, char** argv) {
    std::string mapFile = argc > 1 ? argv[1] : "data/map.txt";
    std::string dataDir = argc > 2 ? argv[2] : "data/store";
    const char* keyEnv = std::getenv("ADMIN_KEY");
    const std::string adminKey = keyEnv ? keyEnv : "admin123";

    try {
        RideSystem sys(Graph::loadFromFile(mapFile), dataDir);
        crow::SimpleApp app;

        CROW_ROUTE(app, "/health")([] {
            crow::json::wvalue j;
            j["status"] = "ok";
            return jsonResp(200, j);
        });

        CROW_ROUTE(app, "/locations")([&sys] {
            crow::json::wvalue j;
            const auto& names = sys.graph().names();
            for (std::size_t i = 0; i < names.size(); ++i) j["locations"][i] = names[i];
            return jsonResp(200, j);
        });

        CROW_ROUTE(app, "/register/user").methods("POST"_method)([&sys](const crow::request& req) {
            return guarded([&]() -> crow::response {
                auto b = parseBody(req);
                User u = sys.registerUser(reqStr(b, "name"), reqStr(b, "phone"), reqStr(b, "gender"),
                                          reqStr(b, "password"));
                auto j = userJson(u);
                return jsonResp(201, j);
            });
        });

        CROW_ROUTE(app, "/register/driver").methods("POST"_method)([&sys](const crow::request& req) {
            return guarded([&]() -> crow::response {
                auto b = parseBody(req);
                Driver d = sys.registerDriver(reqStr(b, "name"), reqStr(b, "phone"), reqStr(b, "gender"),
                                              reqStr(b, "password"), reqStr(b, "vehicle"),
                                              reqStr(b, "location"));
                auto j = driverJson(d, sys.graph());
                return jsonResp(201, j);
            });
        });

        CROW_ROUTE(app, "/login").methods("POST"_method)([&sys](const crow::request& req) {
            return guarded([&]() -> crow::response {
                auto b = parseBody(req);
                int id = sys.login(reqStr(b, "role"), reqStr(b, "phone"), reqStr(b, "password"));
                crow::json::wvalue j;
                j["id"] = id;
                j["message"] = "Login successful";
                return jsonResp(200, j);
            });
        });

        CROW_ROUTE(app, "/user/<int>")([&sys](int id) {
            return guarded([&]() -> crow::response {
                auto j = userJson(sys.getUser(id));
                return jsonResp(200, j);
            });
        });

        CROW_ROUTE(app, "/wallet/add").methods("POST"_method)([&sys](const crow::request& req) {
            return guarded([&]() -> crow::response {
                auto b = parseBody(req);
                int userId = (int)reqNum(b, "userId");
                double balance = sys.addMoney(userId, reqNum(b, "amount"));
                crow::json::wvalue j;
                j["userId"] = userId;
                j["walletBalance"] = balance;
                return jsonResp(200, j);
            });
        });

        CROW_ROUTE(app, "/ride/book").methods("POST"_method)([&sys](const crow::request& req) {
            return guarded([&]() -> crow::response {
                auto b = parseBody(req);
                std::string type = b.has("rideType") ? reqStr(b, "rideType") : "normal";
                BookingResult br = sys.bookRide((int)reqNum(b, "userId"), reqStr(b, "source"),
                                                reqStr(b, "destination"), type, optBool(b, "femaleOnly", false));
                crow::json::wvalue j;
                j["ride"] = rideJson(br.ride, sys.graph());
                j["driver"] = driverJson(br.driver, sys.graph());
                j["pickupDistanceKm"] = br.pickupDistanceKm;
                j["pickupEtaMinutes"] = br.pickupEtaMin;
                return jsonResp(201, j);
            });
        });

        CROW_ROUTE(app, "/ride/<int>")([&sys](int id) {
            return guarded([&]() -> crow::response {
                auto j = rideJson(sys.getRide(id), sys.graph());
                return jsonResp(200, j);
            });
        });

        CROW_ROUTE(app, "/ride/<int>/start").methods("POST"_method)([&sys](int id) {
            return guarded([&]() -> crow::response {
                auto j = rideJson(sys.startRide(id), sys.graph());
                return jsonResp(200, j);
            });
        });
        CROW_ROUTE(app, "/ride/<int>/complete").methods("POST"_method)([&sys](int id) {
            return guarded([&]() -> crow::response {
                auto j = rideJson(sys.completeRide(id), sys.graph());
                return jsonResp(200, j);
            });
        });
        CROW_ROUTE(app, "/ride/<int>/cancel").methods("POST"_method)([&sys](int id) {
            return guarded([&]() -> crow::response {
                auto j = rideJson(sys.cancelRide(id), sys.graph());
                return jsonResp(200, j);
            });
        });

        CROW_ROUTE(app, "/admin/report")([&sys, adminKey](const crow::request& req) {
            return guarded([&]() -> crow::response {
                if (req.get_header_value("X-Admin-Key") != adminKey) throw ApiError(401, "Invalid or missing X-Admin-Key");
                Report r = sys.report();
                crow::json::wvalue j;
                j["totalUsers"] = r.totalUsers;
                j["totalDrivers"] = r.totalDrivers;
                j["availableDrivers"] = r.availableDrivers;
                j["totalRides"] = r.totalRides;
                j["activeRides"] = r.activeRides;
                j["completedRides"] = r.completedRides;
                j["cancelledRides"] = r.cancelledRides;
                j["totalFareCollected"] = r.totalFareCollected;
                j["platformRevenue"] = r.platformRevenue;
                j["driverPayouts"] = r.driverPayouts;
                return jsonResp(200, j);
            });
        });

        std::cout << "Ride server listening on http://localhost:18080\n";
        app.port(18080).multithreaded().run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
