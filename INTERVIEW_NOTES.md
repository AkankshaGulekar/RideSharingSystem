# Interview cheat-sheet: where everything is in THIS code

**Pitch:** C++17 ride-sharing backend. Crow REST API -> RideSystem service layer -> models; city = weighted graph, Dijkstra picks
nearest driver and shortest route; Strategy pattern for fares; wallet escrow payments; female-only matching rule; file persistence.

## "Show me where..."
| Question | File / function |
|---|---|
| Dijkstra | `src/Graph.cpp` `Graph::dijkstra`, path rebuilt in `shortestPath` via `parent[]` |
| Nearest driver | `RideSystem::bookRide`: one Dijkstra from pickup, scan available drivers, min distance (tie -> lower id) |
| Strategy pattern | `include/Fare.h` `FareStrategy` + `FareFactory::create` |
| Dynamic (surge) pricing | `FareFactory::isPeak` -> `SurgeFare(1.5)` at 8-11 and 17-21 |
| ETA | `include/Eta.h` distance / (30 x trafficFactor) |
| Female-only rule | `bookRide`: rider must be female (403), driver loop skips non-female |
| Wallet safety | `Wallet::deduct` throws 402; fare deducted only after matching succeeded |
| Ride state machine | REQUESTED/ACCEPTED -> ONGOING -> COMPLETED, cancel only before start (409 otherwise) |
| Concurrency | `std::mutex m_` + `lock_guard` in every public `RideSystem` method |
| Persistence | `src/Storage.cpp` save/load, `writeAtomic` (tmp + rename) |
| Error handling | `ApiError{status}` thrown in service, mapped to HTTP in `guarded()` in `main.cpp` |
| OOP | `Person` (abstract, virtual dtor, pure virtual `role()`) -> `User`, `Driver`; `Ride` etc. composed |

## Key numbers
- Dijkstra: O((V+E) log V), needs non-negative weights; lazy deletion (`if (d > dist[u]) continue`).
- Fare normal = 40 + 12/km + 1.5/min; premium = 60 + 18/km + 2/min; surge = 1.5 x normal. Commission 20%.
- ETA speed 30 km/h; factor 0.6 rush hour, 1.2 night, 1.0 otherwise.

## Honest limitations (say them first)
std::hash passwords, no JWT/HTTPS, whole-file rewrite, no ACID, heuristic ETA, static graph, self-declared gender, one coarse mutex.

## Likely follow-ups
- Why unordered_map for users? O(1) average lookup by id. Why vector for adjacency? sparse graph, cache-friendly.
- Why virtual destructor? deleting derived through base pointer is UB otherwise.
- Why lock around match + assign? otherwise two riders could get the same driver (race condition).
- Why hold funds at booking? avoids a rider spending the money mid-ride; refund on cancel.
- Scale up? geospatial index (geohash/quadtree), Redis for locations, DB + transactions, message queue, A*.
- Why rename for saving? atomic replace; a crash never leaves a half-written file.

## Be honest about authorship
Know every line before the interview. If asked, say you built it with AI assistance, then explain the design and changes you made.
