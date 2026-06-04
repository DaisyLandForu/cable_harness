#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <string>
#include <utility>
#include <set>
#include <queue>
#include <ctime>
#include <iomanip>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <stack>
#include <cmath>
#include <climits>
#include <algorithm>
#include <scip/scip.h>
#include <scip/scipdefplugins.h>
#include <scip/struct_scip.h>
// #include <xlsxwriter.h>

using namespace std;

int copy_num_dflt = 3;
int copy_num = copy_num_dflt;
int unfix_copy_num = 0;
int fix_copy_num = copy_num - unfix_copy_num;

string scale = "1.0";
char center_prefix = 'N';
int thread_num = 16;
double gap = 0.0;

template<typename Container>
std::map<int, std::set<int>> convertToAdjacencyList(const Container& edges) {
    std::map<int, std::set<int>> adjacencyList;
    for (const auto& edge : edges) {
        adjacencyList[edge.first].insert(edge.second);
        adjacencyList[edge.second].insert(edge.first);
    }
    return adjacencyList;
}

std::vector<std::string> parseCSVLine(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ',')) {
        result.push_back(token);
    }
    return result;
}

template <typename... Args>
std::string generateVarName(const std::string& prefix, Args... args) {
    std::ostringstream oss;
    oss << prefix;
    ((oss << "_" << args), ...);
    return oss.str();
}

int primeid(int i, int p) {
    return i * copy_num + p - 1;
}

int read_csv_to_edges(std::string file_path1, 
                        std::map<std::string, int>& node_name_to_id, 
                        std::map<int, std::string>& id_to_node_name,
                        std::vector<std::pair<int, int>>& edges, 
                        std::map<std::pair<int, int>, int>& edges_to_weight, 
                        std::set<int>& center_nodes, 
                        std::vector<std::pair<int, int>>& center_edges, 
                        std::vector<std::pair<int, int>>& leaf_edges, 
                        std::map<int, int>& leaf_to_center, 
                        std::vector<std::pair<int, int>>& entry_edges) {
    std::ifstream file1(file_path1);
    std::string line;

    int next_node_id = 0;

    if (file1.is_open()) {
        std::getline(file1, line);
        while (std::getline(file1, line)) {
            auto tokens = parseCSVLine(line);
            if (tokens.size() >= 5) {
                std::string node1_name = tokens[2];
                std::string node2_name = tokens[3];
                int weight = std::stoi(tokens[4]);

                if (node_name_to_id.find(node1_name) == node_name_to_id.end()) {
                    node_name_to_id[node1_name] = next_node_id;
                    id_to_node_name[next_node_id] = node1_name;
                    next_node_id++;
                }
                if (node_name_to_id.find(node2_name) == node_name_to_id.end()) {
                    node_name_to_id[node2_name] = next_node_id;
                    id_to_node_name[next_node_id] = node2_name;
                    next_node_id++;
                }

                int node1_id = node_name_to_id[node1_name];
                int node2_id = node_name_to_id[node2_name];
                
                auto edge = (node1_id < node2_id) ? std::make_pair(node1_id, node2_id) : std::make_pair(node2_id, node1_id);
                // auto edge = std::make_pair(node1_id, node2_id);
                edges.push_back(edge);
                edges_to_weight[edge] = weight;

                int c_count = 0;
                int center, leaf = 0;
                int node1_count = 0;
                int node2_count = 0;
                if (node1_name.front() == 'N' || node1_name.front() == 'M' ) {
                    node1_count = 3;
                    center = node1_id;
                    center_nodes.insert(node1_id);
                } else if (node1_name.front() == 'E') {
                    node1_count = 1;
                    center = node1_id;
                    center_nodes.insert(node1_id);
                } else {
                    node1_count = 0;
                    leaf = node1_id;
                }
                if (node2_name.front() == 'N' || node2_name.front() == 'M' ) {
                    node2_count = 3;
                    center = node2_id;
                    center_nodes.insert(node2_id);
                } else if (node2_name.front() == 'E') {
                    node2_count = 1;
                    center = node2_id;
                    center_nodes.insert(node2_id);
                } else {
                    node2_count = 0;
                    leaf = node2_id;
                }
                c_count = node1_count + node2_count;

                if (c_count == 6) {
                    center_edges.emplace_back(edge);
                } else if (c_count == 4) {
                    center_edges.emplace_back(edge);
                    entry_edges.emplace_back(edge);
                } else if (c_count == 3 || c_count == 1) {
                    leaf_edges.emplace_back(edge);
                    leaf_to_center[leaf] = center;
                } else {
                    cout << "edge error" << endl;
                }

                // int node1_count = std::count_if(
                //     node1_name.begin(),
                //     node1_name.end(),
                //     [](char c) { return c == 'N' || c == 'E' || c == 'M'; }
                // );
                // if (node1_count > 0) {
                //     center_nodes.insert(node1_id);
                //     c_count++;
                //     center = node1_id;
                // } else {
                //     leaf = node1_id;
                // }
                // int node2_count = std::count_if(
                //     node2_name.begin(),
                //     node2_name.end(),
                //     [](char c) { return c == 'N' || c == 'E' || c == 'M'; }
                // );
                // if (node2_count > 0) {
                //     center_nodes.insert(node2_id);
                //     c_count++;
                //     center = node2_id;
                // } else {
                //     leaf = node2_id;
                // }

                // if (count(node1_name.begin(), node1_name.end(), center_prefix) > 0) {
                //     center_nodes.insert(node1_id);
                //     c_count++;
                //     center = node1_id;
                // } else {
                //     leaf = node1_id;
                // }
                // if (count(node2_name.begin(), node2_name.end(), center_prefix) > 0) {
                //     center_nodes.insert(node2_id);
                //     c_count++;
                //     center = node2_id;
                // } else {
                //     leaf = node2_id;
                // }

                // if (c_count == 2) {
                //     center_edges.push_back(edge);
                // } else if (c_count == 1) {
                //     leaf_edges.push_back(edge);
                //     leaf_to_center[leaf] = center;
                // } else {
                //     cout << "edge error" << endl;
                // }
            }
        }
        file1.close();
        return next_node_id;
    }
    else {
        std::cerr << "file open fail" << file_path1 << std::endl;
        return -1;
    }
}

int read_csv_to_pairs(std::string file_path2, 
                        std::map<std::string, int>& node_name_to_id,
                        std::vector<std::pair<int, int>>& pairs, 
                        std::vector<std::pair<int, int>>& ori_pairs, 
                        std::map<std::pair<int, int>, double>& pairs_to_weight, 
                        std::map<std::pair<int, int>, std::pair<int, int>>& pairs_to_center_pairs, 
                        std::vector<std::pair<int, int>>& center_pairs, 
                        std::map<int, std::pair<int, int>>& id_to_center_pairs, 
                        std::map<std::pair<int, int>, int>& center_pairs_to_id,
                        std::map<std::pair<int, int>, double>& center_pairs_to_weight, 
                        std::map<int, int>& leaf_to_center,
                        std::vector<std::pair<int, int>>& entry_edges,
                        std::map<int, std::vector<std::pair<int, int>>>& cpid_to_entry_edges) {
    std::ifstream file2(file_path2);
    std::string line;
    auto entry_adjlist = convertToAdjacencyList(entry_edges);
    int cpid = 0;
    if (file2.is_open()) {
        std::getline(file2, line);
        while (std::getline(file2, line)) {
            auto tokens = parseCSVLine(line);
            if (tokens.size() >= 5) {
                // std::string node1_name = tokens[3];
                // std::string node2_name = tokens[5];
                // double weight = std::stod(tokens[9]);   
                // 3 5 9       3 4 5
                std::string node1_name = tokens[3];
                std::string node2_name = tokens[4];
                double weight = std::stod(tokens[5]); 

                if (node_name_to_id.find(node1_name) == node_name_to_id.end()) {
                    return -1;
                }
                if (node_name_to_id.find(node2_name) == node_name_to_id.end()) {
                    return -1;
                }

                int node1_id = node_name_to_id[node1_name];
                int node2_id = node_name_to_id[node2_name];
                ori_pairs.push_back(std::make_pair(node1_id, node2_id));

                auto pair = (node1_id < node2_id) ? std::make_pair(node1_id, node2_id) : std::make_pair(node2_id, node1_id);
                // auto pair = std::make_pair(node1_id, node2_id);
                pairs.push_back(pair);
                pairs_to_weight[pair] = weight;

                int cn1id = leaf_to_center[node1_id];
                int cn2id = leaf_to_center[node2_id];
                auto c_pair = (cn1id < cn2id) ? std::make_pair(cn1id, cn2id) : std::make_pair(cn2id, cn1id);
                pairs_to_center_pairs[pair] = c_pair;
                if (std::find(center_pairs.begin(), center_pairs.end(), c_pair) == center_pairs.end()) {
                    center_pairs.emplace_back(c_pair);
                    id_to_center_pairs[cpid] = c_pair;
                    center_pairs_to_id[c_pair] = cpid;
                    if (entry_adjlist.find(cn1id) != entry_adjlist.end()) {
                        for (const auto& nid : entry_adjlist[cn1id]) {
                            auto edge = (nid < cn1id) ? std::make_pair(nid, cn1id) : std::make_pair(cn1id, nid);
                            cpid_to_entry_edges[cpid].emplace_back(edge);
                        }
                    }
                    if (entry_adjlist.find(cn2id) != entry_adjlist.end()) {
                        for (const auto& nid : entry_adjlist[cn2id]) {
                            auto edge = (nid < cn2id) ? std::make_pair(nid, cn2id) : std::make_pair(cn2id, nid);
                            cpid_to_entry_edges[cpid].emplace_back(edge);
                        }
                    }
                    cpid++;
                    center_pairs_to_weight[c_pair] = weight;
                } else {
                    center_pairs_to_weight[c_pair] += weight;
                }
                
            }
        }
        file2.close();
        return 0;
    }
    else {
        std::cerr << "file open fail" << file_path2 << std::endl;
        return -1;
    }
}

int set_dflt_copynums(std::map<std::string, int>& node_name_to_id, 
                        std::map<int, int>& id_to_copynums) {
    const int n = node_name_to_id.size();
    for (int i = 0; i < n; i++) {
        id_to_copynums[i] = copy_num_dflt;
    }
    return 0;
}

int read_csv_to_copynums(std::string file_path1, 
                            std::map<std::string, int>& node_name_to_id, 
                            std::map<int, int>& id_to_copynums) {
    std::ifstream file1(file_path1);
    std::string line;

    if (file1.is_open()) {
        std::getline(file1, line);
        while (std::getline(file1, line)) {
            auto tokens = parseCSVLine(line);
            if (tokens.size() >= 2) {
                std::string node_name = tokens[0];
                int copynum = std::stoi(tokens[1]);

                if (node_name_to_id.find(node_name) == node_name_to_id.end()) {
                    return -1;
                }

                int node_id = node_name_to_id[node_name];
                id_to_copynums[node_id] = copynum;
            }
        }
        file1.close();
        return 0;
    }
    else {
        std::cerr << "file open fail" << file_path1 << std::endl;
        return -1;
    }
}

vector<pair<int, int>> getShortestPathEdges(
    const unordered_map<int, vector<pair<int, int>>>& graph, // 邻接表：节点 -> {邻居, 权重}
    int start, int end,
    const map<pair<int, int>, int>& edges_to_weight,
    vector<int>& path_nodes) {
    
    if (start == end) return {}; // 相同节点，没有边

    // 优先队列：存储 {距离, 节点}
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> pq;
    unordered_map<int, int> dist; // 到每个节点的最短距离
    unordered_map<int, int> parent; // 记录每个节点的前驱节点，用于重建路径
    unordered_set<int> visited;

    // 初始化距离
    dist[start] = 0;
    pq.push({0, start});
    parent[start] = -1;

    while (!pq.empty()) {
        int current = pq.top().second;
        int current_dist = pq.top().first;
        pq.pop();

        if (visited.find(current) != visited.end()) continue;
        visited.insert(current);

        if (current == end) break;

        if (graph.find(current) != graph.end()) {
            for (const auto& neighbor_info : graph.at(current)) {
                int neighbor = neighbor_info.first;
                int weight = neighbor_info.second;

                if (visited.find(neighbor) != visited.end()) continue;

                int new_dist = current_dist + weight;
                if (dist.find(neighbor) == dist.end() || new_dist < dist[neighbor]) {
                    dist[neighbor] = new_dist;
                    parent[neighbor] = current;
                    pq.push({new_dist, neighbor});
                }
            }
        }
    }

    // 如果无法到达终点，返回空路径
    if (dist.find(end) == dist.end() || dist[end] == INT_MAX) {
        return {};
    }

    // 重建节点路径
    vector<int> node_path;
    int node = end;
    while (node != -1) {
        node_path.push_back(node);
        node = parent[node];
    }
    reverse(node_path.begin(), node_path.end());
    path_nodes = node_path;

    // 收集边
    vector<pair<int, int>> edges;
    for (size_t i = 1; i < node_path.size(); i++) {
        int u = node_path[i-1];
        int v = node_path[i];
        edges.emplace_back(min(u, v), max(u, v));
    }
    return edges;

    // // 从终点回溯到起点，重建路径并收集边
    // vector<pair<int, int>> edges;
    // int node = end;
    // while (parent[node] != -1) {
    //     int prev = parent[node];
    //     // 确保边的小节点在前，以便去重时一致
    //     edges.emplace_back(min(prev, node), max(prev, node));
    //     node = prev;
    // }
    // return edges;
}

void processAllPairs(
    const vector<pair<int, int>>& edges,
    const map<pair<int, int>, int>& edges_to_weight,
    const vector<pair<int, int>>& pairs,
    std::map<std::pair<int, int>, std::pair<int, int>>& pairs_to_center_pairs,
    std::map<std::pair<int, int>, int>& center_pairs_to_id,
    std::map<int, std::set<std::pair<int, int>>>& cpid_to_sp_edges,
    std::vector<std::pair<int, int>>& entry_edges,
    std::map<int, std::vector<std::pair<int, int>>>& cpid_to_entry_edges) {

    // 构建有权图的邻接表
    // unordered_map<int, vector<pair<int, int>>> graph;
    // for (const auto& e : edges) {
    //     int u = e.first, v = e.second;
    //     int weight = edges_to_weight.at(e);
    //     graph[u].emplace_back(v, weight);
    //     graph[v].emplace_back(u, weight);
    // }

    // 处理每对节点
    for (size_t pair_id = 0; pair_id < pairs.size(); pair_id++) {
        const auto& p = pairs[pair_id];
        int start = p.first, end = p.second;
        int pid = center_pairs_to_id[p];

        unordered_map<int, vector<pair<int, int>>> graph;
        for (const auto& e : edges) {
            int u = e.first, v = e.second;
            int weight = edges_to_weight.at(e);
            if (std::find(entry_edges.begin(), entry_edges.end(), e) == entry_edges.end()) {
                graph[u].emplace_back(v, weight);
                graph[v].emplace_back(u, weight);
            } else {
                if (u == start || u == end || v == start || v == end) {
                    graph[u].emplace_back(v, weight);
                    graph[v].emplace_back(u, weight);
                }
                // auto save_edges = cpid_to_entry_edges[pid];
                // if (std::find(save_edges.begin(), save_edges.end(), e) != save_edges.end()) {
                //     graph[u].emplace_back(v, weight);
                //     graph[v].emplace_back(u, weight);
                // }
            }          
        }

        vector<int> path_nodes;
        auto path_edges = getShortestPathEdges(graph, start, end, edges_to_weight, path_nodes);
        cpid_to_sp_edges[pid].insert(path_edges.begin(), path_edges.end());
    }
}

std::optional<std::vector<int>> detectCycle(const std::map<int, std::set<int>>& graph) {
    std::unordered_set<int> visited;
    std::stack<int> pathStack;
    auto dfs = [&](int node, int parent, auto& dfsRef) -> std::optional<std::vector<int>> {
        visited.insert(node);
        pathStack.push(node);
        for (int neighbor : graph.at(node)) {
            if (neighbor == parent) {
                continue;
            }
            if (visited.find(neighbor) != visited.end()) {
                std::vector<int> cyclePath;
                while (!pathStack.empty()) {
                    int topNode = pathStack.top();
                    cyclePath.push_back(topNode);
                    pathStack.pop();

                    if (topNode == neighbor) {
                        break;
                    }
                }
                return cyclePath;
            } else {
                auto result = dfsRef(neighbor, node, dfsRef);
                if (result) {
                    return result;
                }
            }
        }
        pathStack.pop();
        return std::nullopt;
    };

    for (const auto& [node, _] : graph) {
        if (visited.find(node) == visited.end()) {
            auto result = dfs(node, -1, dfs);
            if (result) {
                return result;
            }
        }
    }

    return std::nullopt;
}

bool hasCycleInUndirectedGraph(std::set<std::pair<int, int>>& edges) {
    if (edges.empty()) return false;
    
    std::unordered_map<int, std::unordered_set<int>> graph;
    std::unordered_map<int, int> degree;
    
    for (auto& edge : edges) {
        int u = edge.first;
        int v = edge.second;
        
        graph[u].insert(v);
        graph[v].insert(u);
        
        degree[u]++;
        degree[v]++;
    }
    
    std::queue<int> q;
    
    for (auto& [node, deg] : degree) {
        if (deg == 1) {
            q.push(node);
        }
    }
    
    while (!q.empty()) {
        int current = q.front();
        q.pop();

        for (int neighbor : graph[current]) {
            degree[neighbor]--;
            
            if (degree[neighbor] == 1) {
                q.push(neighbor);
            }
            
            graph[neighbor].erase(current);
        }
        
        graph.erase(current);
        degree.erase(current);
    }
    
    return !degree.empty();
}

std::map<int, std::vector<int>> fixPairs(
    std::vector<std::pair<int, int>> center_pairs,
    const std::map<std::pair<int, int>, double>& center_pairs_to_weight,
    const std::map<std::pair<int, int>, int>& center_pairs_to_id,
    const std::map<int, std::set<std::pair<int, int>>>& cpid_to_sp_edges) {
    
    std::vector<std::pair<int, int>> sorted_cpairs(
        center_pairs.begin(), center_pairs.end()
    );
    
    // 排序中心对
    std::sort(sorted_cpairs.begin(), sorted_cpairs.end(),
        [&](const auto& a, const auto& b) {
            double weight_a = center_pairs_to_weight.count(a) ? center_pairs_to_weight.at(a) : 0.0;
            double weight_b = center_pairs_to_weight.count(b) ? center_pairs_to_weight.at(b) : 0.0;
            if (weight_a == weight_b) {
                int len_a = cpid_to_sp_edges.at(center_pairs_to_id.at(a)).size();
                int len_b = cpid_to_sp_edges.at(center_pairs_to_id.at(b)).size();
                return len_a > len_b;
            } else {
                return weight_a > weight_b;
            }
        }
    );

    std::map<int, std::vector<int>> cp_to_lockpids;
    int ttl_num = sorted_cpairs.size();
    
    // 处理固定副本数
    for (int i = 0; i < fix_copy_num; i++) {
        std::vector<std::pair<int, int>> new_sorted_cpairs;
        std::set<std::pair<int, int>> acyclic_graph;
        
        for (const auto& cpair : sorted_cpairs) {
            int pid = center_pairs_to_id.at(cpair);
            auto new_edges = cpid_to_sp_edges.at(pid);
            
            std::set<std::pair<int, int>> now_graph = acyclic_graph;
            now_graph.insert(new_edges.begin(), new_edges.end());
            
            if (!hasCycleInUndirectedGraph(now_graph)) {
                acyclic_graph = now_graph;
                cp_to_lockpids[i].emplace_back(pid);
            } else {
                new_sorted_cpairs.emplace_back(cpair);
            }
        }
        sorted_cpairs = new_sorted_cpairs;
    }
    
    // 输出统计信息
    int acy_num = 0;
    for (const auto& pair : cp_to_lockpids) {
        acy_num += pair.second.size();
    }
    
    std::cout << "total pair size: " << ttl_num << std::endl;
    std::cout << "lock pair size: " << acy_num << std::endl;
    std::cout << "lock per: " << (static_cast<double>(acy_num)/ttl_num) << std::endl;
    
    return cp_to_lockpids;
}


std::map<int, std::vector<int>> checkConflictPairs(
    std::vector<std::pair<int, int>>& center_pairs,
    const std::map<int, std::set<std::pair<int, int>>>& cpid_to_sp_edges) {

    std::map<int, std::vector<int>> cp_to_conflictpids;
    int pair_num = center_pairs.size();
    cout << "pair num: " << pair_num << endl;
    // assert(pair_num == cpid_to_sp_edges.size());
    for (int i = 0; i < pair_num-1; i++) {
        auto now_edges = cpid_to_sp_edges.at(i);
        std::set<std::pair<int, int>> acyclic_graph(now_edges.begin(), now_edges.end());
        for (int j = i+1; j < pair_num; j++) {
            auto new_edges = cpid_to_sp_edges.at(j);
            std::set<std::pair<int, int>> now_graph = acyclic_graph;
            now_graph.insert(new_edges.begin(), new_edges.end());
            if (hasCycleInUndirectedGraph(now_graph)) {
                cp_to_conflictpids[i].emplace_back(j);
                cp_to_conflictpids[j].emplace_back(i);
            }
        }
    }
    return cp_to_conflictpids;
}


double SolveMIPProblem(std::set<int> nodes, 
                        std::vector<std::pair<int, int>>& edges, 
                        std::map<std::pair<int, int>, int>& edges_to_weight,
                        std::map<int, std::pair<int, int>>& id_to_pairs, 
                        std::map<std::pair<int, int>, double>& pairs_to_weight,
                        std::map<int, std::set<int>>& adj_list, 
                        std::map<std::pair<int, int>, std::set<std::pair<int, int>>>& pairs_to_ret_edges_prime, 
                        std::set<std::pair<int, int>>& ret_edges_prime, 
                        std::map<int, int>& k2prime, 
                        std::map<int, int>& id_to_copynums,
                        std::map<int, std::set<std::pair<int, int>>>& cpid_to_sp_edges,
                        std::map<int, std::vector<int>>& cp_to_lockpids,
                        std::vector<std::pair<int, int>>& entry_edges,
                        std::map<int, std::vector<std::pair<int, int>>>& cpid_to_entry_edges) {
    // Initialize SCIP
    SCIP* scip = nullptr;
    SCIP_CALL(SCIPcreate(&scip));
    SCIP_CALL(SCIPincludeDefaultPlugins(scip));
    SCIP_CALL(SCIPcreateProbBasic(scip, "MIPProblem"));

    SCIP_CALL(SCIPsetIntParam(scip, "display/verblevel", 5));
    // Set parameters
    SCIP_CALL(SCIPsetRealParam(scip, "limits/gap", gap));
    // SCIP_CALL(SCIPsetBoolParam(scip, "branching/preferbinary", true));
    SCIP_CALL(SCIPsetIntParam(scip, "parallel/minnthreads", 8));
    SCIP_CALL(SCIPsetIntParam(scip, "parallel/maxnthreads", 16));
    // SCIP_CALL(SCIPsetIntParam(scip, "lp/threads", 4));

    SCIPsetIntParam(scip, "separating/maxrounds", 50);
    SCIPsetIntParam(scip, "separating/maxroundsroot", 100);
    SCIPsetIntParam(scip, "separating/maxcuts", 500);
    SCIPsetIntParam(scip, "separating/maxcutsroot", 1000);
    SCIPsetIntParam(scip, "separating/minefficacy", 0.00001);  // smaller than default 1e‑4 maybe
    SCIPsetIntParam(scip, "separating/maxstallrounds", 20);
    SCIPsetRealParam(scip, "separating/maxbounddist", 0.9);
    SCIPsetIntParam(scip, "display/verblevel", 5);
    SCIPsetIntParam(scip, "presolving/maxrounds", 500);
    SCIPsetBoolParam(scip, "heuristics/fastprimal", TRUE);
    
    // SCIP_CALL(SCIPsetIntParam(scip, "heuristics/rens/freq", 50));
    // SCIP_CALL(SCIPsetIntParam(scip, "heuristics/rens/priority", 100000));
    // SCIP_CALL(SCIPsetIntParam(scip, "heuristics/alns/freq", 50));
    // SCIP_CALL(SCIPsetIntParam(scip, "heuristics/alns/priority", 90000));
    // SCIP_CALL(SCIPsetIntParam(scip, "heuristics/undercover/freq", 20));
    // SCIP_CALL(SCIPsetIntParam(scip, "nodeselection/hybridestim/stdpriority", 500000));
    
    cout << "SCIP" << endl;
    cout << "mip_gap: " << gap << endl;

    const int K = id_to_pairs.size();
    const int e = edges.size();
    const int n = nodes.size();
    // const int lock_K = cp_to_lockpids.size();

    cout << "pairs size: " << K << endl; // 202
    cout << "nodes size: " << n << endl; // 556
    cout << "edges size: " << e << endl; // 564
    cout << "copy   num: " << copy_num << endl;

    // Variable storage
    map<tuple<int, int, int>, SCIP_VAR*> x;
    map<tuple<int, int, int>, SCIP_VAR*> f;
    map<tuple<int, int>, SCIP_VAR*> m;
    map<tuple<int, int>, SCIP_VAR*> y; // topo sort
    map<tuple<int, int, int>, SCIP_VAR*> z;

    // Create variables
    for (const auto& i : nodes) {
        for (int prime = 0; prime < copy_num; ++prime) {
            SCIP_VAR* var;
            SCIP_CALL(SCIPcreateVarBasic(scip, &var,
                generateVarName("y", i, prime).c_str(),
                0.0, 1.0, 0.0, SCIP_VARTYPE_CONTINUOUS));
            SCIP_CALL(SCIPaddVar(scip, var));
            y.emplace(make_tuple(i, prime), var);
        }
    }

    for (int k = 0; k < K; ++k) {
        for (int prime = 0; prime < copy_num; ++prime) {
            SCIP_VAR* var;
            SCIP_CALL(SCIPcreateVarBasic(scip, &var,
                generateVarName("m", k, prime).c_str(),
                0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
            SCIP_CALL(SCIPaddVar(scip, var));
            m.emplace(make_tuple(k, prime), var);
        }
    }

    for (const auto& edge : edges) {
        int i = edge.first;
        int j = edge.second;

        for (int k = 0; k < K; ++k) {
            SCIP_VAR* var_x;
            SCIP_CALL(SCIPcreateVarBasic(scip, &var_x,
                generateVarName("x", i, j, k).c_str(),
                0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
            SCIP_CALL(SCIPaddVar(scip, var_x));
            x.emplace(make_tuple(i, j, k), var_x);

            SCIP_VAR* var_f1;
            SCIP_CALL(SCIPcreateVarBasic(scip, &var_f1,
                generateVarName("f", i, j, k).c_str(),
                -1.0, 1.0, 0.0, SCIP_VARTYPE_CONTINUOUS));
            SCIP_CALL(SCIPaddVar(scip, var_f1));
            f.emplace(make_tuple(i, j, k), var_f1);

            SCIP_VAR* var_f2;
            SCIP_CALL(SCIPcreateVarBasic(scip, &var_f2,
                generateVarName("f", j, i, k).c_str(),
                -1.0, 1.0, 0.0, SCIP_VARTYPE_CONTINUOUS));
            SCIP_CALL(SCIPaddVar(scip, var_f2));
            f.emplace(make_tuple(j, i, k), var_f2);
        }

        for (int prime = 0; prime < copy_num; ++prime) {
            SCIP_VAR* var_z1;
            SCIP_CALL(SCIPcreateVarBasic(scip, &var_z1,
                generateVarName("z", i, j, prime).c_str(),
                0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
            SCIP_CALL(SCIPaddVar(scip, var_z1));
            z.emplace(make_tuple(i, j, prime), var_z1);

            SCIP_VAR* var_z2;
            SCIP_CALL(SCIPcreateVarBasic(scip, &var_z2,
                generateVarName("z", j, i, prime).c_str(),
                0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
            SCIP_CALL(SCIPaddVar(scip, var_z2));
            z.emplace(make_tuple(j, i, prime), var_z2);
        }
    }

    SCIP_SOL* sol = nullptr;
    SCIP_CALL(SCIPcreatePartialSol(scip, &sol, nullptr));
    for (const auto& [cp, pids] : cp_to_lockpids) {
        int prime = cp;
        for (const auto& pid : pids) {
            auto lock_edges = cpid_to_sp_edges[pid];
            int k = pid;
            for (const auto& edge : edges) {
                int i = edge.first;
                int j = edge.second;
                if (lock_edges.count(edge) > 0) {
                    SCIP_CALL(SCIPsetSolVal(scip, sol, x.at(make_tuple(i, j, k)), 1.0));
                    SCIP_CALL(SCIPsetSolVal(scip, sol, m.at(make_tuple(k, prime)), 1.0));
                } else {
                    SCIP_CALL(SCIPsetSolVal(scip, sol, x.at(make_tuple(i, j, k)), 0.0));
                }
            }
        }
    }
    SCIP_Bool stored;
    SCIP_CALL(SCIPaddSol(scip, sol, &stored));

    // for (const auto& [cp, pids] : cp_to_lockpids) {
    //     int prime = cp;
    //     for (const auto& pid : pids) {
    //         auto lock_edges = cpid_to_sp_edges[pid];
    //         int k = pid;
    //         for (const auto& edge : edges) {
    //             int i = edge.first;
    //             int j = edge.second;
    //             if (lock_edges.count(edge) > 0) {
    //                 SCIP_CONS* xlock = nullptr;
    //                 SCIP_CALL(SCIPcreateConsLinear(scip, &xlock, "xlock", 0, nullptr, nullptr,
    //                     1.0, 1.0, true, true, true, true, true, false, false, false, false, false));
    //                 SCIP_CALL(SCIPaddCoefLinear(scip, xlock, x.at(std::make_tuple(i, j, k)), 1.0));
    //                 SCIP_CALL(SCIPaddCons(scip, xlock));
    //                 SCIP_CONS* mlock = nullptr;
    //                 SCIP_CALL(SCIPcreateConsLinear(scip, &mlock, "mlock", 0, nullptr, nullptr,
    //                     1.0, 1.0, true, true, true, true, true, false, false, false, false, false));
    //                 SCIP_CALL(SCIPaddCoefLinear(scip, mlock, m.at(std::make_tuple(k, prime)), 1.0));
    //                 SCIP_CALL(SCIPaddCons(scip, mlock));
    //                 SCIP_CONS* zlock = nullptr;
    //                 SCIP_CALL(SCIPcreateConsLinear(scip, &zlock, "zlock", 0, nullptr, nullptr,
    //                     1.0, 1.0, true, true, true, true, true, false, false, false, false, false));
    //                 SCIP_CALL(SCIPaddCoefLinear(scip, zlock, z.at(std::make_tuple(i, j, prime)), 1.0));
    //                 SCIP_CALL(SCIPaddCoefLinear(scip, zlock, z.at(std::make_tuple(j, i, prime)), 1.0));
    //                 SCIP_CALL(SCIPaddCons(scip, zlock));
    //             } else {
    //                 SCIP_CONS* xlock = nullptr;
    //                 SCIP_CALL(SCIPcreateConsLinear(scip, &xlock, "xlock", 0, nullptr, nullptr,
    //                     0.0, 0.0, true, true, true, true, true, false, false, false, false, false));
    //                 SCIP_CALL(SCIPaddCoefLinear(scip, xlock, x.at(std::make_tuple(i, j, k)), 1.0));
    //                 SCIP_CALL(SCIPaddCons(scip, xlock));
    //             }
    //         }
    //     }
    // }

    // for (const auto& [id, pair] : id_to_pairs) {
    //     int k = id;
    //     int start = pair.first, end = pair.second;
    //     // auto save_edges = cpid_to_entry_edges[k];
    //     for (const auto& edge : entry_edges) {
    //         int i = edge.first;
    //         int j = edge.second;
    //         if (i == start || i == end || j == start || j == end) {
    //             continue;
    //         } else {
    //             SCIP_CONS* xforbid = nullptr;
    //             SCIP_CALL(SCIPcreateConsLinear(scip, &xforbid, "xforbid", 0, nullptr, nullptr,
    //                 0.0, 0.0, true, true, true, true, true, false, false, false, false, false));
    //             SCIP_CALL(SCIPaddCoefLinear(scip, xforbid, x.at(std::make_tuple(i, j, k)), 1.0));
    //             SCIP_CALL(SCIPaddCons(scip, xforbid));
    //         }
    //         // if (std::find(save_edges.begin(), save_edges.end(), edge) == save_edges.end()) {
    //         //     int i = edge.first;
    //         //     int j = edge.second;
    //         //     model.AddConstr(x.at(std::make_tuple(i, j, k)) == 0);
    //         // }
    //     }
    // }

    // Set objective
    for (int k = 0; k < K; ++k) {
        double pair_weight = pairs_to_weight[id_to_pairs[k]];
        for (const auto& edge : edges) {
            int i = edge.first;
            int j = edge.second;
            double coef = edges_to_weight[edge] * pair_weight;
            SCIP_CALL(SCIPchgVarObj(scip, x.at(std::make_tuple(i, j, k)), coef)); 
        }
    }
    SCIP_CALL(SCIPsetObjsense(scip, SCIP_OBJSENSE_MINIMIZE));
    
    // Connection constraints
    for (int k = 0; k < K; ++k) {
        int s = id_to_pairs.at(k).first;
        int t = id_to_pairs.at(k).second;

        // Flow balance constraints
        for (const auto& i : nodes) {
            double bound = (i == s) ? 1 : (i == t) ? -1 : 0;
            SCIP_CONS* cons = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &cons, "flow_balance", 0, nullptr, nullptr,
                bound, bound, true, true, true, true, true, false, false, false, false, false));

            for (const auto& j : adj_list.at(i)) {
                SCIP_CALL(SCIPaddCoefLinear(scip, cons, f.at(make_tuple(i, j, k)), 1.0));
            }

            SCIP_CALL(SCIPaddCons(scip, cons));
            // SCIP_CALL(SCIPreleaseCons(scip, &cons));
        }

        // Flow constraints for each edge
        for (const auto& edge : edges) {
            int i = edge.first;
            int j = edge.second;

            // Flow symmetry
            SCIP_CONS* sym_cons = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &sym_cons, "flow_symmetry", 0, nullptr, nullptr,
                0.0, 0.0, true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, sym_cons, f.at(make_tuple(i, j, k)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, sym_cons, f.at(make_tuple(j, i, k)), 1.0));
            SCIP_CALL(SCIPaddCons(scip, sym_cons));
            // SCIP_CALL(SCIPreleaseCons(scip, &sym_cons));

            // Capacity constraints
            SCIP_CONS* upper1 = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &upper1, "capacity_upper1", 0, nullptr, nullptr,
                -SCIPinfinity(scip), 0.0, true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, upper1, f.at(make_tuple(i, j, k)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, upper1, x.at(make_tuple(i, j, k)), -1.0));
            SCIP_CALL(SCIPaddCons(scip, upper1));
            // SCIP_CALL(SCIPreleaseCons(scip, &upper1));

            SCIP_CONS* lower1 = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &lower1, "capacity_lower1", 0, nullptr, nullptr,
                0.0, SCIPinfinity(scip), true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, lower1, f.at(make_tuple(i, j, k)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, lower1, x.at(make_tuple(i, j, k)), 1.0));
            SCIP_CALL(SCIPaddCons(scip, lower1));
            // SCIP_CALL(SCIPreleaseCons(scip, &lower1));

            SCIP_CONS* upper2 = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &upper2, "capacity_upper2", 0, nullptr, nullptr,
                -SCIPinfinity(scip), 0.0, true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, upper2, f.at(make_tuple(j, i, k)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, upper2, x.at(make_tuple(i, j, k)), -1.0));
            SCIP_CALL(SCIPaddCons(scip, upper2));
            // SCIP_CALL(SCIPreleaseCons(scip, &upper2));

            SCIP_CONS* lower2 = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &lower2, "capacity_lower2", 0, nullptr, nullptr,
                0.0, SCIPinfinity(scip), true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, lower2, f.at(make_tuple(j, i, k)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, lower2, x.at(make_tuple(i, j, k)), 1.0));
            SCIP_CALL(SCIPaddCons(scip, lower2));
            // SCIP_CALL(SCIPreleaseCons(scip, &lower2));
        }
    }
    
    for (int k = 0; k < K; ++k) {
        SCIP_CONS* onlym = nullptr;
        SCIP_CALL(SCIPcreateConsLinear(scip, &onlym, "onlym", 0, nullptr, nullptr,
            1.0, 1.0, true, true, true, true, true, false, false, false, false, false));

        for (int prime = 0; prime < copy_num; ++prime) {
            SCIP_CALL(SCIPaddCoefLinear(scip, onlym, m.at(make_tuple(k, prime)), 1.0));
        }

        SCIP_CALL(SCIPaddCons(scip, onlym));
        // SCIP_CALL(SCIPreleaseCons(scip, &onlym));
    }

    for (int prime = 0; prime < copy_num - 1; ++prime) {
        SCIP_CONS* prio = nullptr;
        SCIP_CALL(SCIPcreateConsLinear(scip, &prio, "prio", 0, nullptr, nullptr,
            0.0, SCIPinfinity(scip), true, true, true, true, true, false, false, false, false, false));
        for (int k = 0; k < K; ++k) {
            SCIP_CALL(SCIPaddCoefLinear(scip, prio, m.at(std::make_tuple(k, prime)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, prio, m.at(std::make_tuple(k, prime + 1)), -1.0));
        }
        SCIP_CALL(SCIPaddCons(scip, prio));
    }

    for (const auto& edge : edges) {
        int i = edge.first;
        int j = edge.second;
        for (int prime = 0; prime < copy_num; ++prime) {
            for (int k = 0; k < K; ++k) {
                SCIP_CONS* zlower = nullptr;
                SCIP_CALL(SCIPcreateConsLinear(scip, &zlower, "zlower", 0, nullptr, nullptr,
                    -1.0, 1.0, true, true, true, true, true, false, false, false, false, false));
                SCIP_CALL(SCIPaddCoefLinear(scip, zlower, z.at(make_tuple(i, j, prime)), 1.0));
                SCIP_CALL(SCIPaddCoefLinear(scip, zlower, z.at(make_tuple(j, i, prime)), 1.0));
                SCIP_CALL(SCIPaddCoefLinear(scip, zlower, x.at(make_tuple(i, j, k)), -1.0));
                SCIP_CALL(SCIPaddCoefLinear(scip, zlower, m.at(make_tuple(k, prime)), -1.0));
                SCIP_CALL(SCIPaddCons(scip, zlower));
                // SCIP_CALL(SCIPreleaseCons(scip, &zlower));
            }
        }
    }
    

    // Loop constraints
    for (const auto& edge : edges) {
        int i = edge.first;
        int j = edge.second;
        for (int prime = 0; prime < copy_num; ++prime) {
            // Topological sequence constraints
            SCIP_CONS* topo_seq1 = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &topo_seq1, "topo_seq1", 0, nullptr, nullptr,
                -1.0, 1.0 - 1e-3, true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq1, y.at(make_tuple(i, prime)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq1, y.at(make_tuple(j, prime)), -1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq1, z.at(make_tuple(i, j, prime)), 1.0));
            SCIP_CALL(SCIPaddCons(scip, topo_seq1));
            // SCIP_CALL(SCIPreleaseCons(scip, &topo_seq1));

            SCIP_CONS* topo_seq2 = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &topo_seq2, "topo_seq2", 0, nullptr, nullptr,
                -1.0, 1.0 - 1e-3, true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq2, y.at(make_tuple(j, prime)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq2, y.at(make_tuple(i, prime)), -1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq2, z.at(make_tuple(j, i, prime)), 1.0));
            SCIP_CALL(SCIPaddCons(scip, topo_seq2));
            // SCIP_CALL(SCIPreleaseCons(scip, &topo_seq2));

            // Single direction constraint
            SCIP_CONS* single_dir = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &single_dir, "single_dir", 0, nullptr, nullptr,
                0.0, 1.0, true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, single_dir, z.at(make_tuple(i, j, prime)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, single_dir, z.at(make_tuple(j, i, prime)), 1.0));
            SCIP_CALL(SCIPaddCons(scip, single_dir));
            // SCIP_CALL(SCIPreleaseCons(scip, &single_dir));
        }
    }

    // Only father constraints
    for (const auto& i : nodes) {
        for (int prime = 0; prime < copy_num; ++prime) {
            SCIP_CONS* only_father = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &only_father, "only_father", 0, nullptr, nullptr,
                0.0, 1.0, true, true, true, true, true, false, false, false, false, false));

            for (const auto& j : adj_list.at(i)) {
                SCIP_CALL(SCIPaddCoefLinear(scip, only_father, z.at(make_tuple(j, i, prime)), 1.0));
            }

            SCIP_CALL(SCIPaddCons(scip, only_father));
            // SCIP_CALL(SCIPreleaseCons(scip, &only_father));
        }
    }

    // Solve the problem
    cout << "Solving..." << endl;

    SCIP_CALL(SCIPsolve(scip));
    // SCIP_CALL(SCIPsolveConcurrent(scip));
    

    // Check solution status
    SCIP_STATUS status = SCIPgetStatus(scip);
    double obj_value = SCIPgetPrimalbound(scip);

    switch (status) {
    case SCIP_STATUS_OPTIMAL:
        cout << "OPTIMAL" << endl;
        // SCIP_CALL( SCIPwriteOrigProblem(scip, "model_leaf+cp.lp", "lp", FALSE) );
        break;
    case SCIP_STATUS_INFEASIBLE:
        cout << "INFEASIBLE" << endl;
        // SCIP_CALL( SCIPcomputeIIS(scip) );
        // SCIP_CALL( SCIPwriteIIS(scip, "model_inf.iis") );
        break;
    default:
        cout << "OTHER_STATUS" << endl;
        break;
    }

    if (status != SCIP_STATUS_OPTIMAL) {
        cerr << "The problem does not have an optimal solution!" << endl;
        SCIP_CALL(SCIPfree(&scip));
        return -1;
    }

    cout << "Solution:" << endl;
    cout << "Objective value = " << obj_value << endl;

    SCIP_SOL* bestsol = SCIPgetBestSol(scip);
    // Process solution
    for (int k = 0; k < K; ++k) {
        for (int prime = 0; prime < copy_num; ++prime) {
            if (SCIPgetSolVal(scip, bestsol, m.at(make_tuple(k, prime))) > 0.5) {
                k2prime.emplace(k, prime);
            }
        }
    }

    for (auto& [key, var] : x) {
        if (SCIPgetSolVal(scip, bestsol, var) > 0.5) {
            int i = get<0>(key);
            int j = get<1>(key);
            int k = get<2>(key);
            int prime = k2prime[k];
            if (i == j) {
                continue;
            }
            auto edge = (i < j) ? make_pair(i, j) : make_pair(j, i);

            int u = i * copy_num + prime;
            int v = j * copy_num + prime;
            auto edge_prime = (u < v) ? make_pair(u, v) : make_pair(v, u);
            pairs_to_ret_edges_prime[id_to_pairs[k]].insert(edge_prime);
            ret_edges_prime.insert(edge_prime);
        }
    }

    // Free memory
    // SCIP_CALL(SCIPfree(&scip));

    return obj_value;
}

double ret_compose(double obj, std::vector<std::pair<int, int>>& edges, 
                    std::map<std::pair<int, int>, int>& edges_to_weight, 
                    std::vector<std::pair<int, int>>& pairs, 
                    std::map<std::pair<int, int>, double>& pairs_to_weight, 
                    std::map<int, int>& leaf_to_center, 
                    std::map<std::pair<int, int>, std::pair<int, int>>& pairs_to_center_pairs, 
                    std::map<std::pair<int, int>, int>& center_pairs_to_id,
                    std::map<int, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>>& id_to_ret_edges_prime, 
                    std::map<std::pair<int, int>, std::set<std::pair<int, int>>>& cpairs_to_ret_edges_prime, 
                    std::set<std::pair<int, int>>& ret_edges_prime, 
                    std::map<int, int>& k2prime) {
    const int K = pairs.size();
    double ttl_obj = obj;
    for (int k = 0; k < K; ++k) {
        auto pair = pairs[k];
        int s = pair.first;
        int t = pair.second;
        double pair_weight = pairs_to_weight[pair];
        auto cpair = pairs_to_center_pairs[pair];
        int prime = k2prime[center_pairs_to_id[cpair]];
        double edge_weight_sum = 0;

        auto sedge_prime = (s < leaf_to_center[s]) 
            ? std::make_pair(std::make_pair(s, 0), std::make_pair(leaf_to_center[s], prime)) 
            : std::make_pair(std::make_pair(leaf_to_center[s], prime), std::make_pair(s, 0));
        auto tedge_prime = (t < leaf_to_center[t]) 
            ? std::make_pair(std::make_pair(t, 0), std::make_pair(leaf_to_center[t], prime)) 
            : std::make_pair(std::make_pair(leaf_to_center[t], prime), std::make_pair(t, 0));
        id_to_ret_edges_prime[k].insert(sedge_prime);
        id_to_ret_edges_prime[k].insert(tedge_prime);
        // ret_edges_prime.insert(sedge_prime);
        // ret_edges_prime.insert(tedge_prime);
        for (const auto& edge : cpairs_to_ret_edges_prime[cpair]) {
            int u = edge.first;
            int v = edge.second;
            int i = u / copy_num;
            int i_prime = u % copy_num;
            int j = v / copy_num;
            int j_prime = v % copy_num;
            id_to_ret_edges_prime[k].insert(std::make_pair(std::make_pair(i, i_prime), std::make_pair(j, j_prime)));
        }

        // for (const auto& edge : id_to_ret_edges_prime[k]) {
        //     auto u = edge.first;
        //     auto v = edge.second;
        //     int i = u.first;
        //     int j = v.first;
        //     edge_weight_sum += edges_to_weight[std::make_pair(i, j)];
        // }
        
        // ttl_obj += edge_weight_sum * pair_weight;

        auto sedge = (s < leaf_to_center[s]) ? std::make_pair(s, leaf_to_center[s]) : std::make_pair(leaf_to_center[s], s);
        auto tedge = (t < leaf_to_center[t]) ? std::make_pair(t, leaf_to_center[t]) : std::make_pair(leaf_to_center[t], t);
        ttl_obj += (edges_to_weight[sedge] + edges_to_weight[tedge]) * pair_weight;
    }
    return ttl_obj;
}

std::string getCurrentTimeSuffix() {
    // 获取当前时间
    std::time_t now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);

    // 将时间格式化为字符串
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

// int print_pairs_to_ret_edges(std::map<int, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>>& pairs_to_ret_edges, 
//                              std::map<int, std::string>& id_to_node_name, 
//                              const std::string& suffix) {
//     std::ofstream outFile("./result/opt_result_" + suffix + "_pair.csv");
//     std::stringstream outss;

//     if (!outFile.is_open()) {
//         std::cerr << "file open fail" << std::endl;
//         return 1;
//     }
//     outFile << "pair_id,start,id,end,id" << std::endl;
//     for (const auto& pair2edges : pairs_to_ret_edges) {
//         int pair_id = pair2edges.first;
//         auto ret_edges = pair2edges.second;
//         for (const auto& edge : ret_edges) {
//             auto u = edge.first;
//             auto v = edge.second;
//             int i = u.first;
//             int i_prime = u.second+1;
//             int j = v.first;
//             int j_prime = v.second+1;
//             // if (i_prime == 0) {
//             //     outss << pair_id << "," << id_to_node_name[i] << "," << i_prime << "," << id_to_node_name[j] << "," << j_prime << std::endl;
//             // } else if (j_prime == 0) {
//             //     outss << pair_id << "," << id_to_node_name[i] << "," << i_prime << "," << id_to_node_name[j] << "," << j_prime << std::endl;
//             // } else {
//             //     outss << pair_id << "," << id_to_node_name[i] << "," << i_prime << "," << id_to_node_name[j] << "," << j_prime << std::endl;
//             // }
//             outss << pair_id+1 << "," << id_to_node_name[i] << "," << i_prime << "," << id_to_node_name[j] << "," << j_prime << std::endl;
//         }
//     }
//     outFile << outss.str();
//     outFile.close();
//     return 0;
// }

// int print_pairs_to_ret_edges(std::map<int, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>>& pairs_to_ret_edges, 
//                              std::map<int, std::string>& id_to_node_name, 
//                              const std::string& suffix) {
//     // 创建XLSX文件名
//     std::string filename = "./result/opt_result_" + suffix + "_pair.xls";
    
//     // 创建工作簿和工作表
//     lxw_workbook* workbook = workbook_new(filename.c_str());
//     lxw_worksheet* worksheet = workbook_add_worksheet(workbook, "Pair Results");
    
//     if (!worksheet) {
//         std::cerr << "Failed to create worksheet" << std::endl;
//         return 1;
//     }
    
//     // 定义单元格格式
//     lxw_format* header_format = workbook_add_format(workbook);
//     format_set_bold(header_format);
//     format_set_align(header_format, LXW_ALIGN_CENTER);
    
//     lxw_format* data_format = workbook_add_format(workbook);
//     format_set_align(data_format, LXW_ALIGN_LEFT);
    
//     // 写入表头
//     worksheet_write_string(worksheet, 0, 0, "pair_id", header_format);
//     worksheet_write_string(worksheet, 0, 1, "start", header_format);
//     worksheet_write_string(worksheet, 0, 2, "id", header_format);
//     worksheet_write_string(worksheet, 0, 3, "end", header_format);
//     worksheet_write_string(worksheet, 0, 4, "id", header_format);
    
//     int row = 1; // 从第1行开始（0-based索引，第0行是表头）
    
//     // 写入数据
//     for (const auto& pair2edges : pairs_to_ret_edges) {
//         int pair_id = pair2edges.first;
//         auto ret_edges = pair2edges.second;
        
//         for (const auto& edge : ret_edges) {
//             auto u = edge.first;
//             auto v = edge.second;
//             int i = u.first;
//             int i_prime = u.second + 1;
//             int j = v.first;
//             int j_prime = v.second + 1;
            
//             // 写入数据到Excel
//             worksheet_write_number(worksheet, row, 0, pair_id + 1, data_format);
//             worksheet_write_string(worksheet, row, 1, id_to_node_name[i].c_str(), data_format);
//             worksheet_write_number(worksheet, row, 2, i_prime, data_format);
//             worksheet_write_string(worksheet, row, 3, id_to_node_name[j].c_str(), data_format);
//             worksheet_write_number(worksheet, row, 4, j_prime, data_format);
            
//             row++;
//         }
//     }
    
//     // 自动调整列宽
//     worksheet_set_column(worksheet, 0, 0, 10, NULL);  // pair_id列
//     worksheet_set_column(worksheet, 1, 1, 15, NULL);  // start列
//     worksheet_set_column(worksheet, 2, 2, 8, NULL);   // id列
//     worksheet_set_column(worksheet, 3, 3, 15, NULL);  // end列
//     worksheet_set_column(worksheet, 4, 4, 8, NULL);   // id列
    
//     // 关闭工作簿
//     lxw_error error = workbook_close(workbook);
    
//     if (error != LXW_NO_ERROR) {
//         std::cerr << "Failed to close workbook: " << lxw_strerror(error) << std::endl;
//         return 1;
//     }
    
//     return 0;
// }

string find_path(int pair_id, pair<int, int> start, pair<int, int> end, 
                const set<pair<pair<int, int>, pair<int, int>>>& edges,
                const map<int, string>& id_to_node_name) {
    
    // 构建邻接表
    std::map<int, vector<int>> graph;
    for (const auto& edge : edges) {
        auto src = edge.first;
        auto dst = edge.second;
        int src_id = src.first*copy_num+src.second;
        int dst_id = dst.first*copy_num+dst.second;
        graph[src_id].push_back(dst_id);
        graph[dst_id].push_back(src_id);
    }
    int start_id = start.first*copy_num+start.second;
    int end_id = end.first*copy_num+end.second;
    std::string path = id_to_node_name.at(start.first) + ";";
    int now = start_id;
    std::set<int> visited;
    
    while (!path.empty()) {
        visited.insert(now);
        
        if (now == end_id) {
            return path;
        }
        // cout << now << endl;
        int flag = 0;
        for (int neighbor : graph.at(now)) {
            if (!visited.count(neighbor)) {
                int id = neighbor / copy_num;
                int prime = neighbor % copy_num;
                // cout << id_to_node_name.at(id) << endl;
                if (neighbor == end_id) {
                    path += id_to_node_name.at(id);
                } else {
                    path += (id_to_node_name.at(id) + "_" + to_string(prime+1)) + ";";
                }
                now = neighbor;
                flag = 1;
                break;
            }
        }
        if (flag == 0) {
            // cout << "dead loop in " << pair_id << " " << id_to_node_name.at(now / copy_num) << endl;
            break;
        }
    }
    return "";
}

// int print_pairs_to_path(std::vector<std::pair<int, int>>& pairs, 
//                         std::map<int, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>>& pairs_to_ret_edges, 
//                         std::map<int, std::string>& id_to_node_name, 
//                         const std::string& suffix) {
//     std::ofstream outFile("./result/opt_result_" + suffix + "_pair_path.csv");
//     std::stringstream outss;

//     if (!outFile.is_open()) {
//         std::cerr << "file open fail" << std::endl;
//         return 1;
//     }
//     outFile << "id,start,end,path" << std::endl;
//     for (const auto& pair2edges : pairs_to_ret_edges) {
//         int pair_id = pair2edges.first;
//         auto edges = pair2edges.second;
//         const auto& pair = pairs[pair_id];
//         int start = pair.first;
//         int end = pair.second;
//         string path = find_path(pair_id, {start, 0}, {end, 0}, edges, id_to_node_name);
//         outss << pair_id+1 << "," << id_to_node_name[start] << "," << id_to_node_name[end] << "," << path << std::endl;
//         // outss << id << "," << id_to_node_name[start] << "-1" << "," << id_to_node_name[end] << "-1" << "," << path << std::endl;
//     }
//     outFile << outss.str();
//     outFile.close();
//     return 0;
// }

// int print_pairs_to_path(std::vector<std::pair<int, int>>& pairs, 
//                         std::map<int, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>>& pairs_to_ret_edges, 
//                         std::map<int, std::string>& id_to_node_name, 
//                         const std::string& suffix) {
//     // 创建XLSX文件名
//     std::string filename = "./result/opt_result_" + suffix + "_pair_path.xls";
    
//     // 创建工作簿和工作表
//     lxw_workbook* workbook = workbook_new(filename.c_str());
//     lxw_worksheet* worksheet = workbook_add_worksheet(workbook, "Pair Paths");
    
//     if (!worksheet) {
//         std::cerr << "Failed to create worksheet" << std::endl;
//         return 1;
//     }
    
//     // 定义单元格格式
//     lxw_format* header_format = workbook_add_format(workbook);
//     format_set_bold(header_format);
//     format_set_align(header_format, LXW_ALIGN_CENTER);
    
//     lxw_format* data_format = workbook_add_format(workbook);
//     format_set_align(data_format, LXW_ALIGN_LEFT);
    
//     lxw_format* path_format = workbook_add_format(workbook);
//     format_set_align(path_format, LXW_ALIGN_LEFT);
//     format_set_text_wrap(path_format); // 允许文本换行
    
//     // 写入表头
//     worksheet_write_string(worksheet, 0, 0, "id", header_format);
//     worksheet_write_string(worksheet, 0, 1, "start", header_format);
//     worksheet_write_string(worksheet, 0, 2, "end", header_format);
//     worksheet_write_string(worksheet, 0, 3, "path", header_format);
    
//     int row = 1; // 从第1行开始（0-based索引，第0行是表头）
    
//     // 写入数据
//     for (const auto& pair2edges : pairs_to_ret_edges) {
//         int pair_id = pair2edges.first;
//         auto edges = pair2edges.second;
//         const auto& pair = pairs[pair_id];
//         int start = pair.first;
//         int end = pair.second;
        
//         std::string path = find_path(pair_id, {start, 0}, {end, 0}, edges, id_to_node_name);
        
//         // 写入数据到Excel
//         worksheet_write_number(worksheet, row, 0, pair_id + 1, data_format);
//         worksheet_write_string(worksheet, row, 1, id_to_node_name[start].c_str(), data_format);
//         worksheet_write_string(worksheet, row, 2, id_to_node_name[end].c_str(), data_format);
//         worksheet_write_string(worksheet, row, 3, path.c_str(), path_format);
        
//         row++;
//     }
    
//     // 自动调整列宽
//     worksheet_set_column(worksheet, 0, 0, 8, NULL);   // id列
//     worksheet_set_column(worksheet, 1, 1, 15, NULL);  // start列
//     worksheet_set_column(worksheet, 2, 2, 15, NULL);  // end列
//     worksheet_set_column(worksheet, 3, 3, 50, NULL);  // path列（设置较宽以容纳路径信息）
    
//     // 关闭工作簿
//     lxw_error error = workbook_close(workbook);
    
//     if (error != LXW_NO_ERROR) {
//         std::cerr << "Failed to close workbook: " << lxw_strerror(error) << std::endl;
//         return 1;
//     }
    
//     std::cout << "Excel file created successfully: " << filename << std::endl;
//     return 0;
// }

int print_ret_edges(std::set<std::pair<int, int>>& ret_edges, 
                    std::map<int, std::string>& id_to_node_name, 
                    const std::string& suffix, bool is_prime = false) {
    std::string prime_suffix = is_prime ? "_prime" : "";
    std::ofstream outFileall1("./result/opt_result_" + suffix + prime_suffix + ".csv");
    std::ofstream outFileall2("./result/opt_result_" + suffix + prime_suffix + "_name.csv");
    std::stringstream outFile1;
    std::stringstream outFile2;

    if (!outFileall1.is_open() || !outFileall2.is_open()) {
        std::cerr << "file open fail" << std::endl;
        return 1;
    }

    outFile1 << "start,end" << std::endl;
    outFile2 << (is_prime ? "start,id,end,id" : "start,end") << std::endl;

    for (const auto& edge : ret_edges) {
        int u = edge.first;
        int v = edge.second;

        if (is_prime) {
            int i = u / copy_num;
            int i_prime = u % copy_num + 1;
            int j = v / copy_num;
            int j_prime = v % copy_num + 1;
            outFile1 << u << "," << v << std::endl;
            outFile2 << id_to_node_name[i] << "," << i_prime << "," << id_to_node_name[j] << "," << j_prime << std::endl;
        } else {
            outFile1 << u << "," << v << std::endl;
            outFile2 << id_to_node_name[u] << "," << id_to_node_name[v] << std::endl;
        }
    }

    outFileall1 << outFile1.str();
    outFileall2 << outFile2.str();
    outFileall1.close();
    outFileall2.close();
    return 0;
}

int main(int argc, char* argv[]) {
    // std::string file_path1 = "data/edges-3.csv";    
    // std::string file_path2 = "data/pairs-3-40.csv";
    std::string file_path1 = "data/edges-4.csv";    
    std::string file_path2 = "data/pairs-4-246.csv";
    // std::string file_path1 = "data/edges.csv";    
    // std::string file_path2 = "data/pairs.csv";
    std::string file_path3 = "data/name_to_copynums.csv";
    if (argc > 1) {
        copy_num = std::stoi(argv[1]);
        fix_copy_num = copy_num - unfix_copy_num;
    }
    if (argc > 2) {
        gap = std::stod(argv[2]);
        // scale = std::string(argv[2]);
        // if (scale != "1.0") {
        //     file_path1 = "data/edges_" + scale + "x.csv";    
        //     file_path2 = "data/pairs_" + scale + "x.csv";
        // }
    }
    // if (argc > 3) {
    //     thread_num = std::stoi(argv[3]);
    //     // scale = std::string(argv[2]);
    //     // if (scale != "1.0") {
    //     //     file_path1 = "data/edges_" + scale + "x.csv";    
    //     //     file_path2 = "data/pairs_" + scale + "x.csv";
    //     // }
    // }
    // std::string file_path1 = "test_edges.csv";    
    // std::string file_path2 = "test_pairs.csv";
    std::map<std::string, int> node_name_to_id;
    std::map<int, std::string> id_to_node_name;
    std::vector<std::pair<int, int>> edges;
    std::map<std::pair<int, int>, int> edges_to_weight;
    std::map<int, std::set<int>> adj_list;
    std::set<int> center_nodes;
    std::vector<std::pair<int, int>> center_edges; 
    std::vector<std::pair<int, int>> entry_edges;
    std::map<int, std::vector<std::pair<int, int>>> cpid_to_entry_edges;
    std::vector<std::pair<int, int>> leaf_edges;
    std::map<int, int> leaf_to_center;
    std::vector<std::pair<int, int>> ori_pairs;
    std::vector<std::pair<int, int>> pairs;
    std::map<std::pair<int, int>, double> pairs_to_weight;
    std::map<std::pair<int, int>, std::pair<int, int>> pairs_to_center_pairs;
    std::vector<std::pair<int, int>> center_pairs;
    std::map<int, std::pair<int, int>> id_to_center_pairs;
    std::map<std::pair<int, int>, int> center_pairs_to_id;
    std::map<std::pair<int, int>, double> center_pairs_to_weight;
    std::map<int, int> id_to_copynums;
    // std::map<std::pair<int, int>, std::set<std::pair<int, int>>> pairs_to_ret_edges; // k--n
    std::map<std::pair<int, int>, std::set<std::pair<int, int>>> cpairs_to_ret_edges_prime; // k--copy_num*n center
    std::set<std::pair<int, int>> cret_edges_prime;
    std::map<int, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>> id_to_ret_edges_prime;
    // std::map<int, std::set<std::pair<int, int>>> id_to_ret_edges_prime;
    // std::set<std::pair<int, int>> ret_edges; // n
    std::set<std::pair<int, int>> ret_edges_prime; // copy_num*n
    std::map<int, std::set<int>> ret_edges_prime_adj_list;
    std::map<int, int> k2prime;
    std::cout << "Reading edges" << std::endl;
    int node_num = read_csv_to_edges(file_path1, node_name_to_id, id_to_node_name, edges, edges_to_weight, 
                                        center_nodes, center_edges, leaf_edges, leaf_to_center, entry_edges);
    if (node_num == -1) {
        return 1;
    }
    std::cout << "Reading pairs" << std::endl;
    if (read_csv_to_pairs(file_path2, node_name_to_id, pairs, ori_pairs, pairs_to_weight, pairs_to_center_pairs, center_pairs, 
                            id_to_center_pairs, center_pairs_to_id, center_pairs_to_weight, leaf_to_center,
                            entry_edges, cpid_to_entry_edges) == -1) {
        return 1;
    }
    // set_dflt_copynums(node_name_to_id, id_to_copynums);
    // if (read_csv_to_copynums(file_path3, node_name_to_id, id_to_copynums) == -1) {
    //     return 1;
    // }
    // std::cout << "Cpair size: " << id_to_center_pairs.size() << std::endl;

    std::map<int, std::set<std::pair<int, int>>> cpid_to_sp_edges;
    processAllPairs(center_edges, edges_to_weight, center_pairs, pairs_to_center_pairs, center_pairs_to_id, 
                    cpid_to_sp_edges, entry_edges, cpid_to_entry_edges);
    
    std::map<int, std::vector<int>> cp_to_lockpids;
    cp_to_lockpids = fixPairs(
        center_pairs, 
        center_pairs_to_weight,
        center_pairs_to_id, 
        cpid_to_sp_edges
    );

    // std::map<int, std::vector<int>> cp_to_conflictpids;
    // cp_to_conflictpids = checkConflictPairs(
    //     center_pairs, 
    //     cpid_to_sp_edges
    // );
    // for (const auto& pair : cp_to_conflictpids) {
    //     std::cout << "Key: " << pair.first << " -> Values: ";
    //     for (int value : pair.second) {
    //         std::cout << value << " ";
    //     }
    //     std::cout << std::endl;
    // }
    
    adj_list = convertToAdjacencyList(center_edges);
    std::cout << "Solving problem" << std::endl;
    double obj = 0;
    try {
        obj = SolveMIPProblem(center_nodes, center_edges, edges_to_weight, id_to_center_pairs, center_pairs_to_weight, 
                                adj_list, cpairs_to_ret_edges_prime, cret_edges_prime, k2prime, id_to_copynums, cpid_to_sp_edges, 
                                cp_to_lockpids, entry_edges, cpid_to_entry_edges);
        // SolveMIPProblemFromFile(pairs_to_ret_edges, ret_edges, cret_edges_prime, "model-opt.lp");
    } catch (std::exception e) {
        cout << e.what() << endl;
    } catch (...) {
      cout << "Unknown exception occurs!" << endl;
    }
    // saveToText(cpairs_to_ret_edges_prime, "./cpairs_to_paths.txt");
    // saveMapToText(k2prime, "./k2prime.txt");
    // cout << "save success" << endl;

    // cpairs_to_ret_edges_prime = loadFromText("./cpairs_to_paths.txt");
    // k2prime = loadMapFromText("./k2prime.txt");
    
    // check cret_edges_prime
    ret_edges_prime_adj_list = convertToAdjacencyList(cret_edges_prime);
    std::cout << "Checking cycles" << std::endl;
    auto cyclePath = detectCycle(ret_edges_prime_adj_list);
    if (cyclePath) {
        std::cout << "Error, Cycle in result: " << std::endl;
        for (int node : *cyclePath) {
            std::cout << node << " ";
        }
        std::cout << std::endl;
    } else {
        std::cout << "No cycle" << std::endl;
    }
    // if (obj <= 0) {
    //     return 0;
    // }
    double total_obj = ret_compose(obj, edges, edges_to_weight, pairs, pairs_to_weight, leaf_to_center, pairs_to_center_pairs, 
                                    center_pairs_to_id, id_to_ret_edges_prime, cpairs_to_ret_edges_prime, ret_edges_prime, k2prime);
    // double total_obj = ret_compose_2(obj, edges, edges_to_weight, pairs, pairs_to_weight, leaf_to_center, pairs_to_center_pairs, 
    //                                 center_pairs_to_id, id_to_ret_edges_prime, cpairs_to_ret_edges_prime, ret_edges_prime, k2prime);
    std::cout << "Total best res: " << total_obj << std::endl;
    std::cout << "Outputing results" << std::endl;
    std::string suffix = getCurrentTimeSuffix();
    // print_pairs_to_ret_edges(id_to_ret_edges_prime, id_to_node_name, suffix);
    // print_pairs_to_path(ori_pairs, id_to_ret_edges_prime, id_to_node_name, suffix);
    // print_pairs_to_ret_edges(pairs_to_ret_edges, id_to_node_name, suffix, false);
    // print_ret_edges(ret_edges_prime, id_to_node_name, suffix, true);
    // print_ret_edges(ret_edges, id_to_node_name, suffix, false);
    // print_ret_edges_prime_name(ret_edges, id_to_node_name, suffix);
    // print_model_cip(node_num, edges, edges_to_weight, pairs, pairs_to_weight, adj_list);
    std::cout << "Execute success" << std::endl;
    return 0;
}