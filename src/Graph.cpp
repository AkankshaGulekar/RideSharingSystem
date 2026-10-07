#include "Graph.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace rs {

int Graph::addNode(const std::string& name) {
    auto it = index_.find(name);
    if (it != index_.end()) return it->second;
    int id = (int)names_.size();
    names_.push_back(name);
    index_[name] = id;
    adj_.emplace_back();
    return id;
}

void Graph::addEdge(int u, int v, double km) {
    if (!valid(u) || !valid(v)) throw std::invalid_argument("addEdge: bad node id");
    if (!(km > 0)) throw std::invalid_argument("addEdge: weight must be positive (Dijkstra needs non-negative)");
    adj_[u].push_back({v, km});
    adj_[v].push_back({u, km});
}

int Graph::nodeId(const std::string& name) const {
    auto it = index_.find(name);
    return it == index_.end() ? -1 : it->second;
}

Graph::Result Graph::dijkstra(int src) const {
    Result r;
    r.dist.assign(size(), INF);
    r.parent.assign(size(), -1);
    if (!valid(src)) return r;

    using QItem = std::pair<double, int>;  // (distance, node)
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;  // min-heap
    r.dist[src] = 0;
    pq.push({0, src});
    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        if (d > r.dist[u]) continue;  // stale entry (lazy deletion)
        for (auto [v, w] : adj_[u]) {
            if (r.dist[u] + w < r.dist[v]) {
                r.dist[v] = r.dist[u] + w;
                r.parent[v] = u;
                pq.push({r.dist[v], v});
            }
        }
    }
    return r;
}

Graph::Path Graph::shortestPath(int src, int dst) const {
    Path p;
    if (!valid(src) || !valid(dst)) return p;
    Result r = dijkstra(src);
    if (r.dist[dst] == INF) return p;
    for (int cur = dst; cur != -1; cur = r.parent[cur]) p.nodes.push_back(cur);
    std::reverse(p.nodes.begin(), p.nodes.end());
    p.distanceKm = r.dist[dst];
    return p;
}

Graph Graph::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open map file: " + path);
    Graph g;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kind;
        ss >> kind;
        if (kind == "N") {
            std::string name;
            ss >> name;
            g.addNode(name);
        } else if (kind == "E") {
            std::string a, b;
            double km;
            ss >> a >> b >> km;
            int u = g.nodeId(a), v = g.nodeId(b);
            if (u < 0 || v < 0 || ss.fail()) throw std::runtime_error("Bad edge line: " + line);
            g.addEdge(u, v, km);
        }
    }
    return g;
}

}  // namespace rs
