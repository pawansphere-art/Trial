#include <iostream>
#include <string>
#include <queue>   
#include <vector>  
#include <unordered_map> 
#include <climits>   
#include <algorithm> 
#include <thread> 
#include <mutex>  
#include "sqlite3.h"

using namespace std;

// ==========================================
// 1. DATABASE & MODELS
// ==========================================
void executeSQL(sqlite3* db, const string& sqlCommand, bool silent = true) {
    char* errorMessage = nullptr;
    int exitCode = sqlite3_exec(db, sqlCommand.c_str(), nullptr, 0, &errorMessage);
    if (exitCode != SQLITE_OK && !silent) {
        cerr << "[ERROR] DB: " << errorMessage << endl;
        sqlite3_free(errorMessage);
    }
}

sqlite3* getDatabaseConnection() {
    sqlite3* db; 
    if (sqlite3_open("dispatch_core.db", &db) != SQLITE_OK) return nullptr;
    executeSQL(db, "CREATE TABLE IF NOT EXISTS Ambulances (unit_id INTEGER PRIMARY KEY);");
    executeSQL(db, "CREATE TABLE IF NOT EXISTS DispatchLogs (log_id INTEGER PRIMARY KEY AUTOINCREMENT, emergency_id INTEGER, assigned_unit_id INTEGER, travel_time_ms INTEGER, dispatch_timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);");
    return db;
}

struct Location { int node_id; };

class Ambulance {
public:
    int unit_id; Location current_location; bool is_available;
    Ambulance(int id, int start_node) { unit_id = id; current_location.node_id = start_node; is_available = true; }
};

struct Emergency {
    int id; int patient_id; int severity; Location emergency_location;
    bool operator<(const Emergency& other) const { return severity < other.severity; }
};

// ==========================================
// 2. BINARY SEARCH TREE (BST)
// ==========================================
struct PatientNode {
    int patient_id; string medical_history;
    PatientNode *left, *right;
    PatientNode(int id, string history) : patient_id(id), medical_history(history), left(nullptr), right(nullptr) {}
};

class MedicalDatabaseBST {
    PatientNode* root;
    PatientNode* insertHelper(PatientNode* node, int id, string history) {
        if (!node) return new PatientNode(id, history);
        if (id < node->patient_id) node->left = insertHelper(node->left, id, history);
        else if (id > node->patient_id) node->right = insertHelper(node->right, id, history);
        return node;
    }
    string searchHelper(PatientNode* node, int id) {
        if (!node) return "No prior history.";
        if (node->patient_id == id) return node->medical_history;
        if (id < node->patient_id) return searchHelper(node->left, id);
        return searchHelper(node->right, id);
    }
public:
    MedicalDatabaseBST() { root = nullptr; }
    void addRecord(int id, string history) { root = insertHelper(root, id, history); }
    string getHistory(int id) { return searchHelper(root, id); }
};

// ==========================================
// 3. DISJOINT SET (O(1) Connectivity Check)
// ==========================================
class DisjointSet {
    vector<int> parent, rank;
public:
    DisjointSet(int n) {
        parent.resize(n + 1); rank.resize(n + 1, 0);
        for (int i = 0; i <= n; i++) parent[i] = i; 
    }
    int findUltimateParent(int node) {
        if (node == parent[node]) return node;
        return parent[node] = findUltimateParent(parent[node]); 
    }
    void unionByRank(int u, int v) {
        int ult_u = findUltimateParent(u), ult_v = findUltimateParent(v);
        if (ult_u == ult_v) return;
        if (rank[ult_u] < rank[ult_v]) parent[ult_u] = ult_v;
        else if (rank[ult_v] < rank[ult_u]) parent[ult_v] = ult_u;
        else { parent[ult_v] = ult_u; rank[ult_u]++; }
    }
};

// ==========================================
// 4. GRAPH & PATHFINDING
// ==========================================
class CityMap {
public:
    unordered_map<int, vector<pair<int, int>>> adjacency_list;
    DisjointSet ds; 

    CityMap(int total_nodes) : ds(total_nodes) {}

    void addRoad(int source, int destination, int travel_time_mins) {
        adjacency_list[source].push_back({destination, travel_time_mins});
        adjacency_list[destination].push_back({source, travel_time_mins});
        ds.unionByRank(source, destination); 
    }

    int findFastestRoute(int start_node, int target_node) {
        // NEW: O(1) Abort if nodes are disconnected (saves massive CPU time)
        if (ds.findUltimateParent(start_node) != ds.findUltimateParent(target_node)) {
            cout << "  -> [FATAL ERROR] No physical road exists to Node " << target_node << ". DIJKSTRA ABORTED." << endl;
            return -1;
        }

        unordered_map<int, int> travel_times;
        unordered_map<int, int> previous_node;
        for (auto const& pair : adjacency_list) travel_times[pair.first] = INT_MAX;
        
        priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> min_heap;
        travel_times[start_node] = 0;
        min_heap.push({0, start_node}); 
        
        while (!min_heap.empty()) {
            int current_time = min_heap.top().first;
            int current_node = min_heap.top().second;
            min_heap.pop();
            if (current_node == target_node) break;
            
            for (auto const& road : adjacency_list[current_node]) {
                int next_node = road.first;
                int new_time = current_time + road.second;
                if (new_time < travel_times[next_node]) {
                    travel_times[next_node] = new_time;
                    previous_node[next_node] = current_node; 
                    min_heap.push({new_time, next_node});
                }
            }
        }
        
        vector<int> path;
        for (int at = target_node; at != 0; at = previous_node[at]) {
            path.push_back(at);
            if (at == start_node) break;
        }
        reverse(path.begin(), path.end()); 
        
        cout << "  -> [GPS ROUTING] Path: ";
        for (size_t i = 0; i < path.size(); i++) cout << "Node " << path[i] << (i == path.size() - 1 ? "" : " -> ");
        cout << " | ETA: " << travel_times[target_node] << " mins" << endl;
        return travel_times[target_node];
    }
};

// ==========================================
// 5. DISPATCH MANAGER
// ==========================================
class DispatchManager {
public:
    CityMap* city_map; MedicalDatabaseBST* medical_db; sqlite3* db_connection; 
    vector<Ambulance> fleet; priority_queue<Emergency> triage_queue; mutex triage_mutex; 

    DispatchManager(CityMap* map, MedicalDatabaseBST* med_db, sqlite3* db) { 
        city_map = map; medical_db = med_db; db_connection = db;
    }
    void registerAmbulance(Ambulance unit) { fleet.push_back(unit); }
    void receiveCallConcurrent(Emergency call) {
        lock_guard<mutex> lock(triage_mutex); 
        triage_queue.push(call);
    }
    void processDispatch() {
        cout << "\n--- INITIATING AUTONOMOUS DISPATCH ---" << endl;
        while (!triage_queue.empty()) {
            Emergency call = triage_queue.top(); triage_queue.pop();
            bool dispatched = false;
            
            for (auto& unit : fleet) {
                if (unit.is_available) {
                    cout << "\n[DISPATCH] Assigning Unit #" << unit.unit_id << " to Call ID: " << call.id << " (Severity: " << call.severity << ")" << endl;
                    cout << "  -> [MEDICAL FILE] " << medical_db->getHistory(call.patient_id) << endl;

                    int eta = city_map->findFastestRoute(unit.current_location.node_id, call.emergency_location.node_id);
                    if (eta != -1) {
                        string sql = "INSERT INTO DispatchLogs (emergency_id, assigned_unit_id, travel_time_ms) VALUES (" + to_string(call.id) + ", " + to_string(unit.unit_id) + ", " + to_string(eta) + ");";
                        executeSQL(db_connection, sql, false);
                        unit.is_available = false; 
                    } else {
                        cout << "  -> [RE-ROUTING] Unit #" << unit.unit_id << " remains available. Dispatch failed." << endl;
                    }
                    dispatched = true;
                    break;
                }
            }
        }
    }
};

// ==========================================
// 6. MAIN EXECUTION
// ==========================================
int main() {
    sqlite3* system_db = getDatabaseConnection();
    CityMap tokyo_district(10); // Graph handles up to 10 nodes
    
    // City Network (Nodes 1, 2, 3, 4 are connected)
    tokyo_district.addRoad(1, 2, 5);  
    tokyo_district.addRoad(2, 3, 10); 
    tokyo_district.addRoad(3, 4, 3);  
    tokyo_district.addRoad(4, 1, 8);  
    
    // ISOLATED ISLAND (Nodes 8 and 9 are connected to each other, but NOT to the city)
    tokyo_district.addRoad(8, 9, 2);

    MedicalDatabaseBST med_records;
    med_records.addRecord(111, "Patient 111: No known allergies.");
    med_records.addRecord(999, "Patient 999: Severe Asthma.");

    DispatchManager manager(&tokyo_district, &med_records, system_db);
    manager.registerAmbulance(Ambulance(404, 1)); // At node 1
    manager.registerAmbulance(Ambulance(505, 2)); // At node 2

    // 989 Calls
    Emergency call1; call1.id = 90210; call1.patient_id = 111; call1.severity = 5; call1.emergency_location.node_id = 4; // Standard call
    Emergency call2; call2.id = 90211; call2.patient_id = 999; call2.severity = 10; call2.emergency_location.node_id = 9; // Call from ISOLATED ISLAND!

    thread op1(&DispatchManager::receiveCallConcurrent, &manager, call1);
    thread op2(&DispatchManager::receiveCallConcurrent, &manager, call2);
    op1.join(); op2.join();

    manager.processDispatch();
    sqlite3_close(system_db);
    return 0;
}