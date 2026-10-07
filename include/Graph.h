#pragma once
// City road network as a weighted undirected graph (adjacency list) + Dijkstra.
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rs {

class Graph {
public:
    static constexpr double INF = std::numeric_limits<double>::infinity();

    struct Result { std::vector<double> dist; std::vector<int> parent; };
    struct Path {
        double distanceKm = INF;
        std::vector<int> nodes;
        bool found() const { return !nodes.empty(); }
    };

    int addNode(const std::string& name);
    void addEdge(int u, int v, double km);            // undirected; km must be > 0
    int nodeId(const std::string& name) const;        // -1 if unknown
    const std::string& nodeName(int id) const { return names_.at(id); }
    const std::vector<std::string>& names() const { return names_; }
    bool valid(int id) const { return id >= 0 && id < (int)names_.size(); }
    std::size_t size() const { return names_.size(); }

    Result dijkstra(int src) const;                   // O((V+E) log V)
    Path shortestPath(int src, int dst) const;

    // File format: "N <name>" lines, then "E <nameA> <nameB> <km>" lines; '#' = comment.
    static Graph loadFromFile(const std::string& path);

private:
    std::vector<std::string> names_;
    std::unordered_map<std::string, int> index_;
    std::vector<std::vector<std::pair<int, double>>> adj_;
};

}  // namespace rs
