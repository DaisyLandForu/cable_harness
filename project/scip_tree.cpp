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
#include <stack>
#include <cmath> 
#include <algorithm>
#include <scip/scip.h>
#include <scip/scipdefplugins.h>
#include <scip/struct_scip.h>

using namespace std;

int copy_num_dflt = 4;
int copy_num = copy_num_dflt;
string scale = "1.0";
char center_prefix = 'N';
int thread_num = 16;
int div_part = 1;
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

std::vector<std::string> parseCSVLine(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ',')) {
        result.push_back(token);
    }
    return result;
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
                int weight = std::stod(tokens[4]);

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
                        std::set<std::pair<int, int>>& center_pairs, 
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
            if (tokens.size() >= 7) {
                // std::string node1_name = tokens[3];
                // std::string node2_name = tokens[4];
                // double weight = std::stod(tokens[5]);

                std::string node1_name = tokens[3];
                std::string node2_name = tokens[5];
                double weight = std::stod(tokens[9]);

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
                if (center_pairs.insert(c_pair).second) {
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
                        std::vector<std::pair<int, int>>& entry_edges,
                        std::map<int, std::vector<std::pair<int, int>>>& cpid_to_entry_edges) {

    // Initialize SCIP
    SCIP* scip = nullptr;
    SCIP_CALL(SCIPcreate(&scip));
    SCIP_CALL(SCIPincludeDefaultPlugins(scip));
    SCIP_CALL(SCIPcreateProbBasic(scip, "MIPProblem"));

    // Set parameters
    double gap = 0.0;
    SCIP_CALL(SCIPsetRealParam(scip, "limits/gap", gap));
    SCIP_CALL(SCIPsetBoolParam(scip, "branching/preferbinary", true));
    SCIP_CALL(SCIPsetIntParam(scip, "parallel/minnthreads", 4));
    SCIP_CALL(SCIPsetIntParam(scip, "parallel/maxnthreads", 16));
    SCIP_CALL(SCIPsetIntParam(scip, "lp/threads", 4));

    SCIP_CALL(SCIPsetIntParam(scip, "heuristics/rens/freq", 50));
    SCIP_CALL(SCIPsetIntParam(scip, "heuristics/rens/priority", 100000));
    SCIP_CALL(SCIPsetIntParam(scip, "heuristics/alns/freq", 50));
    SCIP_CALL(SCIPsetIntParam(scip, "heuristics/alns/priority", 90000));
    // SCIP_CALL(SCIPsetIntParam(scip, "heuristics/dinns/priority", 85000));
    // SCIP_CALL(SCIPsetIntParam(scip, "nodeselection/hybridestim/stdpriority", 500000));
    
    cout << "SCIP" << endl;
    cout << "mip_gap: " << gap << endl;

    const int K = id_to_pairs.size();
    const int e = edges.size();
    const int n = nodes.size();
    //const int copy_num = 3; // Assuming copy_num is consistent

    cout << "pairs size: " << K << endl;
    cout << "nodes size: " << n << endl;
    cout << "edges size: " << e << endl;
    cout << "copy   num: " << copy_num << endl;

    // Variable storage
    map<tuple<int, int, int>, SCIP_VAR*> x;
    map<tuple<int, int, int>, SCIP_VAR*> f;
    map<tuple<int, int, int>, SCIP_VAR*> absf;
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

    for (int k = 0; k < K; ++k) {
        // auto save_edges = cpid_to_entry_edges[k];
        for (const auto& edge : edges) {
            int i = edge.first;
            int j = edge.second;

            // if (std::find(entry_edges.begin(), entry_edges.end(), edge) != entry_edges.end()) {
            //     if (std::find(save_edges.begin(), save_edges.end(), edge) == save_edges.end()) {
            //         continue;
            //     }
            // }

            // SCIP_VAR* var_x;
            // SCIP_CALL(SCIPcreateVarBasic(scip, &var_x,
            //     generateVarName("x", i, j, k).c_str(),
            //     0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
            // SCIP_CALL(SCIPaddVar(scip, var_x));
            // x.emplace(make_tuple(i, j, k), var_x);

            SCIP_VAR* var_absf;
            SCIP_CALL(SCIPcreateVarBasic(scip, &var_absf,
                generateVarName("absf", i, j, k).c_str(),
                0.0, 1.0, 0.0, SCIP_VARTYPE_CONTINUOUS));
            SCIP_CALL(SCIPaddVar(scip, var_absf));
            absf.emplace(make_tuple(i, j, k), var_absf);

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
    }

    for (const auto& edge : edges) {
        int i = edge.first;
        int j = edge.second;

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

    // priority
    // for (int k = 0; k < K; ++k) {
    //     for (int prime = 0; prime < copy_num; ++prime) {
    //         SCIP_CALL(SCIPchgVarBranchPriority(scip, m.at(make_tuple(k, prime)), 10-k));
    //     }
    // }

    for (const auto& [k, pair] : id_to_pairs) {
        int start = pair.first, end = pair.second;
        // auto save_edges = cpid_to_entry_edges[k];
        for (const auto& edge : entry_edges) {
            int i = edge.first;
            int j = edge.second;
            if (i == start || i == end || j == start || j == end) {
                continue;
            } else {
                SCIP_CONS* fforbid = nullptr;
                SCIP_CALL(SCIPcreateConsLinear(scip, &fforbid, "fforbid", 0, nullptr, nullptr,
                    0.0, 0.0, true, true, true, true, true, false, false, false, false, false));
                SCIP_CALL(SCIPaddCoefLinear(scip, fforbid, f.at(std::make_tuple(i, j, k)), 1.0));
                SCIP_CALL(SCIPaddCons(scip, fforbid));
            }
            // if (std::find(save_edges.begin(), save_edges.end(), edge) == save_edges.end()) {
            //     int i = edge.first;
            //     int j = edge.second;
            //     model.AddConstr(x.at(std::make_tuple(i, j, k)) == 0);
            // }
        }
    }

    // Set objective
    for (int k = 0; k < K; ++k) {
        double pair_weight = pairs_to_weight[id_to_pairs[k]];
        for (const auto& edge : edges) {
            int i = edge.first;
            int j = edge.second;
            double coef = edges_to_weight[edge] * pair_weight;
            SCIP_CALL(SCIPchgVarObj(scip, absf.at(std::make_tuple(i, j, k)), coef)); 
        }
    }
    SCIP_CALL(SCIPsetObjsense(scip, SCIP_OBJSENSE_MINIMIZE));

    for (const auto& edge : edges) {
        int i = edge.first;
        int j = edge.second;
        for (int k = 0; k < K; ++k) {
            SCIP_CONS* abs1 = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &abs1, "abs1", 0, nullptr, nullptr,
                0.0, SCIPinfinity(scip), true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, abs1, absf.at(make_tuple(i, j, k)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, abs1, f.at(make_tuple(i, j, k)), -1.0));
            SCIP_CALL(SCIPaddCons(scip, abs1));
            SCIP_CONS* abs2 = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &abs2, "abs2", 0, nullptr, nullptr,
                0.0, SCIPinfinity(scip), true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, abs2, absf.at(make_tuple(i, j, k)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, abs2, f.at(make_tuple(i, j, k)), 1.0));
            SCIP_CALL(SCIPaddCons(scip, abs2));
        }
    }

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

            // // Capacity constraints
            // SCIP_CONS* upper1 = nullptr;
            // SCIP_CALL(SCIPcreateConsLinear(scip, &upper1, "capacity_upper1", 0, nullptr, nullptr,
            //     -SCIPinfinity(scip), 0.0, true, true, true, true, true, false, false, false, false, false));
            // SCIP_CALL(SCIPaddCoefLinear(scip, upper1, f.at(make_tuple(i, j, k)), 1.0));
            // SCIP_CALL(SCIPaddCoefLinear(scip, upper1, x.at(make_tuple(i, j, k)), -1.0));
            // SCIP_CALL(SCIPaddCons(scip, upper1));
            // // SCIP_CALL(SCIPreleaseCons(scip, &upper1));

            // SCIP_CONS* lower1 = nullptr;
            // SCIP_CALL(SCIPcreateConsLinear(scip, &lower1, "capacity_lower1", 0, nullptr, nullptr,
            //     0.0, SCIPinfinity(scip), true, true, true, true, true, false, false, false, false, false));
            // SCIP_CALL(SCIPaddCoefLinear(scip, lower1, f.at(make_tuple(i, j, k)), 1.0));
            // SCIP_CALL(SCIPaddCoefLinear(scip, lower1, x.at(make_tuple(i, j, k)), 1.0));
            // SCIP_CALL(SCIPaddCons(scip, lower1));
            // // SCIP_CALL(SCIPreleaseCons(scip, &lower1));

            // SCIP_CONS* upper2 = nullptr;
            // SCIP_CALL(SCIPcreateConsLinear(scip, &upper2, "capacity_upper2", 0, nullptr, nullptr,
            //     -SCIPinfinity(scip), 0.0, true, true, true, true, true, false, false, false, false, false));
            // SCIP_CALL(SCIPaddCoefLinear(scip, upper2, f.at(make_tuple(j, i, k)), 1.0));
            // SCIP_CALL(SCIPaddCoefLinear(scip, upper2, x.at(make_tuple(i, j, k)), -1.0));
            // SCIP_CALL(SCIPaddCons(scip, upper2));
            // // SCIP_CALL(SCIPreleaseCons(scip, &upper2));

            // SCIP_CONS* lower2 = nullptr;
            // SCIP_CALL(SCIPcreateConsLinear(scip, &lower2, "capacity_lower2", 0, nullptr, nullptr,
            //     0.0, SCIPinfinity(scip), true, true, true, true, true, false, false, false, false, false));
            // SCIP_CALL(SCIPaddCoefLinear(scip, lower2, f.at(make_tuple(j, i, k)), 1.0));
            // SCIP_CALL(SCIPaddCoefLinear(scip, lower2, x.at(make_tuple(i, j, k)), 1.0));
            // SCIP_CALL(SCIPaddCons(scip, lower2));
            // // SCIP_CALL(SCIPreleaseCons(scip, &lower2));
        }
    }

    // Edge merge constraints
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

    for (int prime = 0; prime < copy_num-1; ++prime) {
        SCIP_CONS* imbalance = nullptr;
        SCIP_CALL(SCIPcreateConsLinear(scip, &imbalance, "imbalance", 0, nullptr, nullptr,
            0.0, SCIPinfinity(scip), true, true, true, true, true, false, false, false, false, false));
        for (int k = 0; k < K; ++k) {
            SCIP_CALL(SCIPaddCoefLinear(scip, imbalance, m.at(make_tuple(k, prime)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, imbalance, m.at(make_tuple(k, prime+1)), -1.0));
        }
        SCIP_CALL(SCIPaddCons(scip, imbalance));
        // SCIP_CALL(SCIPreleaseCons(scip, &onlym));
    }

    for (const auto& edge : edges) {
        int i = edge.first;
        int j = edge.second;
        for (int prime = 0; prime < copy_num; ++prime) {
            for (int k = 0; k < K; ++k) {
                SCIP_CONS* zlower = nullptr;
                SCIP_CALL(SCIPcreateConsLinear(scip, &zlower, "zlower", 0, nullptr, nullptr,
                    -1.0-1e-3, SCIPinfinity(scip), true, true, true, true, true, false, false, false, false, false));
                SCIP_CALL(SCIPaddCoefLinear(scip, zlower, z.at(make_tuple(i, j, prime)), 1.0));
                SCIP_CALL(SCIPaddCoefLinear(scip, zlower, z.at(make_tuple(j, i, prime)), 1.0));
                SCIP_CALL(SCIPaddCoefLinear(scip, zlower, f.at(make_tuple(i, j, k)), -1.0));
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
                -SCIPinfinity(scip), 1.0 - 1e-3, true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq1, y.at(make_tuple(i, prime)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq1, y.at(make_tuple(j, prime)), -1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq1, z.at(make_tuple(i, j, prime)), 1.0));
            SCIP_CALL(SCIPaddCons(scip, topo_seq1));
            // SCIP_CALL(SCIPreleaseCons(scip, &topo_seq1));

            SCIP_CONS* topo_seq2 = nullptr;
            SCIP_CALL(SCIPcreateConsLinear(scip, &topo_seq2, "topo_seq2", 0, nullptr, nullptr,
                -SCIPinfinity(scip), 1.0 - 1e-3, true, true, true, true, true, false, false, false, false, false));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq2, y.at(make_tuple(j, prime)), 1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq2, y.at(make_tuple(i, prime)), -1.0));
            SCIP_CALL(SCIPaddCoefLinear(scip, topo_seq2, z.at(make_tuple(j, i, prime)), 1.0));
            SCIP_CALL(SCIPaddCons(scip, topo_seq2));
            // SCIP_CALL(SCIPreleaseCons(scip, &topo_seq2));

            // Single direction constraint
            // SCIP_CONS* single_dir = nullptr;
            // SCIP_CALL(SCIPcreateConsLinear(scip, &single_dir, "single_dir", 0, nullptr, nullptr,
            //     -SCIPinfinity(scip), 1.0, true, true, true, true, true, false, false, false, false, false));
            // SCIP_CALL(SCIPaddCoefLinear(scip, single_dir, z.at(make_tuple(i, j, prime)), 1.0));
            // SCIP_CALL(SCIPaddCoefLinear(scip, single_dir, z.at(make_tuple(j, i, prime)), 1.0));
            // SCIP_CALL(SCIPaddCons(scip, single_dir));
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

    std::stringstream ss;
    ss << "./save/model_scip_tree_data4+cp" << copy_num << ".lp";
    // std::string filename = ss.str().c_str();
    SCIP_CALL(SCIPwriteOrigProblem(scip, ss.str().c_str(), "lp", FALSE));
    SCIP_CALL(SCIPsolve(scip));
    // SCIP_CALL(SCIPsolveConcurrent(scip));
    

    // Check solution status
    SCIP_STATUS status = SCIPgetStatus(scip);
    double obj_value = SCIPgetPrimalbound(scip);

    switch (status) {
    case SCIP_STATUS_OPTIMAL:
        cout << "OPTIMAL" << endl;
        // SCIP_CALL(SCIPwriteOrigProblem(scip, "model_data4+cp4.lp", "lp", FALSE));
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

    SCIP_SOL* sol = SCIPgetBestSol(scip);
    // Process solution
    for (int k = 0; k < K; ++k) {
        for (int prime = 0; prime < copy_num; ++prime) {
            if (SCIPgetSolVal(scip, sol, m.at(make_tuple(k, prime))) > 0.5) {
                k2prime.emplace(k, prime);
            }
        }
    }

    for (auto& [key, var] : f) {
        if (SCIPgetSolVal(scip, sol, var) > 0.5) {
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
    // double ttl_obj = obj;
    double ttl_obj = 0;
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

        for (const auto& edge : id_to_ret_edges_prime[k]) {
            auto u = edge.first;
            auto v = edge.second;
            int i = u.first;
            int j = v.first;
            edge_weight_sum += edges_to_weight[std::make_pair(i, j)];
        }
        
        ttl_obj += (edge_weight_sum * pair_weight);

        // auto sedge = (s < leaf_to_center[s]) ? std::make_pair(s, leaf_to_center[s]) : std::make_pair(leaf_to_center[s], s);
        // auto tedge = (t < leaf_to_center[t]) ? std::make_pair(t, leaf_to_center[t]) : std::make_pair(leaf_to_center[t], t);
        // ttl_obj += (edges_to_weight[sedge] + edges_to_weight[tedge]) * pair_weight;
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

int print_pairs_to_ret_edges(std::map<int, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>>& pairs_to_ret_edges, 
                             std::map<int, std::string>& id_to_node_name, 
                             const std::string& suffix) {
    std::ofstream outFile("./result/opt_result_" + suffix + "_pair.csv");
    std::stringstream outss;

    if (!outFile.is_open()) {
        std::cerr << "file open fail" << std::endl;
        return 1;
    }
    outFile << "pair_id,start,id,end,id" << std::endl;
    for (const auto& pair2edges : pairs_to_ret_edges) {
        int pair_id = pair2edges.first;
        auto ret_edges = pair2edges.second;
        for (const auto& edge : ret_edges) {
            auto u = edge.first;
            auto v = edge.second;
            int i = u.first;
            int i_prime = u.second+1;
            int j = v.first;
            int j_prime = v.second+1;
            outss << pair_id << "," << id_to_node_name[i] << "," << i_prime << "," << id_to_node_name[j] << "," << j_prime << std::endl;
        }
    }
    outFile << outss.str();
    outFile.close();
    return 0;
}

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
    
    int flag = 1;
    while (!path.empty()) {
        visited.insert(now);
        if (flag == 0) {
            cout << "dead loop in " << pair_id << " " << id_to_node_name.at(now / copy_num) << endl;
            break;
        }
        
        if (now == end_id) {
            return path;
        }
        // cout << now << endl;
        flag = 0;
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
        
    }
    return "";
}

int print_pairs_to_path(std::vector<std::pair<int, int>>& pairs, 
                        std::map<int, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>>& pairs_to_ret_edges, 
                        std::map<int, std::string>& id_to_node_name, 
                        const std::string& suffix) {
    std::ofstream outFile("./result/opt_result_" + suffix + "_pair_path.csv");
    std::stringstream outss;

    if (!outFile.is_open()) {
        std::cerr << "file open fail" << std::endl;
        return 1;
    }
    outFile << "id,start,end,path" << std::endl;
    for (const auto& pair2edges : pairs_to_ret_edges) {
        int pair_id = pair2edges.first;
        auto edges = pair2edges.second;
        const auto& pair = pairs[pair_id];
        int start = pair.first;
        int end = pair.second;
        string path = find_path(pair_id, {start, 0}, {end, 0}, edges, id_to_node_name);
        outss << pair_id+1 << "," << id_to_node_name[start] << "," << id_to_node_name[end] << "," << path << std::endl;
        // outss << id << "," << id_to_node_name[start] << "-1" << "," << id_to_node_name[end] << "-1" << "," << path << std::endl;
    }
    outFile << outss.str();
    outFile.close();
    return 0;
}

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
    std::string file_path1 = "data/edges.csv";    
    std::string file_path2 = "data/pairs.csv";
    // std::string file_path1 = "data/edges-2.csv";    
    // std::string file_path2 = "data/pairs-2.csv";
    // std::string file_path1 = "data/edges-3.csv";    
    // std::string file_path2 = "data/pairs-3-40.csv";
    // std::string file_path1 = "data/edges-4.csv";    
    // std::string file_path2 = "data/pairs-4-246.csv";
    std::string file_path3 = "data/name_to_copynums.csv";
    if (argc > 1) {
        copy_num = std::stoi(argv[1]);
    }
    if (argc > 2) {
        div_part = std::stoi(argv[2]);
        // gap = std::string(argv[2]);
        // if (scale != "1.0") {
        //     file_path1 = "data/edges_" + scale + "x.csv";    
        //     file_path2 = "data/pairs_" + scale + "x.csv";
        // }
    }
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
    std::set<std::pair<int, int>> center_pairs;
    std::map<int, std::pair<int, int>> id_to_center_pairs;
    std::map<std::pair<int, int>, int> center_pairs_to_id;
    std::map<std::pair<int, int>, double> center_pairs_to_weight;
    std::map<int, int> id_to_copynums;
    // std::map<std::pair<int, int>, std::set<std::pair<int, int>>> pairs_to_ret_edges; // k--n
    std::map<std::pair<int, int>, std::set<std::pair<int, int>>> cpairs_to_ret_edges_prime; // k--copy_num*n center
    std::set<std::pair<int, int>> cret_edges_prime;
    std::map<int, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>>> id_to_ret_edges_prime;    // std::set<std::pair<int, int>> ret_edges; // n
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
    int keep_count = id_to_center_pairs.size() / div_part;
    auto it = id_to_center_pairs.begin();
    std::advance(it, keep_count);
    id_to_center_pairs.erase(it, id_to_center_pairs.end());

    adj_list = convertToAdjacencyList(center_edges);
    std::cout << "Solving problem" << std::endl;
    double obj = 0;
    obj = SolveMIPProblem(center_nodes, center_edges, edges_to_weight, id_to_center_pairs, center_pairs_to_weight, 
                                adj_list, cpairs_to_ret_edges_prime, cret_edges_prime, k2prime, id_to_copynums, 
                                entry_edges, cpid_to_entry_edges);
    // try {
    //     obj = SolveMIPProblem(center_nodes, center_edges, edges_to_weight, id_to_center_pairs, center_pairs_to_weight, 
    //                             adj_list, cpairs_to_ret_edges_prime, cret_edges_prime, k2prime, id_to_copynums, 
    //                             entry_edges, cpid_to_entry_edges);
    //     // SolveMIPProblemFromFile(pairs_to_ret_edges, ret_edges, ret_edges_prime, "model-opt.lp");
    // } catch (std::exception e) {
    //     cout << e.what() << endl;
    // } catch (...) {
    //   cout << "Unknown exception occurs!" << endl;
    // }
    // check ret_edges_prime
    ret_edges_prime_adj_list = convertToAdjacencyList(ret_edges_prime);
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
    if (obj <= 0) {
        return 0;
    }
    double total_obj = ret_compose(obj, edges, edges_to_weight, pairs, pairs_to_weight, leaf_to_center, pairs_to_center_pairs, 
                                    center_pairs_to_id, id_to_ret_edges_prime, cpairs_to_ret_edges_prime, ret_edges_prime, k2prime);
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