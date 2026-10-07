#pragma once
// File persistence: pipe-delimited text files, written atomically (temp file + rename).
#include <string>
#include <unordered_map>
#include <vector>

#include "models.h"

namespace rs {

struct Data {
    std::unordered_map<int, User> users;      // O(1) average lookup by id
    std::unordered_map<int, Driver> drivers;
    std::unordered_map<int, Ride> rides;
    std::vector<Payment> payments;
};

class Storage {
    std::string dir_;
public:
    explicit Storage(std::string dir);
    void save(const Data& d) const;
    void load(Data& d) const;  // missing files = empty data
};

}  // namespace rs
