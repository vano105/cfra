#pragma once

#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <cstdint>

using VertexId = uint32_t;

class LabeledGraph {
public:
    struct Edge {
    public:
        VertexId src;
        VertexId dst;
    };

    static LabeledGraph load(const std::string& path);

    VertexId num_vertices() const { return num_vertices_; }

    const std::set<std::string>& labels() const { return labels_; }

    const std::unordered_map<std::string, std::vector<Edge>>& edges_by_label() const {
        return edges_by_label_;
    }

private:
    VertexId num_vertices_ = 0;
    std::set<std::string> labels_;
    std::unordered_map<std::string, std::vector<Edge>> edges_by_label_;
};