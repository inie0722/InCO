#pragma once

#include <string>
#include <stddef.h>
#include <memory>
#include <boost/asio.hpp>

namespace mio
{
    namespace mq
    {
        namespace detail
        {
            class basic_socket
            {
            public:
                virtual void connect(const std::string &address) = 0;

                virtual size_t write(const void *data, size_t size) = 0;

                virtual size_t read(void *data, size_t size) = 0;

                virtual void close() = 0;

                virtual ~basic_socket() = default;
            };

            class basic_acceptor
            {
            public:
                virtual void bind(const std::string &address) = 0;
                virtual std::shared_ptr<detail::basic_socket> accept(boost::asio::io_context &io_context) = 0;
                virtual void close() = 0;
                virtual ~basic_acceptor() = default;
            };
        } // namespace detail
    }     // namespace mq
} // namespace mio