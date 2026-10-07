# Ride Sharing System (Uber/Ola backend simulation)

C++17 backend with REST APIs (Crow), Dijkstra route optimisation, ETA prediction, strategy-based dynamic fares,
wallet/payments, admin reports, a female-only safety rule and file persistence.

## Architecture
```
HTTP/JSON -> src/main.cpp (Crow routes, controller)
          -> RideSystem (service layer: business rules, one mutex)
          -> models.h (Wallet, Person -> User/Driver, Ride, Payment)
          -> Graph (Dijkstra) | Fare.h (Strategy+Factory) | Eta.h | Storage (files)
```
| File | Responsibility |
|---|---|
| `include/models.h` | Wallet, Person/User/Driver (inheritance, polymorphism), Ride, Payment, ApiError |
| `src/Graph.cpp` | Adjacency-list graph, Dijkstra with min-heap, path reconstruction, map loader |
| `include/Fare.h` | `FareStrategy` -> Normal / Surge / Premium, `FareFactory` |
| `include/Eta.h` | ETA = distance / (30 km/h x traffic factor by hour) |
| `src/RideSystem.cpp` | Register, login, wallet, matching, booking, ride state machine, reports |
| `src/Storage.cpp` | Pipe-delimited files, atomic write (temp file + rename) |
| `tests/test_core.cpp` | 50 checks: Dijkstra, fares, ETA, wallet, female-only, lifecycle, persistence |

## Setup
Requirements: g++ 9+ (or clang), CMake 3.14+, standalone Asio.

```bash
# Ubuntu/Debian
sudo apt update && sudo apt install -y build-essential cmake libasio-dev curl
# macOS:  brew install cmake asio
# Download Crow's single header
curl -L -o third_party/crow_all.h https://github.com/CrowCpp/Crow/releases/download/v1.2.0/crow_all.h
# Build
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure     # run tests
./build/ride_server                            # start server on :18080
```
Admin key: env `ADMIN_KEY` (default `admin123`). On macOS with Homebrew Asio you may need
`cmake -S . -B build -DCMAKE_CXX_FLAGS="-I$(brew --prefix asio)/include"`.

## API
| Method | Path | Body |
|---|---|---|
| POST | `/register/user` | name, phone, gender, password |
| POST | `/register/driver` | name, phone, gender, password, vehicle, location |
| POST | `/login` | role (user/driver), phone, password |
| GET | `/user/<id>` | |
| POST | `/wallet/add` | userId, amount |
| POST | `/ride/book` | userId, source, destination, rideType (normal/premium), femaleOnly |
| POST | `/ride/<id>/start`, `/complete`, `/cancel` | |
| GET | `/ride/<id>` | |
| GET | `/locations` | |
| GET | `/admin/report` | header `X-Admin-Key` |

Errors: 400 validation, 401 auth, 402 insufficient balance, 403 female-only violation, 404 not found / no driver, 409 invalid state / duplicate.

## Demo
```bash
curl -X POST localhost:18080/register/user -H 'Content-Type: application/json' \
  -d '{"name":"Asha","phone":"9000000001","gender":"female","password":"secret1"}'
curl -X POST localhost:18080/register/driver -H 'Content-Type: application/json' \
  -d '{"name":"Meera","phone":"9100000001","gender":"female","password":"secret2","vehicle":"Swift MH12AB1234","location":"Station"}'
curl -X POST localhost:18080/wallet/add -H 'Content-Type: application/json' -d '{"userId":1,"amount":1000}'
curl -X POST localhost:18080/ride/book -H 'Content-Type: application/json' \
  -d '{"userId":1,"source":"Camp","destination":"Hinjewadi","rideType":"normal","femaleOnly":true}'
curl -X POST localhost:18080/ride/1/start
curl -X POST localhost:18080/ride/1/complete
curl localhost:18080/admin/report -H 'X-Admin-Key: admin123'
```

## Design decisions
- **Dijkstra** on a weighted graph; one run from the pickup node gives distances to all drivers (nearest-driver match).
- **Strategy pattern** for fares: add a new pricing rule without touching booking code (Open/Closed).
- **Escrow-style payments**: fare is held at booking, refunded on cancel, split 80/20 driver/platform on completion.
- **Concurrency**: Crow is multithreaded, so `RideSystem` guards state with one mutex; matching and driver assignment happen under one lock (no double-booking).
- **Atomic persistence**: write `file.tmp`, then `rename` over the real file.

## Known limitations (and what production would do)
- Passwords hashed with `std::hash` (not secure) -> bcrypt/argon2; no tokens/JWT/HTTPS/rate limiting.
- Whole files rewritten on every change; no transactions -> a real database (PostgreSQL), Redis for driver locations.
- ETA is a heuristic, not ML; the graph is static (no live traffic) -> A*/contraction hierarchies, live edge weights.
- Gender is self-declared -> KYC/ID verification in production.
- Coarse single mutex limits throughput -> per-driver locks / DB row locks.
