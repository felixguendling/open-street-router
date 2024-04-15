#pragma once

#include <cinttypes>

#include "osr/types.h"

namespace osr {

struct multi_counter {
  using size_type = bitvec64::size_type;

  bool is_multi(size_type const i) const { return multi_[i]; }

  void increment(size_type const i) {
    once_.resize(std::max(once_.size(), i + 1U));
    multi_.resize(std::max(multi_.size(), i + 1U));
    if (once_[i]) {
      if (!multi_[i]) {
        multi_.set(i, true);
      }
    } else {
      once_.set(i, true);
    }
  }

  size_type size() const noexcept { return once_.size(); }

  void reserve(size_type const size) {
    once_.blocks_.reserve(size / bitvec64::bits_per_block);
    multi_.blocks_.reserve(size / bitvec64::bits_per_block);
  }

  bitvec64 once_, multi_;
};

}  // namespace osr