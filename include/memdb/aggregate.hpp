#pragma once

#include "value.hpp"

#include <cstddef>
#include <optional>

namespace memdb {

enum class AggOp { Min, Max, Sum, Avg, Count };

struct GroupAggregate {
    Value key;
    std::size_t count = 0;
    std::optional<Value> value;
};

} // namespace memdb
