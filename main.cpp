#include <iostream>
#include <string>
#include <queue>   
#include <vector>  
#include <unordered_map> // NEW DAY 4: Required for Graph Adjacency List
#include "sqlite3.h"

using namespace std;

// ==========================================
// DAY 4: CITY GRAPH DATA STRUCTURE
// ==========================================

// This class represents our entire city map using an Adjacency List
class CityMap {
public:
    // The Graph: Maps a Node ID to a list of its neighbors (Neighbor ID, Travel Time in mins)
    unordered_map<int, vector<pair<int, int>>> adjacency_list;

    // Function to build the roads
    void addRoad(int source, int destination, int travel_time_mins) {
        // We are assuming 2-way streets for now, so we connect them both ways
        adjacency_list[source].push_back({destination, travel_time_mins});
        adjacency_list[destination].push_back({source, travel_time_mins});
    }

    // Function to print the graph and prove it works
    void displayNetwork() {
        cout << "\n--- CURRENT CITY ROAD NETWORK ---" << endl;
        for (auto const& intersection : adjacency_list) {
            cout << "Intersection " << intersection.first << " connects to: ";
            for (auto const& neighbor : intersection.second) {
                cout << "[Node " << neighbor.first << " (" << neighbor.second << " mins)] ";
            }
            cout << endl;
        }
    }
};

// ==========================================
// DAY 2: OBJECT-ORIENTED MODELS
// ==========================================

struct Location {
    int x;
    int y;
    int node_id; // We will soon use this to snap emergencies to Graph Intersections!
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
    
    // --- DAY 4: GRAPH NETWORK TESTING ---
    CityMap tokyo_district;

    // Building a small 4-intersection grid
    tokyo_district.addRoad(1, 2, 5);  // Node 1 to Node 2 takes 5 mins
    tokyo_district.addRoad(2, 3, 10); // Node 2 to Node 3 takes 10 mins
    tokyo_district.addRoad(3, 4, 3);  // Node 3 to Node 4 takes 3 mins
    tokyo_district.addRoad(4, 1, 8);  // Node 4 to Node 1 takes 8 mins

    // Print the graph to the terminal
    tokyo_district.displayNetwork();

    // --- DAY 3: MAX-HEAP TRIAGE ENGINE ---
    priority_queue<Emergency> triage_queue;
    
    Emergency call1; call1.id = 90210; call1.severity = 10; call1.emergency_location.node_id = 3;
    Emergency call2; call2.id = 90211; call2.severity = 3;  call2.emergency_location.node_id = 1;

    triage_queue.push(call2);
    triage_queue.push(call1);

    cout << "\n--- DISPATCHING UNITS ---" << endl;
    while (!triage_queue.empty()) {
        Emergency curr = triage_queue.top();
        triage_queue.pop();
        cout << "[DISPATCH] Assigning ambulance to Call ID: " << curr.id 
             << " | Severity: " << curr.severity << "/10 | Location Node: " << curr.emergency_location.node_id << endl;
    }

    cout << "\n--- SYSTEM READY ---" << endl;
    return 0;
}