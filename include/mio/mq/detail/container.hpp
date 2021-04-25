#pragma once

#include <boost/fiber/fiber.hpp>

#include <mio/parallelism/unordered_map.hpp>
#include <mio/parallelism/unordered_set.hpp>

namespace mio
{
    namespace mq
    {
        namespace detail
        {
            template <class Key, class Value,
                      class Hash = phmap::priv::hash_default_hash<Key>,
                      class Eq = phmap::priv::hash_default_eq<Key>,
                      class Alloc = phmap::priv::Allocator<phmap::priv::Pair<const Key, Value>>, size_t N = 4>
            using fiber_unordered_map = mio::parallelism::unordered_map<Key, Value, Hash, Eq, Alloc, N, boost::fibers::mutex>;

            template <class T,
                      class Hash = phmap::priv::hash_default_hash<T>,
                      class Eq = phmap::priv::hash_default_eq<T>,
                      class Alloc = phmap::priv::Allocator<T>, size_t N = 4>
            using fiber_unordered_set = phmap::parallel_node_hash_set<T, Hash, Eq, Alloc, N, boost::fibers::mutex>;
        }
    }
}