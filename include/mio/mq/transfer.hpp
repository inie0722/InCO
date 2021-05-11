#pragma once

#include <string>
#include <memory>

#include <boost/asio.hpp>

#include "mio/mq/detail/round_robin.hpp"
#include "mio/mq/detail/basic_socket.hpp"

namespace mio
{
    namespace mq
    {
        template <typename Protocol>
        class transfer
        {
        public:
            using socket_t = typename Protocol::socket_t;
            using acceptor_t = typename Protocol::acceptor_t;

            class socket : public detail::basic_socket, public socket_t
            {
            public:
                socket(boost::asio::io_context &io_context) : socket_t(io_context)
                {
                }

                virtual ~socket() = default;

                virtual void connect(const std::string &address)
                {
                    socket_t::async_connect(address, boost::fibers::asio::yield);
                }

                virtual size_t write(const void *data, size_t size)
                {
                    return socket_t::async_write(data, size, boost::fibers::asio::yield);
                }

                virtual size_t read(void *data, size_t size)
                {
                    return socket_t::async_read(data, size, boost::fibers::asio::yield);
                }

                virtual void close()
                {
                    socket_t::close();
                }
            };

            class acceptor : public detail::basic_acceptor, public acceptor_t
            {
            public:
                acceptor(boost::asio::io_context &io_context) : acceptor_t(io_context)
                {
                }

                virtual ~acceptor() = default;

                virtual void bind(const std::string &address)
                {
                    acceptor_t::bind(address);
                }

                virtual std::shared_ptr<detail::basic_socket> accept(boost::asio::io_context &io_context)
                {
                    std::shared_ptr<detail::basic_socket> ptr = std::make_shared<socket>(io_context);

                    acceptor_t::async_accept(static_cast<socket_t &>(static_cast<socket &>(*ptr)), boost::fibers::asio::yield);

                    return ptr;
                }

                virtual void close()
                {
                    acceptor_t::close();
                }
            };
        };
    } // namespace mq
} // namespace mio