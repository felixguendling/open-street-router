#pragma once

#include <string>
#include <vector>

#include "geo/latlng.h"

namespace osr {

struct ways;
struct dijkstra_state;

std::string to_geojson(ways const&,
                       dijkstra_state const*,
                       std::vector<geo::latlng> const* start_left_path,
                       std::vector<geo::latlng> const* start_right_path);

}  // namespace osr