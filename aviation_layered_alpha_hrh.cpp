#include <algorithm>
#include <chrono>
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
 * A solver-free routing heuristic for the cable-harness routing problem (CHRP),
 * migrated from the aviation engineering-topology setting to the automotive
 * setting of Karlsson et al., "Automatic cable harness layout routing in a
 * customizable 3D environment" (2023).
 *
 * Objective (identical to the automotive CHRP, eqs. (1a)-(2) of the paper):
 *     min  f = wL * fL + wB * fB,        wL + wB = 1
 *     fL = sum_k demand_k * length(route_k)          (total cable length)
 *     fB = sum_{e used by any cable} c_e             (bundle / harness length)
 *
 * Extra manufacturability constraint inherited from the aviation MIP model
 * (scip_heur.cpp): the harness is split into at most `copy_num` layers, and the
 * edges used inside one layer must satisfy the acyclicity + only-father
 * constraints.  We use the following exact equivalence (proved from the MIP):
 *
 *     {acyclic} + {single-direction} + {only-father (in-degree <= 1)}
 *         <=>  the undirected set of edges used inside the layer is a FOREST.
 *
 * (Any forest can be oriented as a union of out-arborescences, giving
 *  in-degree <= 1 and a valid topological order; conversely an in-degree<=1
 *  acyclic digraph is a forest.)  This replaces the previous, incorrect
 *  travel-direction "parent" test which rejected legal layers and produced 0
 *  feasible solutions.
 *
 * Algorithm (per bundle weight wB):
 *   - Layer 0 is a "safe" layer routed on a global MST spanning tree, so every
 *     cable always has a feasible home (its unique MST path) -> feasibility is
 *     guaranteed by construction.
 *   - Layers 1..copy_num-1 are "free" layers grown as forests; a cable is routed
 *     in a free layer with a bundling-aware Dijkstra (paper Algorithm 3) while a
 *     union-find keeps each layer a forest.
 *   - Each cable is assigned to the layer/route of minimum marginal CHRP cost.
 *   - HRH improvement passes (paper Algorithm 2) re-optimise one cable at a time.
 *
 * For comparison we also run the pure automotive HRH (no layering) to obtain the
 * unconstrained CHRP reference objective and runtime.
 *
 * Build:
 *   g++ -std=c++17 -O2 aviation_layered_alpha_hrh.cpp -o aviation_layered_alpha_hrh
 *
 * Run:
 *   ./aviation_layered_alpha_hrh dataset/edges-4.csv dataset/pairs-4.csv \
 *        3 "0,0.05,0.1,0.2,0.35" 40 result_layered_hrh
 *   positional args: edge_csv pair_csv copy_num wb_list [max_passes] [outdir]
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
    double length = INF;      // base physical length, unweighted
    bool feasible = false;
};

struct CablePair {
    int id = -1;
    int leaf_s = -1;
    int leaf_t = -1;
    int center_s = -1;
    int center_t = -1;
    double demand = 1.0;
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

struct Solution {
    double wB = 0.0;
    vector<Path> routes;
    vector<int> pair_to_layer;
    double objective = INF;
    double cable_length = INF;
    double bundle_length = INF;
    int used_layers = 0;
    bool feasible = false;
    double seconds = 0.0;
    // automotive (non-layered) reference solved with the same wB.
    double ref_objective = INF;
    double ref_cable_length = INF;
    double ref_bundle_length = INF;
    double ref_seconds = 0.0;
};

// ---------------------------------------------------------------------------
// Union-find used to keep every layer's used-edge set acyclic (a forest).
// ---------------------------------------------------------------------------
struct UnionFind {
    vector<int> parent, rank_;
    void init(int n) {
        parent.resize(n);
        rank_.assign(n, 0);
        iota(parent.begin(), parent.end(), 0);
    }
    int find(int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    bool connected(int a, int b) { return find(a) == find(b); }
    bool unite(int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra == rb) return false;
        if (rank_[ra] < rank_[rb]) swap(ra, rb);
        parent[rb] = ra;
        if (rank_[ra] == rank_[rb]) rank_[ra]++;
        return true;
    }
};

// A layer holds the multiset of edges used by the cables assigned to it.
struct Layer {
    bool is_mst = false;                  // layer 0 is restricted to MST edges
    unordered_map<int,int> edge_count;    // edge_id -> #cables using it in this layer
    UnionFind uf;                         // forest over node ids (free layers only)
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
        if (n1.empty() || n2.empty()) continue;
        double w = stod(trim(tok[4]));
        int u = getNodeId(g, n1);
        int v = getNodeId(g, n2);
        auto key = normEdge(u, v);
        if (g.undirected_edge_to_id.count(key)) {
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
            e.is_leaf_edge = true;
            g.leaf_edge_ids.push_back(eid);
        }
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

// Generic Dijkstra over the center graph with arbitrary per-edge costs.
// Edges with cost >= INF/2 are treated as forbidden.
static Path dijkstraPath(
    const GraphData& g,
    int source,
    int target,
    const vector<double>& edge_cost
) {
    Path res;
    if (source == target) {
        res.nodes = {source};
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
            double ce = edge_cost[eid];
            if (ce >= INF / 2) continue;
            double nd = du + ce;
            if (nd + EPS < dist[v]) {
                dist[v] = nd;
                prev_node[v] = u;
                prev_edge[v] = eid;
                pq.push({nd, v});
            }
        }
    }
    if (dist[target] >= INF / 2) return res;

    vector<int> rev_nodes, rev_edges;
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

static vector<double> baseCosts(const GraphData& g) {
    vector<double> c(g.edges.size(), INF);
    for (int eid : g.center_edge_ids) c[eid] = max(g.edges[eid].w, EPS);
    return c;
}

// CHRP objective: f = wL*fL + wB*fB, with fL the demand-weighted cable length
// and fB the bundle/harness length (each physical edge counted once if used by
// any cable); matches the automotive paper's eqs. (1a),(1b),(2).
//
// Because our dataset mixes tiny per-cable demands (~1e-6) with raw physical
// edge weights (~10-500), fL and fB differ by orders of magnitude. We therefore
// normalize each objective by a reference scale (fL0 = sum of demand-weighted
// shortest-path lengths, fB0 = MST/min-harness weight) so that wB genuinely
// trades cable length against bundling and the weighted sum is O(1). The raw fL
// and fB are still reported. Set fL0=fB0=1 to recover the un-normalized value.
static double computeObjective(
    const GraphData& g,
    const vector<CablePair>& pairs,
    const vector<Path>& routes,
    double wB,
    double fL0,
    double fB0,
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
    return wL * (cable_len / fL0) + wB * (bundle_len / fB0);
}

// ---------------------------------------------------------------------------
// MST spanning tree of the center graph (Kruskal) + tree-path routing.
// ---------------------------------------------------------------------------
struct MstTree {
    vector<vector<pair<int,int>>> adj;    // node -> (nbr, edge_id) over MST edges
    double total_weight = 0.0;            // sum of MST edge weights (min-harness scale)
};

static MstTree buildMST(const GraphData& g) {
    MstTree t;
    t.adj.assign(g.id_to_node_name.size(), {});
    vector<int> order = g.center_edge_ids;
    sort(order.begin(), order.end(), [&](int a, int b) {
        return g.edges[a].w < g.edges[b].w;
    });
    UnionFind uf;
    uf.init(static_cast<int>(g.id_to_node_name.size()));
    for (int eid : order) {
        const Edge& e = g.edges[eid];
        if (uf.unite(e.u, e.v)) {
            t.adj[e.u].push_back({e.v, eid});
            t.adj[e.v].push_back({e.u, eid});
            t.total_weight += e.w;
        }
    }
    return t;
}

// Unique path between s and t on the MST tree (BFS). Empty path if disconnected.
static Path treePath(const GraphData& g, const MstTree& t, int s, int target) {
    Path res;
    if (s == target) {
        res.nodes = {s};
        res.length = 0.0;
        res.feasible = true;
        return res;
    }
    int n = static_cast<int>(g.id_to_node_name.size());
    vector<int> prev_node(n, -1), prev_edge(n, -1);
    vector<char> seen(n, 0);
    queue<int> q;
    q.push(s);
    seen[s] = 1;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        if (u == target) break;
        for (auto [v, eid] : t.adj[u]) {
            if (!seen[v]) {
                seen[v] = 1;
                prev_node[v] = u;
                prev_edge[v] = eid;
                q.push(v);
            }
        }
    }
    if (!seen[target]) return res;
    vector<int> rn, re;
    int cur = target;
    rn.push_back(cur);
    while (cur != s) {
        re.push_back(prev_edge[cur]);
        cur = prev_node[cur];
        rn.push_back(cur);
    }
    reverse(rn.begin(), rn.end());
    reverse(re.begin(), re.end());
    res.nodes = std::move(rn);
    res.edge_ids = std::move(re);
    res.length = 0.0;
    for (int eid : res.edge_ids) res.length += g.edges[eid].w;
    res.feasible = true;
    return res;
}

// ---------------------------------------------------------------------------
// Layered heuristic core.
// ---------------------------------------------------------------------------
static void rebuildLayerUF(const GraphData& g, Layer& L) {
    L.uf.init(static_cast<int>(g.id_to_node_name.size()));
    for (auto& [eid, cnt] : L.edge_count) {
        if (cnt > 0) L.uf.unite(g.edges[eid].u, g.edges[eid].v);
    }
}

// Marginal CHRP cost (and the resulting route) of placing cable k into layer L,
// given the global usage map.  Returns feasible=false if no forest-preserving
// route exists in this layer.
struct PlaceResult {
    Path route;
    double marginal = INF;
    bool feasible = false;
};

static PlaceResult placeInLayer(
    const GraphData& g,
    const MstTree& mst,
    const CablePair& cp,
    const Layer& L,
    const unordered_map<int,int>& global_count,
    double wB,
    double fL0,
    double fB0
) {
    PlaceResult pr;
    // Normalized marginal coefficients: cable term uses wL/fL0, bundle activation
    // term uses wB/fB0. This keeps the per-edge trade-off consistent with the
    // normalized objective so that varying wB actually moves along the Pareto
    // front (instead of bundle always dominating because of the tiny demands).
    double cl = (1.0 - wB) / fL0;
    double bl = wB / fB0;

    if (L.is_mst) {
        // Safe layer: route on the MST tree path. Used edges stay within the MST
        // (a tree), hence always a forest -> always feasible.
        Path p = treePath(g, mst, cp.center_s, cp.center_t);
        if (!p.feasible) return pr;
        double marg = 0.0;
        for (int eid : p.edge_ids) {
            double ce = g.edges[eid].w;
            bool in_layer = L.edge_count.count(eid) && L.edge_count.at(eid) > 0;
            bool in_global = global_count.count(eid) && global_count.at(eid) > 0;
            marg += cl * cp.demand * ce;
            if (!in_layer && !in_global) marg += bl * ce;  // newly activated edge
        }
        pr.route = std::move(p);
        pr.marginal = marg;
        pr.feasible = true;
        return pr;
    }

    // Free layer: bundling-aware Dijkstra (paper Algorithm 3) with a forest guard.
    UnionFind uf = L.uf;  // local mutable copy (path-compressing find())
    vector<double> cost(g.edges.size(), INF);
    for (int eid : g.center_edge_ids) {
        double ce = g.edges[eid].w;
        bool in_layer = L.edge_count.count(eid) && L.edge_count.at(eid) > 0;
        if (in_layer) {
            // Reusing an existing layer edge adds no new edge -> no cycle risk,
            // and no new bundle activation in this layer.
            cost[eid] = cl * cp.demand * ce;
        } else {
            const Edge& e = g.edges[eid];
            // Forbid new edges whose endpoints are already connected in the layer
            // (they would close a cycle and break the forest property).
            if (uf.connected(e.u, e.v)) {
                cost[eid] = INF;
            } else {
                bool in_global = global_count.count(eid) && global_count.at(eid) > 0;
                cost[eid] = cl * cp.demand * ce + (in_global ? 0.0 : bl * ce);
            }
        }
    }
    Path p = dijkstraPath(g, cp.center_s, cp.center_t, cost);
    if (!p.feasible) return pr;

    // Verify the forest property: adding all new edges of the path must not
    // create a cycle (guards the rare multi-new-edge same-component case).
    UnionFind trial = uf;
    for (int eid : p.edge_ids) {
        if (L.edge_count.count(eid) && L.edge_count.at(eid) > 0) continue;
        const Edge& e = g.edges[eid];
        if (!trial.unite(e.u, e.v)) return pr;  // would create a cycle
    }

    double marg = 0.0;
    for (int eid : p.edge_ids) {
        double ce = g.edges[eid].w;
        bool in_layer = L.edge_count.count(eid) && L.edge_count.at(eid) > 0;
        bool in_global = global_count.count(eid) && global_count.at(eid) > 0;
        marg += cl * cp.demand * ce;
        if (!in_layer && !in_global) marg += bl * ce;
    }
    pr.route = std::move(p);
    pr.marginal = marg;
    pr.feasible = true;
    return pr;
}

static void addRouteToState(
    const GraphData& g,
    Layer& L,
    unordered_map<int,int>& global_count,
    const Path& route
) {
    for (int eid : route.edge_ids) {
        int before = L.edge_count[eid];
        L.edge_count[eid] = before + 1;
        if (before == 0 && !L.is_mst) {
            const Edge& e = g.edges[eid];
            L.uf.unite(e.u, e.v);
        }
        global_count[eid] += 1;
    }
}

static void removeRouteFromState(
    const GraphData& g,
    Layer& L,
    unordered_map<int,int>& global_count,
    const Path& route
) {
    bool topology_changed = false;
    for (int eid : route.edge_ids) {
        auto it = L.edge_count.find(eid);
        if (it != L.edge_count.end()) {
            it->second -= 1;
            if (it->second <= 0) {
                L.edge_count.erase(it);
                topology_changed = true;
            }
        }
        auto gt = global_count.find(eid);
        if (gt != global_count.end()) {
            gt->second -= 1;
            if (gt->second <= 0) global_count.erase(gt);
        }
    }
    if (topology_changed && !L.is_mst) rebuildLayerUF(g, L);
}

static Solution solveLayered(
    const GraphData& g,
    const vector<CablePair>& pairs,
    const MstTree& mst,
    double wB,
    int copy_num,
    int max_passes,
    double fL0,
    double fB0
) {
    auto t0 = chrono::steady_clock::now();
    int K = static_cast<int>(pairs.size());

    vector<Layer> layers(max(1, copy_num));
    for (int l = 0; l < static_cast<int>(layers.size()); ++l) {
        layers[l].is_mst = (l == 0);            // layer 0 is the safe MST layer
        if (!layers[l].is_mst) layers[l].uf.init(static_cast<int>(g.id_to_node_name.size()));
    }
    unordered_map<int,int> global_count;
    vector<Path> routes(K);
    vector<int> layer_of(K, -1);

    // Construction order: largest demand first, then longest shortest path.
    auto base = baseCosts(g);
    vector<double> sp_len(K, 0.0);
    for (int k = 0; k < K; ++k) {
        Path sp = dijkstraPath(g, pairs[k].center_s, pairs[k].center_t, base);
        sp_len[k] = sp.feasible ? sp.length : INF;
    }
    vector<int> order(K);
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b) {
        if (fabs(pairs[a].demand - pairs[b].demand) > EPS) return pairs[a].demand > pairs[b].demand;
        return sp_len[a] > sp_len[b];
    });

    auto assignBest = [&](int k) {
        PlaceResult best;
        int best_layer = -1;
        for (int l = 0; l < static_cast<int>(layers.size()); ++l) {
            PlaceResult pr = placeInLayer(g, mst, pairs[k], layers[l], global_count, wB, fL0, fB0);
            if (pr.feasible && pr.marginal + EPS < best.marginal) {
                best = std::move(pr);
                best_layer = l;
            }
        }
        if (best_layer < 0) {
            // Should never happen: the MST layer always offers a route.
            best = placeInLayer(g, mst, pairs[k], layers[0], global_count, wB, fL0, fB0);
            best_layer = 0;
        }
        routes[k] = best.route;
        layer_of[k] = best_layer;
        addRouteToState(g, layers[best_layer], global_count, routes[k]);
    };

    for (int k : order) assignBest(k);

    // HRH improvement passes (paper Algorithm 2): re-optimise one cable at a time.
    double best_obj = computeObjective(g, pairs, routes, wB, fL0, fB0);
    for (int pass = 0; pass < max_passes; ++pass) {
        bool improved = false;
        for (int k : order) {
            removeRouteFromState(g, layers[layer_of[k]], global_count, routes[k]);
            Path old_route = routes[k];
            int old_layer = layer_of[k];

            PlaceResult best;
            int best_layer = -1;
            for (int l = 0; l < static_cast<int>(layers.size()); ++l) {
                PlaceResult pr = placeInLayer(g, mst, pairs[k], layers[l], global_count, wB, fL0, fB0);
                if (pr.feasible && pr.marginal + EPS < best.marginal) {
                    best = std::move(pr);
                    best_layer = l;
                }
            }
            if (best_layer < 0) {
                routes[k] = old_route;
                layer_of[k] = old_layer;
                addRouteToState(g, layers[old_layer], global_count, routes[k]);
                continue;
            }
            routes[k] = best.route;
            layer_of[k] = best_layer;
            addRouteToState(g, layers[best_layer], global_count, routes[k]);

            double obj = computeObjective(g, pairs, routes, wB, fL0, fB0);
            if (obj + 1e-7 < best_obj) {
                best_obj = obj;
                improved = true;
            } else if (best_layer != old_layer || best.route.edge_ids != old_route.edge_ids) {
                // Revert if it did not strictly help the global objective.
                removeRouteFromState(g, layers[best_layer], global_count, routes[k]);
                routes[k] = old_route;
                layer_of[k] = old_layer;
                addRouteToState(g, layers[old_layer], global_count, routes[k]);
            }
        }
        if (!improved) break;
    }

    Solution sol;
    sol.wB = wB;
    sol.routes = std::move(routes);
    sol.pair_to_layer = layer_of;
    sol.objective = computeObjective(g, pairs, sol.routes, wB, fL0, fB0,
                                     &sol.cable_length, &sol.bundle_length);

    // Count actually-used layers and confirm feasibility (every layer a forest).
    set<int> used_layers;
    for (int k = 0; k < K; ++k) if (!sol.routes[k].edge_ids.empty()) used_layers.insert(layer_of[k]);
    for (int k = 0; k < K; ++k) if (sol.routes[k].edge_ids.empty()) used_layers.insert(layer_of[k]);
    sol.used_layers = static_cast<int>(used_layers.size());
    sol.feasible = true;
    for (const auto& L : layers) {
        if (L.is_mst) continue;
        UnionFind chk; chk.init(static_cast<int>(g.id_to_node_name.size()));
        for (auto& [eid, cnt] : L.edge_count) {
            if (cnt <= 0) continue;
            const Edge& e = g.edges[eid];
            if (!chk.unite(e.u, e.v)) { sol.feasible = false; break; }
        }
        if (!sol.feasible) break;
    }
    sol.seconds = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    return sol;
}

// ---------------------------------------------------------------------------
// Automotive reference: pure CHRP HRH (no layering, no forest constraint).
// This is the algorithm of Karlsson et al. (paper Algorithms 2-3) applied
// directly to our engineering topology, used only for comparison.
// ---------------------------------------------------------------------------
// One HRH local search (paper Algorithm 2/3) from a given initial routing.
static vector<Path> hrhLocalSearch(
    const GraphData& g,
    const vector<CablePair>& pairs,
    const vector<int>& order,
    vector<Path> routes,
    double wB,
    double fL0,
    double fB0,
    int max_passes
) {
    double cl = (1.0 - wB) / fL0;   // normalized cable coefficient
    double bl = wB / fB0;           // normalized bundle coefficient
    unordered_map<int,int> usage;
    for (const auto& r : routes)
        for (int eid : r.edge_ids) usage[eid]++;

    double best_obj = computeObjective(g, pairs, routes, wB, fL0, fB0);
    for (int pass = 0; pass < max_passes; ++pass) {
        bool improved = false;
        for (int k : order) {
            for (int eid : routes[k].edge_ids) if (--usage[eid] <= 0) usage.erase(eid);
            vector<double> cost(g.edges.size(), INF);
            for (int eid : g.center_edge_ids) {
                double ce = g.edges[eid].w;
                bool shared = usage.count(eid) && usage.at(eid) > 0;
                cost[eid] = cl * pairs[k].demand * ce + (shared ? 0.0 : bl * ce);
            }
            Path cand = dijkstraPath(g, pairs[k].center_s, pairs[k].center_t, cost);
            Path chosen = cand.feasible ? cand : routes[k];
            vector<Path> trial = routes;
            trial[k] = chosen;
            double obj = computeObjective(g, pairs, trial, wB, fL0, fB0);
            if (obj + 1e-7 < best_obj) {
                routes[k] = chosen;
                best_obj = obj;
                improved = true;
            }
            for (int eid : routes[k].edge_ids) usage[eid]++;
        }
        if (!improved) break;
    }
    return routes;
}

static void runPaperHRH(
    const GraphData& g,
    const vector<CablePair>& pairs,
    const MstTree& mst,
    double wB,
    int max_passes,
    double fL0,
    double fB0,
    vector<Path>& routes_out,
    double& seconds_out
) {
    auto t0 = chrono::steady_clock::now();
    int K = static_cast<int>(pairs.size());
    auto base = baseCosts(g);

    vector<int> order(K);
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b) {
        return pairs[a].demand > pairs[b].demand;
    });

    // The paper locally optimises several initial routings (it uses Lagrangian
    // dual solutions and heuristic constructions, Section 3.2-3.3) and keeps the
    // best.  We give the unconstrained reference its strongest fair shot by
    // running the HRH from two initialisations and keeping the better result:
    //   (A) shortest paths  -> good for low wB,
    //   (B) MST tree paths   -> a bundled start that lets the HRH reach a tree
    //                           backbone for high wB (a proxy for the paper's
    //                           Lagrangian/bundled initial routes).
    vector<Path> initA(K), initB(K);
    for (int k = 0; k < K; ++k) {
        initA[k] = dijkstraPath(g, pairs[k].center_s, pairs[k].center_t, base);
        initB[k] = treePath(g, mst, pairs[k].center_s, pairs[k].center_t);
        if (!initB[k].feasible) initB[k] = initA[k];
    }
    vector<Path> rA = hrhLocalSearch(g, pairs, order, std::move(initA), wB, fL0, fB0, max_passes);
    vector<Path> rB = hrhLocalSearch(g, pairs, order, std::move(initB), wB, fL0, fB0, max_passes);
    double oA = computeObjective(g, pairs, rA, wB, fL0, fB0);
    double oB = computeObjective(g, pairs, rB, wB, fL0, fB0);

    routes_out = (oA <= oB) ? std::move(rA) : std::move(rB);
    seconds_out = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
}

static vector<double> parseWBList(const string& s) {
    vector<double> vals;
    for (string item : split(s, ',')) {
        item = trim(item);
        if (!item.empty()) vals.push_back(stod(item));
    }
    if (vals.empty()) vals = {0.0, 0.05, 0.10, 0.20, 0.35};
    for (double v : vals)
        if (v < -EPS || v > 1.0 + EPS) throw runtime_error("wB must be in [0,1]");
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
    ofstream fs(outdir + "/comparison_summary.csv");
    fs << "wB,layered_feasible,layered_objective,layered_cable,layered_bundle,layered_layers,layered_seconds,"
          "paper_objective,paper_cable,paper_bundle,paper_seconds,gap_percent\n";
    for (const auto& s : sols) {
        double gap = (s.ref_objective > EPS)
            ? 100.0 * (s.objective - s.ref_objective) / s.ref_objective : 0.0;
        fs << s.wB << "," << (s.feasible ? 1 : 0)
           << fixed << setprecision(6)
           << "," << s.objective << "," << s.cable_length << "," << s.bundle_length
           << "," << s.used_layers << "," << setprecision(4) << s.seconds
           << setprecision(6)
           << "," << s.ref_objective << "," << s.ref_cable_length << "," << s.ref_bundle_length
           << "," << setprecision(4) << s.ref_seconds
           << "," << setprecision(3) << gap << "\n";
    }
    fs.close();

    if (sols.empty()) return;
    // Pick the layered solution with the largest wB (most bundling) for detail dump.
    const Solution* bestp = &sols.front();
    for (const auto& s : sols) if (s.wB > bestp->wB) bestp = &s;
    const Solution& best = *bestp;

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
    fe << "layer,edge_name,edge_weight\n";
    set<pair<int,int>> wrote;
    for (size_t k = 0; k < pairs.size(); ++k) {
        int layer = (k < best.pair_to_layer.size() ? best.pair_to_layer[k] : -1);
        for (int eid : best.routes[k].edge_ids) {
            auto key = make_pair(layer, eid);
            if (wrote.count(key)) continue;
            wrote.insert(key);
            const Edge& e = g.edges[eid];
            fe << layer << "," << nodeName(g, e.u) << "-" << nodeName(g, e.v)
               << "," << e.w << "\n";
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
        string edge_csv = argc > 1 ? argv[1] : "dataset/edges-4.csv";
        string pair_csv = argc > 2 ? argv[2] : "dataset/pairs-4.csv";
        int copy_num   = argc > 3 ? stoi(argv[3]) : 3;
        string wb_str  = argc > 4 ? argv[4] : "0,0.05,0.1,0.2,0.35";
        int maxPasses  = argc > 5 ? stoi(argv[5]) : 40;
        string outdir  = argc > 6 ? argv[6] : "result_layered_hrh";

        cerr << "edge_csv=" << edge_csv << "\n";
        cerr << "pair_csv=" << pair_csv << "\n";
        cerr << "copy_num=" << copy_num << ", max_passes=" << maxPasses << "\n";

        GraphData g = readEdges(edge_csv);
        vector<CablePair> pairs = readPairs(pair_csv, g);
        if (pairs.empty()) throw runtime_error("No valid center pairs loaded.");
        vector<double> wb_values = parseWBList(wb_str);

        MstTree mst = buildMST(g);

        // Reference scales for normalizing the two objectives onto a comparable
        // footing (otherwise the ~1e-6 demands make fL negligible against fB and
        // the wB sweep degenerates into "always minimize the bundle").
        //   fL0 = sum of demand-weighted shortest-path lengths (min cable length)
        //   fB0 = MST total weight (a min-harness/bundle scale)
        auto base0 = baseCosts(g);
        double fL0 = 0.0;
        for (const auto& cp : pairs) {
            Path sp = dijkstraPath(g, cp.center_s, cp.center_t, base0);
            if (sp.feasible) fL0 += cp.demand * sp.length;
        }
        double fB0 = mst.total_weight;
        if (fL0 < EPS) fL0 = 1.0;
        if (fB0 < EPS) fB0 = 1.0;
        cerr << "Normalization scales: fL0(min cable)=" << fL0
             << ", fB0(MST harness)=" << fB0 << "\n";

        vector<Solution> sols;
        cout << fixed << setprecision(4);
        cout << "(obj is the normalized weighted sum wL*fL/fL0 + wB*fB/fB0; cable=fL, bundle=fB are raw)\n";
        cout << "wB      | layered: obj      cable      bundle     L  t(s)   | paper: obj      cable      bundle     t(s)   | gap%\n";
        cout << "--------+--------------------------------------------------+-------------------------------------------+------\n";
        for (double wB : wb_values) {
            Solution sol = solveLayered(g, pairs, mst, wB, copy_num, maxPasses, fL0, fB0);

            vector<Path> ref_routes;
            double ref_sec = 0.0;
            runPaperHRH(g, pairs, mst, wB, maxPasses, fL0, fB0, ref_routes, ref_sec);
            sol.ref_objective = computeObjective(g, pairs, ref_routes, wB, fL0, fB0,
                                                 &sol.ref_cable_length, &sol.ref_bundle_length);
            sol.ref_seconds = ref_sec;

            double gap = (sol.ref_objective > EPS)
                ? 100.0 * (sol.objective - sol.ref_objective) / sol.ref_objective : 0.0;
            cout << setw(7) << wB << " | "
                 << setw(11) << sol.objective << " " << setw(10) << sol.cable_length << " "
                 << setw(10) << sol.bundle_length << " " << setw(2) << sol.used_layers << " "
                 << setw(6) << sol.seconds << " | "
                 << setw(11) << sol.ref_objective << " " << setw(10) << sol.ref_cable_length << " "
                 << setw(10) << sol.ref_bundle_length << " " << setw(6) << sol.ref_seconds << " | "
                 << setprecision(2) << gap << setprecision(4) << "\n";
            sols.push_back(std::move(sol));
        }

        writeOutputs(outdir, g, pairs, sols);
        cout << "\nNote: 'layered' = our manufacturable heuristic (<=copy_num forest layers,\n"
                "satisfying the aviation acyclic/only-father constraints). 'paper' = the\n"
                "automotive CHRP HRH (Karlsson et al.) with no structural constraints, used as\n"
                "an unconstrained lower-reference. gap% = (layered-paper)/paper.\n";
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
