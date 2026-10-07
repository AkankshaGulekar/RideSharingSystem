#include "Storage.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace rs {
namespace {

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string item;
    std::istringstream ss(s);
    while (std::getline(ss, item, delim)) out.push_back(item);
    if (!s.empty() && s.back() == delim) out.push_back("");
    return out;
}

std::string num(double v) {
    std::ostringstream o;
    o.precision(12);
    o << v;
    return o.str();
}

void writeAtomic(const fs::path& path, const std::vector<std::string>& lines) {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot write " + tmp.string());
        for (const auto& l : lines) out << l << '\n';
    }
    fs::rename(tmp, path);  // atomic replace: a crash never leaves a half-written file
}

std::vector<std::string> readLines(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string l;
    while (std::getline(in, l))
        if (!l.empty()) lines.push_back(l);
    return lines;
}

std::string joinRoute(const std::vector<int>& r) {
    std::string s;
    for (std::size_t i = 0; i < r.size(); ++i) s += (i ? "," : "") + std::to_string(r[i]);
    return s;
}

}  // namespace

Storage::Storage(std::string dir) : dir_(std::move(dir)) { fs::create_directories(dir_); }

void Storage::save(const Data& d) const {
    std::vector<std::string> lines;
    for (const auto& [id, u] : d.users)
        lines.push_back(std::to_string(id) + "|" + u.name() + "|" + u.phone() + "|" + toString(u.gender()) +
                        "|" + std::to_string(u.passwordHash()) + "|" + num(u.wallet().balance()));
    writeAtomic(fs::path(dir_) / "users.txt", lines);

    lines.clear();
    for (const auto& [id, v] : d.drivers)
        lines.push_back(std::to_string(id) + "|" + v.name() + "|" + v.phone() + "|" + toString(v.gender()) +
                        "|" + std::to_string(v.passwordHash()) + "|" + num(v.wallet().balance()) + "|" +
                        v.vehicle() + "|" + std::to_string(v.location()) + "|" + (v.available() ? "1" : "0") +
                        "|" + num(v.rating()) + "|" + std::to_string(v.ridesCompleted()));
    writeAtomic(fs::path(dir_) / "drivers.txt", lines);

    lines.clear();
    for (const auto& [id, r] : d.rides)
        lines.push_back(std::to_string(id) + "|" + std::to_string(r.userId) + "|" + std::to_string(r.driverId) +
                        "|" + std::to_string(r.source) + "|" + std::to_string(r.destination) + "|" +
                        joinRoute(r.route) + "|" + num(r.distanceKm) + "|" + num(r.etaMin) + "|" + num(r.fare) +
                        "|" + r.fareType + "|" + (r.femaleOnly ? "1" : "0") + "|" + toString(r.status) + "|" +
                        std::to_string(r.createdAt));
    writeAtomic(fs::path(dir_) / "rides.txt", lines);

    lines.clear();
    for (const auto& p : d.payments)
        lines.push_back(std::to_string(p.id) + "|" + std::to_string(p.rideId) + "|" + std::to_string(p.userId) +
                        "|" + std::to_string(p.driverId) + "|" + num(p.amount) + "|" + p.type + "|" +
                        std::to_string(p.timestamp));
    writeAtomic(fs::path(dir_) / "payments.txt", lines);
}

void Storage::load(Data& d) const {
    try {
        for (const auto& l : readLines(fs::path(dir_) / "users.txt")) {
            auto f = split(l, '|');
            User u(std::stoi(f.at(0)), f.at(1), f.at(2), parseGender(f.at(3)), std::stoull(f.at(4)));
            u.wallet().restore(std::stod(f.at(5)));
            d.users.emplace(u.id(), u);
        }
        for (const auto& l : readLines(fs::path(dir_) / "drivers.txt")) {
            auto f = split(l, '|');
            Driver v(std::stoi(f.at(0)), f.at(1), f.at(2), parseGender(f.at(3)), std::stoull(f.at(4)),
                     f.at(6), std::stoi(f.at(7)));
            v.wallet().restore(std::stod(f.at(5)));
            v.setAvailable(f.at(8) == "1");
            v.setRating(std::stod(f.at(9)));
            v.setRidesCompleted(std::stoi(f.at(10)));
            d.drivers.emplace(v.id(), v);
        }
        for (const auto& l : readLines(fs::path(dir_) / "rides.txt")) {
            auto f = split(l, '|');
            Ride r;
            r.id = std::stoi(f.at(0));
            r.userId = std::stoi(f.at(1));
            r.driverId = std::stoi(f.at(2));
            r.source = std::stoi(f.at(3));
            r.destination = std::stoi(f.at(4));
            for (const auto& n : split(f.at(5), ',')) r.route.push_back(std::stoi(n));
            r.distanceKm = std::stod(f.at(6));
            r.etaMin = std::stod(f.at(7));
            r.fare = std::stod(f.at(8));
            r.fareType = f.at(9);
            r.femaleOnly = f.at(10) == "1";
            r.status = parseStatus(f.at(11));
            r.createdAt = std::stol(f.at(12));
            d.rides.emplace(r.id, r);
        }
        for (const auto& l : readLines(fs::path(dir_) / "payments.txt")) {
            auto f = split(l, '|');
            Payment p;
            p.id = std::stoi(f.at(0));
            p.rideId = std::stoi(f.at(1));
            p.userId = std::stoi(f.at(2));
            p.driverId = std::stoi(f.at(3));
            p.amount = std::stod(f.at(4));
            p.type = f.at(5);
            p.timestamp = std::stol(f.at(6));
            d.payments.push_back(p);
        }
    } catch (const ApiError&) {
        throw std::runtime_error("Corrupt data file (bad gender value)");
    } catch (const std::logic_error& e) {  // stoi/stod/out_of_range/at()
        throw std::runtime_error(std::string("Corrupt data file: ") + e.what());
    }
}

}  // namespace rs
