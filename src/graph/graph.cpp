#include "graph.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <iostream>

LabeledGraph LabeledGraph::load(const std::string& path) {
    LabeledGraph g;
    std::ifstream fin(path);
    if (!fin.is_open())
        throw std::runtime_error("Cannot open graph file: " + path);

    std::string line;
    VertexId max_id = 0;

    while (std::getline(fin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty()) continue;

        std::istringstream iss(line);
        VertexId src, dst;
        std::string label;
        if (!(iss >> src >> dst >> label)) {
            std::cerr << "Warning: skipping malformed line: " << line << "\n";
            continue;
        }

        g.edges_by_label_[label].push_back({src, dst});
        g.labels_.insert(label);
        max_id = std::max(max_id, std::max(src, dst));
    }

    g.num_vertices_ = max_id + 1;
    uint64_t total_edges = 0;
    for (auto& [_, edges] : g.edges_by_label_)
        total_edges += edges.size();

    std::cout << "Graph: " << g.num_vertices_ << " vertices, "
              << g.labels_.size() << " labels, "
              << total_edges << " edges\n";

    return g;
}