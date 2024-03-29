#pragma once

#include "osr/ways.h"

#include "utl/helpers/algorithm.h"
#include "utl/pairwise.h"

#include "dijkstra.h"
#include "rtree.h"

namespace osr {

struct node_candidate {
  bool valid() const { return node_ != node_idx_t::invalid(); }

  node_idx_t node_{node_idx_t::invalid()};
  double dist_to_node_{0.0};
  dist_t weight_{0U};
  std::vector<geo::latlng> path_{};
};

struct way_candidate {
  friend bool operator<(way_candidate const& a, way_candidate const& b) {
    return a.dist_to_way_ < b.dist_to_way_;
  }

  double dist_to_way_;
  geo::latlng best_;
  std::size_t segment_idx_;
  way_idx_t way_{way_idx_t::invalid()};
  node_candidate left_{}, right_{};
};

enum class cflow { kContinue, kBreak };

template <typename T, typename Collection, typename Fn>
void till_the_end(T const& start,
                  Collection const& c,
                  direction const dir,
                  Fn&& fn) {
  if (dir == direction::kForward) {
    for (auto i = start; i != c.size(); ++i) {
      if (fn(c[i]) == cflow::kBreak) {
        break;
      }
    }
  } else {
    for (auto j = 0U; j <= start; ++j) {
      auto i = start - j;
      if (fn(c[i]) == cflow::kBreak) {
        break;
      }
    }
  }
}

using match_t = std::vector<way_candidate>;

template <typename PolyLine>
way_candidate distance_to_way(geo::latlng const& x, PolyLine&& c) {
  auto min = std::numeric_limits<double>::max();
  auto best = geo::latlng{};
  auto best_segment_idx = 0U;
  auto segment_idx = 0U;
  for (auto const [a, b] : utl::pairwise(c)) {
    auto const candidate = geo::closest_on_segment(x, a, b);
    auto const dist = geo::distance(x, candidate);
    if (dist < min) {
      min = dist;
      best = candidate;
      best_segment_idx = segment_idx;
    }
    ++segment_idx;
  }
  return {.dist_to_way_ = min, .best_ = best, .segment_idx_ = best_segment_idx};
}

struct lookup {
  lookup(ways const& ways) : rtree_{rtree_new()}, ways_{ways} {
    utl::verify(rtree_ != nullptr, "rtree creation failed");
    for (auto i = way_idx_t{0U}; i != ways.n_ways(); ++i) {
      insert(i);
    }
  }

  ~lookup() { rtree_free(rtree_); }

  match_t get_match(geo::latlng const&) { return {}; }

  template <typename WeightFn>
  match_t match(geo::latlng const& query,
                bool const reverse,
                WeightFn&& fn) const {
    auto way_candidates = std::vector<way_candidate>{};
    find(query, [&](way_idx_t const way) {
      if (fn(ways_.way_properties_[way], direction::kForward, 0U) !=
          kInfeasible) {
        auto d = distance_to_way(query, ways_.way_polylines_[way]);
        if (d.dist_to_way_ < 100) {
          auto& wc = way_candidates.emplace_back(std::move(d));
          wc.way_ = way;
          wc.left_ =
              find_next_node(wc, query, direction::kBackward, reverse, fn);
          wc.right_ =
              find_next_node(wc, query, direction::kForward, reverse, fn);
        }
      }
    });
    utl::sort(way_candidates);
    return way_candidates;
  }

  template <typename EdgeWeightFn>
  node_candidate find_next_node(way_candidate const& wc,
                                geo::latlng const& query,
                                direction const dir,
                                bool const reverse,
                                EdgeWeightFn&& edge_weight) const {
    auto const p = ways_.way_properties_[wc.way_];
    auto const edge_dir = reverse ? opposite(dir) : dir;
    if (edge_weight(p, edge_dir, 0U) == kInfeasible) {
      return node_candidate{};
    }

    auto const off_road_length = geo::distance(query, wc.best_);
    auto c = node_candidate{.dist_to_node_ = off_road_length,
                            .weight_ = edge_weight(p, edge_dir, 0U),
                            .path_ = {query, wc.best_}};
    auto const polyline = ways_.way_polylines_[wc.way_];
    auto const osm_nodes = ways_.way_osm_nodes_[wc.way_];

    till_the_end(wc.segment_idx_ + (dir == direction::kForward ? 1U : 0U),
                 utl::zip(polyline, osm_nodes), dir, [&](auto&& x) {
                   auto const& [pos, osm_node_idx] = x;

                   auto const segment_dist = geo::distance(c.path_.back(), pos);
                   c.dist_to_node_ += segment_dist;
                   c.weight_ += edge_weight(p, edge_dir, segment_dist);
                   c.path_.push_back(pos);

                   auto const way_node = ways_.find_node_idx(osm_node_idx);
                   if (way_node.has_value()) {
                     c.node_ = *way_node;
                     return cflow::kBreak;
                   }

                   return cflow::kContinue;
                 });

    if (!reverse) {
      std::reverse(begin(c.path_), end(c.path_));
    }

    return c;
  }

  template <typename Fn>
  void find(geo::latlng const& x, Fn&& fn) const {
    find({x.lat() - 0.01, x.lng() - 0.01}, {x.lat() + 0.01, x.lng() + 0.01},
         std::forward<Fn>(fn));
  }

  template <typename Fn>
  void find(geo::latlng const& a, geo::latlng const& b, Fn&& fn) const {
    auto const min = std::array<double, 2U>{std::min(a.lng_, b.lng_),
                                            std::min(a.lat_, b.lat_)};
    auto const max = std::array<double, 2U>{std::max(a.lng_, b.lng_),
                                            std::max(a.lat_, b.lat_)};
    rtree_search(
        rtree_, min.data(), max.data(),
        [](double const* /* min */, double const* /* max */, void const* item,
           void* udata) {
          (*reinterpret_cast<Fn*>(udata))(
              way_idx_t{static_cast<way_idx_t::value_t>(
                  reinterpret_cast<std::size_t>(item))});
          return true;
        },
        &fn);
  }

  void insert(way_idx_t const way) {
    auto b = osmium::Box{};
    for (auto const& c : ways_.way_polylines_[way]) {
      b.extend(osmium::Location{c.lat_, c.lng_});
    }

    auto const min_corner =
        std::array<double, 2U>{b.bottom_left().lon(), b.bottom_left().lat()};
    auto const max_corner =
        std::array<double, 2U>{b.top_right().lon(), b.top_right().lat()};

    rtree_insert(rtree_, min_corner.data(), max_corner.data(),
                 reinterpret_cast<void*>(to_idx(way)));
  }

  rtree* rtree_;
  ways const& ways_;
};

}  // namespace osr