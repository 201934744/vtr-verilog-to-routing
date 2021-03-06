#ifndef ROUTER_LOOKAHEAD_MAP_UTILS_H_
#define ROUTER_LOOKAHEAD_MAP_UTILS_H_
/*
 * The router lookahead provides an estimate of the cost from an intermediate node to the target node
 * during directed (A*-like) routing.
 *
 * The VPR 7.0 lookahead (route/route_timing.c ==> get_timing_driven_expected_cost) lower-bounds the remaining delay and
 * congestion by assuming that a minimum number of wires, of the same type as the current node being expanded, can be used
 * to complete the route. While this method is efficient, it can run into trouble with architectures that use
 * multiple interconnected wire types.
 *
 * The lookahead in this file pre-computes delay/congestion costs up and to the right of a starting tile. This generates
 * delay/congestion tables for {CHANX, CHANY} channel types, over all wire types defined in the architecture file.
 * See Section 3.2.4 in Oleg Petelin's MASc thesis (2016) for more discussion.
 *
 */

#include <cmath>
#include <limits>
#include <vector>
#include <queue>
#include <unordered_map>
#include "vpr_types.h"
#include "rr_node.h"

namespace util {

/* when a list of delay/congestion entries at a coordinate in Cost_Entry is boiled down to a single
 * representative entry, this enum is passed-in to specify how that representative entry should be
 * calculated */
enum e_representative_entry_method {
    FIRST = 0, //the first cost that was recorded
    SMALLEST,  //the smallest-delay cost recorded
    AVERAGE,
    GEOMEAN,
    MEDIAN
};

/* f_cost_map is an array of these cost entries that specifies delay/congestion estimates
 * to travel relative x/y distances */
class Cost_Entry {
  public:
    float delay;
    float congestion;
    bool fill;

    Cost_Entry() {
        delay = std::numeric_limits<float>::infinity();
        congestion = std::numeric_limits<float>::infinity();
        fill = false;
    }
    Cost_Entry(float set_delay, float set_congestion)
        : delay(set_delay)
        , congestion(set_congestion)
        , fill(false) {}
    Cost_Entry(float set_delay, float set_congestion, bool set_fill)
        : delay(set_delay)
        , congestion(set_congestion)
        , fill(set_fill) {}
    bool valid() const {
        return std::isfinite(delay) && std::isfinite(congestion);
    }
};

/* a class that stores delay/congestion information for a given relative coordinate during the Dijkstra expansion.
 * since it stores multiple cost entries, it is later boiled down to a single representative cost entry to be stored
 * in the final lookahead cost map */
class Expansion_Cost_Entry {
  private:
    std::vector<Cost_Entry> cost_vector;

    Cost_Entry get_smallest_entry() const;
    Cost_Entry get_average_entry() const;
    Cost_Entry get_geomean_entry() const;
    Cost_Entry get_median_entry() const;

  public:
    void add_cost_entry(e_representative_entry_method method,
                        float add_delay,
                        float add_congestion) {
        Cost_Entry cost_entry(add_delay, add_congestion);
        if (method == SMALLEST) {
            /* taking the smallest-delay entry anyway, so no need to push back multple entries */
            if (this->cost_vector.empty()) {
                this->cost_vector.push_back(cost_entry);
            } else {
                if (add_delay < this->cost_vector[0].delay) {
                    this->cost_vector[0] = cost_entry;
                }
            }
        } else {
            this->cost_vector.push_back(cost_entry);
        }
    }
    void clear_cost_entries() {
        this->cost_vector.clear();
    }

    Cost_Entry get_representative_cost_entry(e_representative_entry_method method) const {
        Cost_Entry entry;

        if (!cost_vector.empty()) {
            switch (method) {
                case FIRST:
                    entry = cost_vector[0];
                    break;
                case SMALLEST:
                    entry = this->get_smallest_entry();
                    break;
                case AVERAGE:
                    entry = this->get_average_entry();
                    break;
                case GEOMEAN:
                    entry = this->get_geomean_entry();
                    break;
                case MEDIAN:
                    entry = this->get_median_entry();
                    break;
                default:
                    break;
            }
        }
        return entry;
    }
};

/* a class that represents an entry in the Dijkstra expansion priority queue */
class PQ_Entry {
  public:
    int rr_node_ind; //index in device_ctx.rr_nodes that this entry represents
    float cost;      //the cost of the path to get to this node

    /* store backward delay, R and congestion info */
    float delay;
    float R_upstream;
    float congestion_upstream;

    PQ_Entry(int set_rr_node_ind, int /*switch_ind*/, float parent_delay, float parent_R_upstream, float parent_congestion_upstream, bool starting_node, float Tsw_adjust);

    bool operator<(const PQ_Entry& obj) const {
        /* inserted into max priority queue so want queue entries with a lower cost to be greater */
        return (this->cost > obj.cost);
    }
};

// A version of PQ_Entry that only calculates and stores the delay.
class PQ_Entry_Delay {
  public:
    int rr_node_ind;  //index in device_ctx.rr_nodes that this entry represents
    float delay_cost; //the cost of the path to get to this node

    PQ_Entry_Delay(int set_rr_node_ind, int /*switch_ind*/, const PQ_Entry_Delay* parent);

    float cost() const {
        return delay_cost;
    }

    void adjust_Tsw(float amount) {
        delay_cost += amount;
    }

    bool operator>(const PQ_Entry_Delay& obj) const {
        return (this->delay_cost > obj.delay_cost);
    }
};

// A version of PQ_Entry that only calculates and stores the base cost.
class PQ_Entry_Base_Cost {
  public:
    int rr_node_ind; //index in device_ctx.rr_nodes that this entry represents
    float base_cost;

    PQ_Entry_Base_Cost(int set_rr_node_ind, int /*switch_ind*/, const PQ_Entry_Base_Cost* parent);

    float cost() const {
        return base_cost;
    }

    void adjust_Tsw(float /* amount */) {
        // do nothing
    }

    bool operator>(const PQ_Entry_Base_Cost& obj) const {
        return (this->base_cost > obj.base_cost);
    }
};

struct Search_Path {
    float cost;
    int parent;
    int edge;
};

/* iterates over the children of the specified node and selectively pushes them onto the priority queue */
template<typename Entry>
void expand_dijkstra_neighbours(const t_rr_graph_storage& rr_nodes,
                                const Entry& parent_entry,
                                std::vector<Search_Path>* paths,
                                std::vector<bool>* node_expanded,
                                std::priority_queue<Entry,
                                                    std::vector<Entry>,
                                                    std::greater<Entry>>* pq);

} // namespace util

#endif
