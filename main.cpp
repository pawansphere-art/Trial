#include <iostream>
#include <string>
#include <queue>   
#include <vector>  
#include <unordered_map> // DAY 4: Required for Graph Adjacency List
#include "sqlite3.h"

using namespace std;

// ==========================================
// DAY 4: CITY GRAPH DATA STRUCTURE
// ==========================================

// This class represents our entire city map using an Adjacency List
class CityMap {
public:
    // The Graph: Maps a Node ID (Intersection) to a list of its neighbors.
    // Each neighbor is stored as a C++ 'pair' containing: {Neighbor Node ID, Travel Time in minutes}
    unordered_map<int, vector<pair<int, int>>> adjacency_list;

    // Function to add a bidirectional road between two intersections
    void addRoad(int source, int destination, int travel_time_mins) {
        adjacency_list[source].push_back({destination, travel_time_mins});
        adjacency_list[destination].push_back({source, travel_time_mins});
    }

    // Function to print the graph structure to the terminal
    void displayNetwork() {
        cout << "\n--- CURRENT CITY ROAD NETWORK (ADJACENCY LIST) ---" << endl;
        for (auto const& intersection : adjacency_list) {
            cout << "Intersection [" << intersection.first << "] connects to: " << endl;
            for (auto const& neighbor : intersection.second) {
                cout << "  -> Go to Node [" << neighbor.first << "] takes " << neighbor.second << " mins" << endl;
            }
        }
        cout << "--------------------------------------------------" << endl;
    }
};

// ==========================================
// DAY 2: OBJECT-ORIENTED MODELS
// ==========================================

struct Location {
    int x;
    int y;
    int node_id; // Connects our objects directly to our new Graph Nodes
};

class Ambulance {
public:
    int unit_id;
    Location current_location;
    bool is_available;

    Ambulance(int id, int start_node) {
        unit_id = id;
        current_location.node_id = start_node;
        is_available = true; 
    }
};

struct Emergency {
    int id;
    int severity; 
    Location emergency_location;

    // Operator Overloading for Max-Heap Triage Sorting
    bool operator<(const Emergency& other) const {
        return severity < other.severity;
    }
};

// ==========================================
// DAY 1: DATABASE ENGINE
// ==========================================

void executeSQL(sqlite3* db, const string& sqlCommand, const string& successMessage) {
    char* errorMessage = nullptr;
    int exitCode = sqlite3_exec(db, sqlCommand.c_str(), nullptr, 0, &errorMessage);
    if (exitCode != SQLITE_OK) {
        cerr << "[ERROR] Database Error: " << errorMessage << endl;
        sqlite3_free(errorMessage);
    }
}

void initializeDatabase() {
    sqlite3* db; 
    if (sqlite3_open("dispatch_core.db", &db) != SQLITE_OK) return;
    
    string createAmbulancesTable = "CREATE TABLE IF NOT EXISTS Ambulances (unit_id INTEGER PRIMARY KEY, pos_x INTEGER, pos_y INTEGER, is_available BOOLEAN);";
    string createRoadNetworkTable = "CREATE TABLE IF NOT EXISTS RoadNetwork (edge_id INTEGER PRIMARY KEY AUTOINCREMENT, source_node INTEGER, dest_node INTEGER, travel_time_ms INTEGER);";
    string createDispatchLogsTable = "CREATE TABLE IF NOT EXISTS DispatchLogs (log_id INTEGER PRIMARY KEY AUTOINCREMENT, emergency_id INTEGER, assigned_unit_id INTEGER, travel_time_ms INTEGER, dispatch_timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

    executeSQL(db, createAmbulancesTable, "Ambulances table ready.");
    executeSQL(db, createRoadNetworkTable, "RoadNetwork table ready.");
    executeSQL(db, createDispatchLogsTable, "DispatchLogs table ready.");
    sqlite3_close(db);
}

// ==========================================
// MAIN EXECUTION
// ==========================================

int main() {
    cout << "--- BOOTING EMERGENCY DISPATCH CORE ---" << endl;
    initializeDatabase();
    
    // --- DAY 4: GRAPH NETWORK INITIALIZATION ---
    CityMap city_grid;

    // Building a 4-intersection structural grid layout
    city_grid.addRoad(1, 2, 5);  // Intersection 1 to 2 takes 5 minutes
    city_grid.addRoad(2, 3, 10); // Intersection 2 to 3 takes 10 minutes
    city_grid.addRoad(3, 4, 3);  // Intersection 3 to 4 takes 3 minutes
    city_grid.addRoad(4, 1, 8);  // Intersection 4 to 1 takes 8 minutes

    // Print the memory representation of our graph
    city_grid.displayNetwork();

    // --- DAY 3: MAX-HEAP TRIAGE ENGINE ---
    priority_queue<Emergency> triage_queue;
    
    Emergency call1; call1.id = 90210; call1.severity = 10; call1.emergency_location.node_id = 3;
    Emergency call2; call2.id = 90211; call2.severity = 3;  call2.emergency_location.node_id = 1;

    triage_queue.push(call2);
    triage_queue.push(call1);

    cout << "\n--- PROCESSING TRIAGE QUEUE ---" << endl;
    while (!triage_queue.empty()) {
        Emergency curr = triage_queue.top();
        triage_queue.pop();
        cout << "[TRIAGE] Urgent processing for Call ID: " << curr.id 
             << " | Medical Severity: " << curr.severity << "/10 | At City Intersection: " << curr.emergency_location.node_id << endl;
    }

    cout << "\n--- SYSTEM test ---" << endl;
    return 0;
}