#include "../../epiworld.hpp"
#include <iostream>
#include <vector>
#include <string>

using namespace epiworld;

enum States : size_t {
    Susceptible = 0u,
    Infected,
    Infected_Hospitalized
};

enum Locations : size_t {
    Community = 0u,
    Hospital,
    Home
};
/**
 * 
 * infection would not occur in same places
 *
 */
auto sampler_suscept_same_location = [](Agent<int> *p, Model<int> *m, const std::vector<size_t> &locations) -> VirusPtr<int> {
    // Ensure the agent is still susceptible
    if (p->get_state() != States::Susceptible) {
        return nullptr; // Already infected, no further processing
    }

    size_t agent_id = p->get_id();
    size_t agent_location = locations[agent_id];

    // Get neighbors
    auto neighbors = p->get_neighbors();

    // Filter neighbors by location and state
    std::vector<Agent<int> *> potential_infectors;
    for (auto *neighbor : neighbors) {
        if (locations[neighbor->get_id()] == agent_location && 
            neighbor->get_state() == States::Infected) {
            potential_infectors.push_back(neighbor);
        }
    }

    // Sample a virus from the potential infectors
    if (!potential_infectors.empty()) {
        size_t infector_idx = static_cast<size_t>(m->runif() * potential_infectors.size());
        return potential_infectors[infector_idx]->get_virus();
    }

    return nullptr; // No potential infectors
};




/**
 * - Susceptibles are randomly distributed across Community, Hospital, and Home.
 * - Infection occurs only from individuals in the same location.
 * - Once they become infected, they may be hospitalized or not.
 */
std::vector<std::tuple<size_t, size_t, size_t>> infection_log; // Track infections (Susceptible_ID, Infected_ID, Location)

inline void update_susceptible(Agent<int> *p, Model<int> *m, std::vector<size_t> &locations) {
    // Check if the agent is still susceptible
    if (p->get_state() != States::Susceptible) {
        return; // Skip if already infected
    }

    size_t agent_id = p->get_id();

    // Randomly assign a location (Community, Hospital, Home)
    size_t new_location = static_cast<size_t>(m->runif() * 3); // Random value: 0, 1, or 2
    locations[agent_id] = new_location;

    VirusPtr<int> virus = nullptr; // Reset virus infection attempt
    size_t infector_id = 0;        // To log the infector ID

    // Loop through all neighbors to check for infection
    for (auto *neighbor : p->get_neighbors()) {
        if (neighbor->get_state() == States::Infected &&
            locations[neighbor->get_id()] == locations[agent_id]) { // Same location
            // Each infected neighbor has a 30% chance to infect
            if (m->runif() < 0.3) { 
                virus = neighbor->get_virus(); // Virus passed
                infector_id = neighbor->get_id(); // Log infector's ID
                break; // Infection occurs; stop checking further neighbors
            }
        }
    }

    // If an infection occurred, log and update the agent's state
    if (virus != nullptr) {
        infection_log.push_back({agent_id, infector_id, new_location}); // Log the infection event
        if (m->par("Prob hospitalization") > m->runif()) {
            p->set_virus(*virus, m, States::Infected_Hospitalized); // Hospitalized
        } else {
            p->set_virus(*virus, m, States::Infected); // Regular infection
        }
    }
}






/**
 * Infected individuals may:
 * 
 * - Stay the same
 * - Recover
 * - Be hospitalized
 */
inline void update_infected(Agent<int> *p, Model<int> *m, std::vector<size_t> &locations) {
    size_t agent_id = p->get_id();

    // Randomly assign a location (Community or Home)
    size_t new_location = (m->runif() < 0.5) ? Locations::Community : Locations::Home;
    locations[agent_id] = new_location;

    // Vector of probabilities
    std::vector<epiworld_double> probs = {
        m->par("Prob hospitalization"),
        m->par("Prob recovery")
    };

    // Sampling:
    // - (-1) Nothing happens
    // - (0) Hospitalization
    // - (1) Recovery
    int res = roulette<>(probs, m);

    if (res == 0)
        p->change_state(m, States::Infected_Hospitalized);
    else if (res == 1)
        p->rm_virus(m, States::Susceptible);
}

/**
 * Infected individuals who are hospitalized may:
 * - Stay infected.
 * - Recover (and then be discharged)
 * - Stay the same and be discharged.
 */
inline void update_infected_hospitalized(Agent<int> *p, Model<int> *m, std::vector<size_t> &locations) {
    size_t agent_id = p->get_id();

    // Always in Hospital
    locations[agent_id] = Locations::Hospital;

    if (m->par("Prob recovery") > m->runif()) {
        p->rm_virus(m, States::Susceptible);
    } else if (m->par("Discharge infected") > m->runif()) {
        p->change_state(m, States::Infected);
    }
}

int main() {
    
    Model<int> model;

    // Locations vector to track each agent's location
    std::vector<size_t> locations(1000);
    for (size_t i = 0; i < locations.size(); ++i) {
        locations[i] = static_cast<size_t>(model.runif() * 3); // Randomly assign 0, 1, or 2
    }

    model.add_state("Susceptible", [&](Agent<int> *p, Model<int> *m) {
        update_susceptible(p, m, locations);
    });
    model.add_state("Infected", [&](Agent<int> *p, Model<int> *m) {
        update_infected(p, m, locations);
    });
    model.add_state("Infected (hospitalized)", [&](Agent<int> *p, Model<int> *m) {
        update_infected_hospitalized(p, m, locations);
    });

    // Adding a new virus
    Virus<int> mrsa("MRSA");
    mrsa.set_state(1, 0, 0);
    mrsa.set_prob_infecting(0.1);
    mrsa.set_prob_recovery(.0);
    mrsa.set_distribution(distribute_virus_randomly<>(0.01));

    model.add_virus(mrsa);

    // Add a population
    model.agents_smallworld(1000, 4, 0.1, false); // 1000 agents, average 4 neighbors, 10% randomness, undirected

    model.add_param(0.1, "Prob hospitalization");
    model.add_param(0.0, "Prob recovery");
    model.add_param(0.1, "Discharge infected");

    // Run the model
    model.run(100, 1231);


    std::cout << "\nInfection Events:\n";
    for (const auto &entry : infection_log) {
        size_t susceptible_id, infected_id, location;
        std::tie(susceptible_id, infected_id, location) = entry;
        std::string loc_name = (location == Locations::Community ? "Community" : 
                            (location == Locations::Hospital ? "Hospital" : "Home"));
        std::cout << "Susceptible Agent " << susceptible_id 
                << " infected by Agent " << infected_id 
                << " in " << loc_name << "\n";
}


    // Print the model details
    model.print();

    // Count agents in each location for each state
    std::vector<std::vector<size_t>> state_location_counts(3, std::vector<size_t>(3, 0));
    for (size_t i = 0; i < locations.size(); ++i) {
        size_t state = model.get_agents()[i].get_state();
        size_t location = locations[i];
        state_location_counts[state][location]++;
    }

    // Define state names for better readability
    std::vector<std::string> state_names = {"Susceptible", "Infected", "Infected_Hospitalized"};

    // Print the distribution of agents across locations
    std::cout << "\nLocation-wise distribution of states:\n";
    for (size_t state = 0; state < state_location_counts.size(); ++state) {
        std::cout << "  " << state_names[state] << ":\n"; 
        for (size_t loc = 0; loc < state_location_counts[state].size(); ++loc) {
            std::string loc_name = (loc == Locations::Community ? "Community" : (loc == Locations::Hospital ? "Hospital" : "Home"));
            std::cout << "    " << loc_name << ": " << state_location_counts[state][loc] << "\n";
        }
    }

    return 0;
}
