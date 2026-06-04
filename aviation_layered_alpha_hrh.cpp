#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

/*
 * aviation_layered_alpha_hrh.cpp
 * ------------------------------------------------------------
 * A solver-free routing heuristic for the user's aviation cable routing data.
 * It keeps the original center/leaf modelling convention of scip_heur.cpp,
 * but replaces the final SCIP MIP solve with:
 *   1) alpha-shortest-path initial candidate generation,
 *   2) HRH-style one-cable-at-a-time bundling local search,
 *   3) layer-aware acyclicity / only-father repair,
 *   4) conflict-penalized local rerouting.
 *
 * Build:
 *   g++ -std=c++17 -O2 aviation_layered_alpha_hrh.cpp -o aviation_layered_alpha_hrh
 *
 * Run:
 *   ./aviation_layered_alpha_hrh data/edges-4.csv data/pairs-4-246.csv 3 "0,0.05,0.1,0.2,0.35" 1.2 7 5 80 30 result_layered_hrh
 *
 * CSV assumptions copied from the original code:
 *   edges csv: tokens[2] = node1_name, tokens[3] = node2_name, tokens[4] = edge weight
 *   pairs csv: tokens[3] = leaf start, tokens[4] = leaf end, tokens[5] = pair weight/demand
 */

static constexpr double INF = 1e100;
static constexpr double EPS = 1e-9;

static vector<string> parseCSVLine(const string& line) {
    vector<string> out;
    string cur;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                cur.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (c == ',' && !in_quotes) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

static vector<string> split(const string& s, char sep) {
    vector<string> out;
    string item;
    stringstream ss(s);
    while (getline(ss, item, sep)) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static string trim(const string& s) {
    size_t a = 0;
    while (a < s.size() && isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

static bool isCenterName(const string& name) {
    if (name.empty()) return false;
    char c = name.front();
    return c == 'N' || c == 'M' || c == 'E';
}

static pair<int,int> normEdge(int a, int b) {
    return (a < b) ? make_pair(a, b) : make_pair(b, a);
}

struct Edge {
    int u = -1;
    int v = -1;
    double w = 0.0;
    bool is_center_edge = false;
    bool is_entry_edge = false;
    bool is_leaf_edge = false;
};

struct Path {
    vector<int> nodes;
    vector<int> edge_ids;
    double length = INF;      // base physical/cost length, without wL/wB/penalty
    bool feasible = false;
};

struct CablePair {
    int id = -1;
    int leaf_s = -1;
    int leaf_t = -1;
    int center_s = -1;
    int center_t = -1;
    double demand = 1.0;      // original pair weight. If duplicated center pair, aggregated.
    vector<pair<int,int>> original_leaf_pairs;
};

struct GraphData {
    map<string,int> node_name_to_id;
    vector<string> id_to_node_name;
    vector<Edge> edges;
    map<pair<int,int>, int> undirected_edge_to_id;
    vector<vector<pair<int,int>>> center_adj; // node -> (to, edge_id), center edges only
    set<int> center_nodes;
    vector<int> center_edge_ids;
    vector<int> entry_edge_ids;
    vector<int> leaf_edge_ids;
    unordered_map<int,int> leaf_to_center;
};

struct LayerGraph {
    set<pair<int,int>> directed_edges;  // oriented by path direction
    unordered_map<int, set<int>> parents; // node -> parent nodes
};

struct LayerRepairResult {
    bool feasible = false;
    int used_layers = 0;
    vector<int> pair_to_layer;
    vector<int> conflict_pairs;
    int only_father_conflicts = 0;
    int cycle_conflicts = 0;
};

struct Solution {
    double wB = 0.0;
    int init_id = -1;
    vector<Path> routes;
    double objective = INF;
    double cable_length = INF;
    double bundle_length = INF;
    int used_layers = 0;
    bool feasible = false;
    vector<int> pair_to_layer;
    int repair_rounds = 0;
    int conflict_count = 0;
};

static int getNodeId(GraphData& g, const string& name) {
    auto it = g.node_name_to_id.find(name);
    if (it != g.node_name_to_id.end()) return it->second;
    int id = static_cast<int>(g.id_to_node_name.size());
    g.node_name_to_id[name] = id;
    g.id_to_node_name.push_back(name);
    g.center_adj.emplace_back();
    return id;
}

static GraphData readEdges(const string& edge_csv) {
    GraphData g;
    ifstream fin(edge_csv);
    if (!fin.is_open()) throw runtime_error("Cannot open edge csv: " + edge_csv);
    string line;
    getline(fin, line); // header
    while (getline(fin, line)) {
        if (trim(line).empty()) continue;
        auto tok = parseCSVLine(line);
        if (tok.size() < 5) continue;
        string n1 = trim(tok[2]);
        string n2 = trim(tok[3]);
        double w = stod(trim(tok[4]));
        int u = getNodeId(g, n1);
        int v = getNodeId(g, n2);
        auto key = normEdge(u, v);
        if (g.undirected_edge_to_id.count(key)) {
            // Keep the cheaper duplicate if the CSV repeats an edge.
            int eid = g.undirected_edge_to_id[key];
            g.edges[eid].w = min(g.edges[eid].w, w);
            continue;
        }
        int eid = static_cast<int>(g.edges.size());
        g.undirected_edge_to_id[key] = eid;
        Edge e;
        e.u = key.first;
        e.v = key.second;
        e.w = w;

        bool c1 = isCenterName(n1);
        bool c2 = isCenterName(n2);
        if (c1) g.center_nodes.insert(u);
        if (c2) g.center_nodes.insert(v);

        if (c1 && c2) {
            e.is_center_edge = true;
            g.center_edge_ids.push_back(eid);
        } else if (c1 || c2) {
            e.is_leaf_edge = true;
            g.leaf_edge_ids.push_back(eid);
            int center = c1 ? u : v;
            int leaf = c1 ? v : u;
            g.leaf_to_center[leaf] = center;
        } else {
            // This mirrors the original assumption: pure leaf-leaf edges are unusual.
            e.is_leaf_edge = true;
            g.leaf_edge_ids.push_back(eid);
        }

        // Preserve original special case: N/M/E to E may also be an entry-like center edge.
        // It is still usable in the center graph because both endpoints are treated as centers.
        if (c1 && c2 && (n1.front() == 'E' || n2.front() == 'E')) {
            e.is_entry_edge = true;
            g.entry_edge_ids.push_back(eid);
        }
        g.edges.push_back(e);
    }

    g.center_adj.assign(g.id_to_node_name.size(), {});
    for (int eid : g.center_edge_ids) {
        const Edge& e = g.edges[eid];
        g.center_adj[e.u].push_back({e.v, eid});
        g.center_adj[e.v].push_back({e.u, eid});
    }
    cerr << "Read nodes=" << g.id_to_node_name.size()
         << ", edges=" << g.edges.size()
         << ", center_edges=" << g.center_edge_ids.size()
         << ", leaf_edges=" << g.leaf_edge_ids.size() << "\n";
    return g;
}

static vector<CablePair> readPairs(const string& pair_csv, const GraphData& g) {
    ifstream fin(pair_csv);
    if (!fin.is_open()) throw runtime_error("Cannot open pair csv: " + pair_csv);
    string line;
    getline(fin, line); // header

    map<pair<int,int>, int> center_pair_to_idx;
    vector<CablePair> pairs;

    while (getline(fin, line)) {
        if (trim(line).empty()) continue;
        auto tok = parseCSVLine(line);
        if (tok.size() < 6) continue;
        string sname = trim(tok[3]);
        string tname = trim(tok[4]);
        double demand = stod(trim(tok[5]));
        auto its = g.node_name_to_id.find(sname);
        auto itt = g.node_name_to_id.find(tname);
        if (its == g.node_name_to_id.end() || itt == g.node_name_to_id.end()) {
            cerr << "Skip pair with unknown node: " << sname << "," << tname << "\n";
            continue;
        }
        int ls = its->second;
        int lt = itt->second;
        if (!g.leaf_to_center.count(ls) || !g.leaf_to_center.count(lt)) {
            cerr << "Skip pair without leaf_to_center mapping: " << sname << "," << tname << "\n";
            continue;
        }
        int cs = g.leaf_to_center.at(ls);
        int ct = g.leaf_to_center.at(lt);
        if (cs == ct) {
            // Same center: no center route needed. Keep it as a zero-length center pair.
        }
        auto key = normEdge(cs, ct);
        if (!center_pair_to_idx.count(key)) {
            CablePair p;
            p.id = static_cast<int>(pairs.size());
            p.center_s = key.first;
            p.center_t = key.second;
            p.leaf_s = ls;
            p.leaf_t = lt;
            p.demand = demand;
            p.original_leaf_pairs.push_back({ls, lt});
            center_pair_to_idx[key] = p.id;
            pairs.push_back(p);
        } else {
            int idx = center_pair_to_idx[key];
            pairs[idx].demand += demand;
            pairs[idx].original_leaf_pairs.push_back({ls, lt});
        }
    }
    cerr << "Read aggregated center pairs=" << pairs.size() << "\n";
    return pairs;
}

static Path dijkstraPath(
    const GraphData& g,
    int source,
    int target,
    const vector<double>& edge_cost
) {
    Path res;
    if (source == target) {
        res.nodes = {source};
        res.edge_ids.clear();
        res.length = 0.0;
        res.feasible = true;
        return res;
    }
    int n = static_cast<int>(g.id_to_node_name.size());
    vector<double> dist(n, INF);
    vector<int> prev_node(n, -1), prev_edge(n, -1);
    priority_queue<pair<double,int>, vector<pair<double,int>>, greater<pair<double,int>>> pq;
    dist[source] = 0.0;
    pq.push({0.0, source});
    while (!pq.empty()) {
        auto [du, u] = pq.top();
        pq.pop();
        if (du > dist[u] + EPS) continue;
        if (u == target) break;
        if (u >= static_cast<int>(g.center_adj.size())) continue;
        for (auto [v, eid] : g.center_adj[u]) {
            double nd = du + edge_cost[eid];
            if (nd + EPS < dist[v]) {
                dist[v] = nd;
                prev_node[v] = u;
                prev_edge[v] = eid;
                pq.push({nd, v});
            }
        }
    }
    if (dist[target] >= INF / 2) return res;

    vector<int> rev_nodes;
    vector<int> rev_edges;
    int cur = target;
    rev_nodes.push_back(cur);
    while (cur != source) {
        int eid = prev_edge[cur];
        int pn = prev_node[cur];
        if (eid < 0 || pn < 0) return res;
        rev_edges.push_back(eid);
        cur = pn;
        rev_nodes.push_back(cur);
    }
    reverse(rev_nodes.begin(), rev_nodes.end());
    reverse(rev_edges.begin(), rev_edges.end());
    res.nodes = std::move(rev_nodes);
    res.edge_ids = std::move(rev_edges);
    res.length = 0.0;
    for (int eid : res.edge_ids) res.length += g.edges[eid].w;
    res.feasible = true;
    return res;
}

static string pathKey(const Path& p) {
    string s;
    for (int eid : p.edge_ids) {
        s += to_string(eid);
        s += ";";
    }
    return s;
}

static vector<double> baseCosts(const GraphData& g) {
    vector<double> c(g.edges.size(), INF);
    for (int eid : g.center_edge_ids) c[eid] = max(g.edges[eid].w, EPS);
    return c;
}

static vector<vector<Path>> generateAlphaCandidates(
    const GraphData& g,
    const vector<CablePair>& pairs,
    double alpha,
    int nPhi
) {
    vector<vector<Path>> all(pairs.size());
    auto base = baseCosts(g);
    for (size_t k = 0; k < pairs.size(); ++k) {
        const auto& cp = pairs[k];
        Path sp = dijkstraPath(g, cp.center_s, cp.center_t, base);
        if (!sp.feasible) {
            cerr << "No center shortest path for pair " << k << "\n";
            continue;
        }
        all[k].push_back(sp);
        set<string> seen;
        seen.insert(pathKey(sp));
        const double shortest = max(sp.length, EPS);

        vector<double> penalty(g.edges.size(), 0.0);
        for (int attempt = 0; attempt < nPhi * 12 && static_cast<int>(all[k].size()) < nPhi; ++attempt) {
            vector<double> cost = base;
            // Penalize edges used by previously found paths, with a deterministic rotating bias.
            for (const Path& old : all[k]) {
                for (int eid : old.edge_ids) {
                    penalty[eid] += g.edges[eid].w * (0.20 + 0.05 * (attempt % 5));
                }
            }
            for (int eid : g.center_edge_ids) cost[eid] += penalty[eid];
            Path cand = dijkstraPath(g, cp.center_s, cp.center_t, cost);
            if (!cand.feasible) break;
            string key = pathKey(cand);
            if (!seen.count(key) && cand.length <= alpha * shortest + EPS) {
                all[k].push_back(cand);
                seen.insert(key);
            } else {
                // Add stronger penalty to force diversity next attempt.
                for (int eid : cand.edge_ids) penalty[eid] += g.edges[eid].w * 0.80;
            }
        }
    }
    return all;
}

static double computeObjective(
    const GraphData& g,
    const vector<CablePair>& pairs,
    const vector<Path>& routes,
    double wB,
    double* out_cable_len = nullptr,
    double* out_bundle_len = nullptr
) {
    double wL = 1.0 - wB;
    double cable_len = 0.0;
    unordered_set<int> used_edges;
    for (size_t k = 0; k < routes.size(); ++k) {
        if (!routes[k].feasible) return INF;
        cable_len += pairs[k].demand * routes[k].length;
        for (int eid : routes[k].edge_ids) used_edges.insert(eid);
    }
    double bundle_len = 0.0;
    for (int eid : used_edges) bundle_len += g.edges[eid].w;
    if (out_cable_len) *out_cable_len = cable_len;
    if (out_bundle_len) *out_bundle_len = bundle_len;
    return wL * cable_len + wB * bundle_len;
}

static vector<int> buildUsageExcluding(const vector<Path>& routes, int excluded_k, int m_edges) {
    vector<int> usage(m_edges, 0);
    for (int k = 0; k < static_cast<int>(routes.size()); ++k) {
        if (k == excluded_k) continue;
        for (int eid : routes[k].edge_ids) usage[eid] += 1;
    }
    return usage;
}

static vector<Path> constructInitialRoutes(
    const GraphData& g,
    const vector<CablePair>& pairs,
    const vector<vector<Path>>& candidates,
    double wB,
    int init_id
) {
    vector<Path> routes(pairs.size());
    vector<int> order(pairs.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b) {
        if (fabs(pairs[a].demand - pairs[b].demand) > EPS) return pairs[a].demand > pairs[b].demand;
        return a < b;
    });
    if (!order.empty()) {
        rotate(order.begin(), order.begin() + (init_id % order.size()), order.end());
    }

    unordered_set<int> fixed_used;
    double wL = 1.0 - wB;
    for (int kk : order) {
        if (candidates[kk].empty()) continue;
        const Path* best = nullptr;
        double best_score = INF;
        // Try one deterministic offset first, but still choose by current bundling-aware score.
        for (size_t i = 0; i < candidates[kk].size(); ++i) {
            const Path& p = candidates[kk][(i + init_id) % candidates[kk].size()];
            double score = 0.0;
            for (int eid : p.edge_ids) {
                bool shared = fixed_used.count(eid) > 0;
                score += wL * pairs[kk].demand * g.edges[eid].w + (shared ? 0.0 : wB * g.edges[eid].w);
            }
            if (score < best_score) {
                best_score = score;
                best = &p;
            }
        }
        if (best) routes[kk] = *best;
        for (int eid : routes[kk].edge_ids) fixed_used.insert(eid);
    }
    return routes;
}

static vector<Path> runHRH(
    const GraphData& g,
    const vector<CablePair>& pairs,
    vector<Path> routes,
    double wB,
    int max_passes,
    const vector<double>& global_penalty
) {
    double wL = 1.0 - wB;
    double best_obj = computeObjective(g, pairs, routes, wB);
    vector<int> order(pairs.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b) {
        if (fabs(pairs[a].demand - pairs[b].demand) > EPS) return pairs[a].demand > pairs[b].demand;
        return routes[a].length > routes[b].length;
    });

    int stagnant_passes = 0;
    for (int pass = 0; pass < max_passes && stagnant_passes < 2; ++pass) {
        bool improved = false;
        for (int kk : order) {
            auto usage = buildUsageExcluding(routes, kk, static_cast<int>(g.edges.size()));
            vector<double> cost(g.edges.size(), INF);
            for (int eid : g.center_edge_ids) {
                double ce = g.edges[eid].w;
                bool shared_by_fixed = usage[eid] > 0;
                // Paper Algorithm 3 idea, adapted to pair demand:
                // used edge: only current cable length cost; new edge: cable length + bundle activation cost.
                cost[eid] = wL * pairs[kk].demand * ce + (shared_by_fixed ? 0.0 : wB * ce);
                cost[eid] += global_penalty[eid];
            }
            Path cand = dijkstraPath(g, pairs[kk].center_s, pairs[kk].center_t, cost);
            if (!cand.feasible) continue;
            vector<Path> trial = routes;
            trial[kk] = cand;
            double obj = computeObjective(g, pairs, trial, wB);
            // Penalty is for repair guidance only; primal objective comparison uses true CHRP objective.
            if (obj + 1e-7 < best_obj) {
                routes.swap(trial);
                best_obj = obj;
                improved = true;
            }
        }
        if (improved) stagnant_passes = 0;
        else stagnant_passes++;
    }
    return routes;
}

static bool directedHasCycleDFS(
    int u,
    const unordered_map<int, vector<int>>& adj,
    unordered_map<int,int>& color
) {
    color[u] = 1;
    auto it = adj.find(u);
    if (it != adj.end()) {
        for (int v : it->second) {
            if (color[v] == 1) return true;
            if (color[v] == 0 && directedHasCycleDFS(v, adj, color)) return true;
        }
    }
    color[u] = 2;
    return false;
}

static bool hasDirectedCycle(const set<pair<int,int>>& directed_edges) {
    unordered_map<int, vector<int>> adj;
    unordered_map<int,int> color;
    for (auto [u, v] : directed_edges) {
        adj[u].push_back(v);
        color[u] = 0;
        color[v] = 0;
    }
    for (auto& kv : color) {
        if (kv.second == 0 && directedHasCycleDFS(kv.first, adj, color)) return true;
    }
    return false;
}

static vector<pair<int,int>> orientedEdgesFromPath(const Path& p) {
    vector<pair<int,int>> out;
    for (size_t i = 1; i < p.nodes.size(); ++i) out.push_back({p.nodes[i - 1], p.nodes[i]});
    return out;
}

static bool canAddToLayer(
    const LayerGraph& layer,
    const Path& p,
    int* father_conflicts = nullptr,
    bool* cycle_conflict = nullptr
) {
    if (father_conflicts) *father_conflicts = 0;
    if (cycle_conflict) *cycle_conflict = false;
    set<pair<int,int>> new_edges = layer.directed_edges;
    unordered_map<int, set<int>> new_parents = layer.parents;

    for (auto [u, v] : orientedEdgesFromPath(p)) {
        if (new_edges.count({v, u})) {
            if (cycle_conflict) *cycle_conflict = true;
            return false;
        }
        new_edges.insert({u, v});
        new_parents[v].insert(u);
        if (new_parents[v].size() > 1) {
            if (father_conflicts) (*father_conflicts)++;
            return false;
        }
    }
    if (hasDirectedCycle(new_edges)) {
        if (cycle_conflict) *cycle_conflict = true;
        return false;
    }
    return true;
}

static void addToLayer(LayerGraph& layer, const Path& p) {
    for (auto [u, v] : orientedEdgesFromPath(p)) {
        layer.directed_edges.insert({u, v});
        layer.parents[v].insert(u);
    }
}

static LayerRepairResult layerAwareRepair(
    const vector<CablePair>& pairs,
    const vector<Path>& routes,
    int copy_num
) {
    LayerRepairResult rr;
    rr.pair_to_layer.assign(pairs.size(), -1);
    vector<int> order(pairs.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b) {
        if (fabs(pairs[a].demand - pairs[b].demand) > EPS) return pairs[a].demand > pairs[b].demand;
        return routes[a].length > routes[b].length;
    });

    vector<LayerGraph> layers;
    for (int kk : order) {
        if (!routes[kk].feasible) {
            rr.conflict_pairs.push_back(kk);
            continue;
        }
        bool placed = false;
        int local_father_conf = 0;
        bool local_cycle_conf = false;
        for (int lid = 0; lid < static_cast<int>(layers.size()); ++lid) {
            int fc = 0;
            bool cyc = false;
            if (canAddToLayer(layers[lid], routes[kk], &fc, &cyc)) {
                addToLayer(layers[lid], routes[kk]);
                rr.pair_to_layer[kk] = lid;
                placed = true;
                break;
            }
            local_father_conf += fc;
            local_cycle_conf = local_cycle_conf || cyc;
        }
        if (!placed && static_cast<int>(layers.size()) < copy_num) {
            LayerGraph ng;
            addToLayer(ng, routes[kk]);
            layers.push_back(std::move(ng));
            rr.pair_to_layer[kk] = static_cast<int>(layers.size()) - 1;
            placed = true;
        }
        if (!placed) {
            rr.conflict_pairs.push_back(kk);
            rr.only_father_conflicts += max(1, local_father_conf);
            if (local_cycle_conf) rr.cycle_conflicts += 1;
        }
    }
    rr.used_layers = static_cast<int>(layers.size());
    rr.feasible = rr.conflict_pairs.empty() && rr.used_layers <= copy_num;
    return rr;
}

static vector<double> updatePenaltyFromConflicts(
    const GraphData& g,
    const vector<Path>& routes,
    const vector<int>& conflict_pairs,
    vector<double> penalty,
    int round
) {
    double factor = 0.5 + 0.25 * round;
    for (int kk : conflict_pairs) {
        for (int eid : routes[kk].edge_ids) {
            penalty[eid] += factor * g.edges[eid].w;
        }
    }
    return penalty;
}

static Solution solveOneCandidate(
    const GraphData& g,
    const vector<CablePair>& pairs,
    const vector<vector<Path>>& alpha_candidates,
    double wB,
    int init_id,
    int copy_num,
    int max_passes,
    int max_repair_rounds
) {
    vector<double> penalty(g.edges.size(), 0.0);
    vector<Path> routes = constructInitialRoutes(g, pairs, alpha_candidates, wB, init_id);
    routes = runHRH(g, pairs, routes, wB, max_passes, penalty);

    LayerRepairResult rr = layerAwareRepair(pairs, routes, copy_num);
    int repair_rounds = 0;
    while (!rr.feasible && repair_rounds < max_repair_rounds) {
        penalty = updatePenaltyFromConflicts(g, routes, rr.conflict_pairs, penalty, repair_rounds + 1);
        // Rerun HRH under conflict penalties. This keeps bundling but discourages illegal layer patterns.
        routes = runHRH(g, pairs, routes, wB, max(5, max_passes / 3), penalty);
        rr = layerAwareRepair(pairs, routes, copy_num);
        repair_rounds++;
    }

    Solution sol;
    sol.wB = wB;
    sol.init_id = init_id;
    sol.routes = std::move(routes);
    sol.objective = computeObjective(g, pairs, sol.routes, wB, &sol.cable_length, &sol.bundle_length);
    sol.used_layers = rr.used_layers;
    sol.feasible = rr.feasible;
    sol.pair_to_layer = rr.pair_to_layer;
    sol.repair_rounds = repair_rounds;
    sol.conflict_count = static_cast<int>(rr.conflict_pairs.size());
    return sol;
}

static vector<double> parseWBList(const string& s) {
    vector<double> vals;
    for (string item : split(s, ',')) {
        item = trim(item);
        if (!item.empty()) vals.push_back(stod(item));
    }
    if (vals.empty()) vals = {0.0, 0.05, 0.10, 0.20, 0.35};
    for (double v : vals) {
        if (v < -EPS || v > 1.0 + EPS) throw runtime_error("wB must be in [0,1]");
    }
    return vals;
}

static string nodeName(const GraphData& g, int id) {
    if (id >= 0 && id < static_cast<int>(g.id_to_node_name.size())) return g.id_to_node_name[id];
    return "#" + to_string(id);
}

static string joinNodePath(const GraphData& g, const vector<int>& nodes) {
    string out;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i) out += "|";
        out += nodeName(g, nodes[i]);
    }
    return out;
}

static string joinEdgeNames(const GraphData& g, const vector<int>& eids) {
    string out;
    for (size_t i = 0; i < eids.size(); ++i) {
        if (i) out += "|";
        const Edge& e = g.edges[eids[i]];
        out += nodeName(g, e.u) + "-" + nodeName(g, e.v);
    }
    return out;
}

static void ensureDir(const string& outdir) {
    std::filesystem::create_directories(outdir);
}

static void writeOutputs(
    const string& outdir,
    const GraphData& g,
    const vector<CablePair>& pairs,
    const vector<Solution>& sols
) {
    ensureDir(outdir);
    string summary_path = outdir + "/candidates_summary.csv";
    ofstream fs(summary_path);
    fs << "rank,wB,init_id,feasible,objective,cable_length,bundle_length,shared_edge_ratio,used_layers,repair_rounds,conflict_count\n";
    for (size_t r = 0; r < sols.size(); ++r) {
        unordered_set<int> used;
        int edge_instances = 0;
        for (const auto& p : sols[r].routes) {
            edge_instances += static_cast<int>(p.edge_ids.size());
            for (int eid : p.edge_ids) used.insert(eid);
        }
        double ratio = edge_instances > 0 ? 1.0 - static_cast<double>(used.size()) / edge_instances : 0.0;
        fs << r << "," << sols[r].wB << "," << sols[r].init_id << "," << (sols[r].feasible ? 1 : 0)
           << "," << fixed << setprecision(6) << sols[r].objective
           << "," << sols[r].cable_length
           << "," << sols[r].bundle_length
           << "," << ratio
           << "," << sols[r].used_layers
           << "," << sols[r].repair_rounds
           << "," << sols[r].conflict_count << "\n";
    }
    fs.close();

    if (sols.empty()) return;
    const Solution& best = sols.front();
    ofstream fp(outdir + "/best_center_paths.csv");
    fp << "pair_id,layer,demand,center_start,center_end,path_length,node_path,edge_path\n";
    for (size_t k = 0; k < pairs.size(); ++k) {
        fp << k << "," << (k < best.pair_to_layer.size() ? best.pair_to_layer[k] : -1)
           << "," << pairs[k].demand
           << "," << nodeName(g, pairs[k].center_s)
           << "," << nodeName(g, pairs[k].center_t)
           << "," << best.routes[k].length
           << "," << joinNodePath(g, best.routes[k].nodes)
           << "," << joinEdgeNames(g, best.routes[k].edge_ids) << "\n";
    }
    fp.close();

    ofstream fe(outdir + "/best_layer_edges.csv");
    fe << "layer,from,to,edge_name,edge_weight\n";
    set<tuple<int,int,int>> wrote;
    for (size_t k = 0; k < pairs.size(); ++k) {
        int layer = (k < best.pair_to_layer.size() ? best.pair_to_layer[k] : -1);
        if (layer < 0) continue;
        const Path& p = best.routes[k];
        for (size_t i = 0; i < p.edge_ids.size(); ++i) {
            int u = p.nodes[i];
            int v = p.nodes[i + 1];
            int eid = p.edge_ids[i];
            auto key = make_tuple(layer, u, v);
            if (wrote.count(key)) continue;
            wrote.insert(key);
            fe << layer << "," << nodeName(g, u) << "," << nodeName(g, v)
               << "," << nodeName(g, g.edges[eid].u) << "-" << nodeName(g, g.edges[eid].v)
               << "," << g.edges[eid].w << "\n";
        }
    }
    fe.close();

    ofstream ff(outdir + "/best_full_leaf_paths.csv");
    ff << "center_pair_id,original_leaf_start,original_leaf_end,layer,center_start,center_end,center_node_path\n";
    for (size_t k = 0; k < pairs.size(); ++k) {
        for (auto [ls, lt] : pairs[k].original_leaf_pairs) {
            ff << k << "," << nodeName(g, ls) << "," << nodeName(g, lt)
               << "," << (k < best.pair_to_layer.size() ? best.pair_to_layer[k] : -1)
               << "," << nodeName(g, pairs[k].center_s)
               << "," << nodeName(g, pairs[k].center_t)
               << "," << joinNodePath(g, best.routes[k].nodes) << "\n";
        }
    }
    ff.close();

    cerr << "Wrote outputs to " << outdir << "\n";
}

int main(int argc, char** argv) {
    try {
        string edge_csv = argc > 1 ? argv[1] : "data/edges-4.csv";
        string pair_csv = argc > 2 ? argv[2] : "data/pairs-4-246.csv";
        int copy_num = argc > 3 ? stoi(argv[3]) : 3;
        string wb_str = argc > 4 ? argv[4] : "0,0.05,0.1,0.2,0.35";
        double alpha = argc > 5 ? stod(argv[5]) : 1.2;
        int nPhi = argc > 6 ? stoi(argv[6]) : 7;
        int nInit = argc > 7 ? stoi(argv[7]) : 5;
        int maxPasses = argc > 8 ? stoi(argv[8]) : 80;
        int maxRepair = argc > 9 ? stoi(argv[9]) : 30;
        string outdir = argc > 10 ? argv[10] : "result_layered_hrh";

        cerr << "edge_csv=" << edge_csv << "\n";
        cerr << "pair_csv=" << pair_csv << "\n";
        cerr << "copy_num=" << copy_num << ", alpha=" << alpha << ", nPhi=" << nPhi
             << ", nInit=" << nInit << ", maxPasses=" << maxPasses
             << ", maxRepair=" << maxRepair << "\n";

        GraphData g = readEdges(edge_csv);
        vector<CablePair> pairs = readPairs(pair_csv, g);
        if (pairs.empty()) throw runtime_error("No valid center pairs loaded.");
        vector<double> wb_values = parseWBList(wb_str);

        cerr << "Generating alpha-shortest candidate paths...\n";
        auto alpha_candidates = generateAlphaCandidates(g, pairs, alpha, nPhi);
        int no_candidate = 0;
        for (const auto& v : alpha_candidates) if (v.empty()) no_candidate++;
        if (no_candidate > 0) cerr << "Warning: pairs without candidate path=" << no_candidate << "\n";

        vector<Solution> sols;
        for (double wB : wb_values) {
            cerr << "Solving wB=" << wB << "...\n";
            for (int init = 0; init < nInit; ++init) {
                Solution sol = solveOneCandidate(g, pairs, alpha_candidates, wB, init, copy_num, maxPasses, maxRepair);
                cerr << "  init=" << init
                     << " feasible=" << sol.feasible
                     << " obj=" << sol.objective
                     << " cable=" << sol.cable_length
                     << " bundle=" << sol.bundle_length
                     << " layers=" << sol.used_layers
                     << " conflicts=" << sol.conflict_count
                     << " repair=" << sol.repair_rounds << "\n";
                sols.push_back(std::move(sol));
            }
        }

        sort(sols.begin(), sols.end(), [](const Solution& a, const Solution& b) {
            if (a.feasible != b.feasible) return a.feasible > b.feasible;
            if (a.conflict_count != b.conflict_count) return a.conflict_count < b.conflict_count;
            if (fabs(a.objective - b.objective) > EPS) return a.objective < b.objective;
            return a.used_layers < b.used_layers;
        });

        writeOutputs(outdir, g, pairs, sols);
        if (!sols.empty()) {
            const auto& best = sols.front();
            cout << "Best candidate:\n";
            cout << "  feasible=" << (best.feasible ? 1 : 0) << "\n";
            cout << "  wB=" << best.wB << ", init=" << best.init_id << "\n";
            cout << "  objective=" << fixed << setprecision(6) << best.objective << "\n";
            cout << "  cable_length=" << best.cable_length << ", bundle_length=" << best.bundle_length << "\n";
            cout << "  used_layers=" << best.used_layers << ", repair_rounds=" << best.repair_rounds
                 << ", conflicts=" << best.conflict_count << "\n";
        }
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
