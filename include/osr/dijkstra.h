#pragma once

#include <variant>

#include "fmt/core.h"

#include "utl/helpers/algorithm.h"

#include "osr/dial.h"
#include "osr/types.h"
#include "osr/ways.h"

namespace osr {

using cost_t = std::uint16_t;

constexpr auto const kInfeasible = std::numeric_limits<cost_t>::max();

enum class direction : std::uint8_t {
  kForward,
  kBackward,
};

constexpr direction opposite(direction const dir) {
  return dir == direction::kForward ? direction::kBackward
                                    : direction::kForward;
}

template <direction Dir>
constexpr direction flip(direction const dir) {
  return Dir == direction::kForward ? dir : opposite(dir);
}

constexpr std::string_view to_str(direction const d) {
  switch (d) {
    case direction::kForward: return "forward";
    case direction::kBackward: return "backward";
  }
  std::unreachable();
}

template <typename Profile>
struct dijkstra {
  using label = typename Profile::label;
  using node = typename Profile::node;
  using entry = typename Profile::entry;
  using hash = typename Profile::hash;

  struct get_bucket {
    cost_t operator()(label const& l) { return l.cost(); }
  };

  void reset(cost_t const max) {
    pq_.clear();
    pq_.n_buckets(max + 1U);
    cost_.clear();
  }

  void add_start(label const l) {
    assert(l.get_node().get_node() != node_idx_t::invalid());
    if (cost_[l.get_node()].update(l.cost(), node::invalid())) {
      push(l);
    }
  }

  cost_t get_cost(node const n) const {
    auto const it = cost_.find(n);
    return it != end(cost_) ? it->second.cost() : kInfeasible;
  }

  void push(label const& l) { pq_.push(l); }

  template <direction SearchDir>
  void run(ways const& w, cost_t const max) {
    while (!pq_.empty()) {
      auto l = pq_.pop();
      if (get_cost(l.get_node()) < l.cost()) {
        continue;
      }

      auto const curr = l.get_node();
      Profile::template adjacent<SearchDir>(
          w, curr, [&](node const neighbor, std::uint32_t const cost) {
            auto const total = l.cost() + cost;
            if (total < max &&
                cost_[neighbor].update(static_cast<cost_t>(total), curr)) {
              assert(neighbor.get_node() != node_idx_t::invalid());
              pq_.push(label{neighbor, static_cast<cost_t>(total)});
            }
          });
    }
  }

  void run(ways const& w, cost_t const max, direction const dir) {
    dir == direction::kForward ? run<direction::kForward>(w, max)
                               : run<direction::kBackward>(w, max);
  }

  dial<label, get_bucket> pq_{get_bucket{}};
  ankerl::unordered_dense::map<node, entry, hash> cost_;
};

}  // namespace osr