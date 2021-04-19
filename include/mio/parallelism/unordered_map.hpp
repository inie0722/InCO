#pragma once

#include "parallel_hashmap/phmap.h"

namespace mio
{
    namespace parallelism
    {
        template <class Key, class Value,
                  class Hash = phmap::priv::hash_default_hash<Key>,
                  class Eq = phmap::priv::hash_default_eq<Key>,
                  class Alloc = phmap::priv::Allocator<phmap::priv::Pair<const Key, Value>>, size_t N = 4, class Mutex = phmap::NullMutex>
        using unordered_map = phmap::parallel_node_hash_map<Key, Value, Hash, Eq, Alloc, N, Mutex>;
    }
} // namespace mio