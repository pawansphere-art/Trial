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
// 1. DATABASE ENGINE & MODELS
// ==========================================

void executeSQL(sqlite3* db, const string& sqlCommand, bool silent = true) {
    char* errorMessage = nullptr;
    int exitCode = sqlite3_exec(db, sqlCommand.c_str(), nullptr, 0, &errorMessage);
    if (exitCode != SQLITE_OK && !silent) {
        cerr << "[ERROR] Database Error: " << errorMessage << endl;
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
    int id; int patient_id; int severity; Location emergency_location; // Added patient_id
    bool operator<(const Emergency& other) const { return severity < other.severity; }
};

// ==========================================
// 2. CAPSTONE: BINARY SEARCH TREE (BST)
// ==========================================

// A node in our BST holding a single patient's data
struct PatientNode {
    int patient_id;
    string medical_history;
    PatientNode* left;
    PatientNode* right;
    PatientNode(int id, string history) : patient_id(id), medical_history(history), left(nullptr), right(nullptr) {}
};

class MedicalDatabaseBST {
private:
    PatientNode* root;

    // Recursively finds the correct O(log n) placement for a new record
    PatientNode* insertHelper(PatientNode* node, int id, string history) {
        if (node == nullptr) return new PatientNode(id, history);
        if (id < node->patient_id) node->left = insertHelper(node->left, id, history);
        else if (id > node->patient_id) node->right = insertHelper(node->right, id, history);
        return node;
    }

    // Recursively searches the tree
    string searchHelper(PatientNode* node, int id) {
        if (node == nullptr) return "No prior medical history found.";
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
// 3. CAPSTONE: DYNAMIC GRAPH UPDATES
// ==========================================

class CityMap {
public:
    unordered_map<int, vector<pair<int, int>>> adjacency_list;

    void addRoad(int source, int destination, int travel_time_mins) {
        adjacency_list[source].push_back({destination, travel_time_mins});
        adjacency_list[destination].push_back({source, travel_time_mins});
    }

    // NEW: Find the specific edge in the vector and overwrite the travel time
    void updateRoadTraffic(int source, int destination, int new_time) {
        for (auto& road : adjacency_list[source]) {
            if (road.first == destination) road.second = new_time;
        }
        for (auto& road : adjacency_list[destination]) {
            if (road.first == source) road.second = new_time;
        }
        cout << "[TRAFFIC ALERT] Road from Node " << source << " to Node " << destination << " is now " << new_time << " mins!" << endl;
    }

    int findFastestRoute(int start_node, int target_node) {
        unordered_map<int, int> travel_times;
        unordered_map<int, int> previous_intersection;
        
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
                int road_weight = road.second;
                int new_calculated_time = current_time + road_weight;
                
                if (new_calculated_time < travel_times[next_node]) {
                    travel_times[next_node] = new_calculated_time;
                    previous_intersection[next_node] = current_node; 
                    min_heap.push({new_calculated_time, next_node});
                }
            }
        }
        
        if (travel_times[target_node] == INT_MAX) return -1;
        
        vector<int> path;
        for (int at = target_node; at != 0; at = previous_intersection[at]) {
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
// 4. THE DISPATCH MANAGER
// ==========================================

class DispatchManager {
public:
    CityMap* city_map; 
    MedicalDatabaseBST* medical_db; // Manager now has access to the hospital DB
    sqlite3* db_connection; 
    vector<Ambulance> fleet;
    priority_queue<Emergency> triage_queue;
    mutex triage_mutex; 

    DispatchManager(CityMap* map, MedicalDatabaseBST* med_db, sqlite3* db) { 
        city_map = map; 
        medical_db = med_db;
        db_connection = db;
    }

    void registerAmbulance(Ambulance unit) { fleet.push_back(unit); }

    void receiveCallConcurrent(Emergency call) {
        triage_mutex.lock(); 
        triage_queue.push(call);
        triage_mutex.unlock(); 
    }

    void processDispatch() {
        cout << "\n--- INITIATING AUTONOMOUS DISPATCH ---" << endl;
        
        while (!triage_queue.empty()) {
            Emergency critical_call = triage_queue.top();
            triage_queue.pop();
            bool dispatched = false;
            
            for (auto& unit : fleet) {
                if (unit.is_available) {
                    cout << "\n[DISPATCH] Assigning Unit #" << unit.unit_id << " to Call ID: " << critical_call.id << endl;
                    
                    // NEW: O(log n) BST Medical History Lookup
                    string history = medical_db->getHistory(critical_call.patient_id);
                    cout << "  -> [MEDICAL FILE] Patient ID " << critical_call.patient_id << ": " << history << endl;

                    int eta = city_map->findFastestRoute(unit.current_location.node_id, critical_call.emergency_location.node_id);
                    
                    if (eta != -1) {
                        string sql_query = "INSERT INTO DispatchLogs (emergency_id, assigned_unit_id, travel_time_ms) VALUES (" + 
                                           to_string(critical_call.id) + ", " + to_string(unit.unit_id) + ", " + to_string(eta) + ");";
                        executeSQL(db_connection, sql_query, false);
                    }

                    unit.is_available = false; 
                    dispatched = true;
                    break;
                }
            }
            if (!dispatched) cout << "\n[ALERT] No available ambulances for Call ID: " << critical_call.id << "!" << endl;
        }
    }
};

// ==========================================
// 5. MAIN EXECUTION
// ==========================================

int main() {
    cout << "--- BOOTING EMERGENCY DISPATCH CORE ---" << endl;
    
    sqlite3* system_db = getDatabaseConnection();
    if (!system_db) return 1;
    
    // 1. Build City Map
    CityMap tokyo_district;
    tokyo_district.addRoad(1, 2, 5);  
    tokyo_district.addRoad(2, 3, 10); 
    tokyo_district.addRoad(3, 4, 3);  
    tokyo_district.addRoad(4, 1, 8);  
    
    // 2. DYNAMIC GRAPH UPDATE: Major accident on the fast route!
    cout << "\n--- LIVE SATELLITE FEED ---" << endl;
    tokyo_district.updateRoadTraffic(1, 4, 45); // Route 1->4 jumps from 8 mins to 45 mins

    // 3. Populate Medical BST
    MedicalDatabaseBST med_records;
    med_records.addRecord(555, "Severe Penicillin Allergy");
    med_records.addRecord(777, "Diabetic - Type 1");

    DispatchManager manager(&tokyo_district, &med_records, system_db);
    manager.registerAmbulance(Ambulance(404, 1));
    manager.registerAmbulance(Ambulance(505, 2));

    cout << "\n--- INCOMING TRANSMISSIONS ---" << endl;
    // Notice we are passing a `patient_id` parameter now
    Emergency call1; call1.id = 90210; call1.patient_id = 111; call1.severity = 3;  call1.emergency_location.node_id = 3; 
    Emergency call2; call2.id = 90211; call2.patient_id = 555; call2.severity = 10; call2.emergency_location.node_id = 4; 
    Emergency call3; call3.id = 90212; call3.patient_id = 777; call3.severity = 8;  call3.emergency_location.node_id = 2; 

    thread op1(&DispatchManager::receiveCallConcurrent, &manager, call1);
    thread op2(&DispatchManager::receiveCallConcurrent, &manager, call2);
    thread op3(&DispatchManager::receiveCallConcurrent, &manager, call3);

    op1.join(); op2.join(); op3.join();

    manager.processDispatch();
    
    sqlite3_close(system_db);
    cout << "\n--- SYSTEM READY ---" << endl;
    return 0;
}