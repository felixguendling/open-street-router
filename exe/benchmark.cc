#include <filesystem>
#include <thread>
#include <vector>

#include "boost/asio/executor_work_guard.hpp"
#include "boost/asio/io_context.hpp"

#include "fmt/core.h"
#include "fmt/std.h"

#include "conf/options_parser.h"

#include "utl/timer.h"

#include "osr/lookup.h"
#include "osr/route.h"
#include "osr/ways.h"
#include "osr/weight.h"

namespace fs = std::filesystem;
using namespace osr;

class settings : public conf::configuration {
public:
  explicit settings() : configuration("Options") {
    param(data_dir_, "data,d", "Data directory");
    param(threads_, "threads,t", "Number of routing threads");
    param(n_queries_, "n", "Number of queries");
    param(max_dist_, "r", "Radius");
  }

  fs::path data_dir_{"osr"};
  unsigned n_queries_{100};
  unsigned max_dist_{7200U};
  unsigned threads_{std::thread::hardware_concurrency()};
};

int main(int argc, char const* argv[]) {
  auto opt = settings{};
  auto parser = conf::options_parser({&opt});
  parser.read_command_line_args(argc, argv);

  if (parser.help()) {
    parser.print_help(std::cout);
    return 0;
  } else if (parser.version()) {
    return 0;
  }

  parser.read_configuration_file();
  parser.print_unrecognized(std::cout);
  parser.print_used(std::cout);

  if (!fs::is_directory(opt.data_dir_)) {
    fmt::println("directory not found: {}", opt.data_dir_);
    return 1;
  }

  auto const w = ways{opt.data_dir_, cista::mmap::protection::READ};

  auto timer = utl::scoped_timer{"timer"};
  auto threads = std::vector<std::thread>(std::max(1U, opt.threads_));
  auto i = std::atomic_size_t{0U};
  for (auto& t : threads) {
    t = std::thread([&]() {
      auto s = dijkstra_state{};
      auto h = cista::BASE_HASH;
      auto n = 0U;
      while (i.fetch_add(1U) < opt.n_queries_) {
        auto const start =
            node_idx_t{cista::hash_combine(h, ++n, i.load()) % w.n_nodes()};
        s.reset(opt.max_dist_);
        s.add_start(start, 0U);
        dijkstra(w, s, opt.max_dist_, car{});
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}
