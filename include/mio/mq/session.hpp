#pragma once

#include <memory>
#include <variant>
#include <functional>

#include <boost/functional/hash.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/fiber/all.hpp>

#include <mio/mq/detail/round_robin.hpp>
#include <mio/mq/detail/basic_socket.hpp>
#include <mio/mq/detail/container.hpp>
#include <mio/mq/message.hpp>
#include <mio/mq/future.hpp>

namespace mio
{
    namespace mq
    {
        class manager;
        class session : public std::enable_shared_from_this<session>
        {
        public:
            using socket_t = detail::basic_socket;
            using acceptor_t = detail::basic_acceptor;
            using message_args_t = std::pair<std::weak_ptr<session>, std::shared_ptr<message>>;
            using message_handler_t = std::function<void(message_args_t args)>;
            using message_queue_t = boost::fibers::buffered_channel<message_args_t>;

            using close_handler_t = std::function<void(session &)>;
            using exception_handler_t = std::function<void(session &, const std::exception &e)>;

        private:
            friend class manager;
            using message_map_t = detail::fiber_unordered_map<std::string, std::pair<size_t, std::variant<message_handler_t, std::shared_ptr<message_queue_t>>>>;

            std::shared_ptr<socket_t> socket_;
            boost::fibers::buffered_channel<std::pair<std::shared_ptr<message>, std::shared_ptr<promise>>> write_pipe_;

            boost::fibers::fiber read_fiber_;
            boost::fibers::fiber write_fiber_;

            //请求消息的回复信息
            detail::fiber_unordered_map<boost::uuids::uuid, std::shared_ptr<promise>, boost::hash<boost::uuids::uuid>> uuid_promise_map_;

            std::atomic<size_t> level_ = 0;

            std::atomic<bool> is_open_ = true;

            close_handler_t close_handler_;
            exception_handler_t exception_handler_;
            message_map_t &message_handler_;

            void do_write()
            {
                try
                {
                    while (1)
                    {
                        std::pair<std::shared_ptr<message>, std::shared_ptr<promise>> msg;
                        if (boost::fibers::channel_op_status::closed == write_pipe_.pop(msg))
                        {
                            throw std::runtime_error("write_pipe is close");
                        }

                        //存储 uuid 用于告知响应
                        if (msg.second != nullptr)
                        {
                            uuid_promise_map_[msg.first->uuid] = msg.second;
                        }

                        uint64_t name_size = msg.first->name.size();
                        uint64_t data_size = msg.first->data.size();

                        socket_->write(&msg.first->type, sizeof(msg.first->type));
                        socket_->write(&msg.first->uuid, sizeof(msg.first->uuid));
                        socket_->write(&name_size, sizeof(name_size));
                        socket_->write(&data_size, sizeof(data_size));

                        if (name_size)
                            socket_->write(&msg.first->name[0], name_size);

                        if (data_size)
                            socket_->write(&msg.first->data[0], data_size);
                    }
                }
                catch (const std::exception &e)
                {
                    if (is_open_)
                        this->exception_handler_(*this, e);
                }
            }

            void do_read()
            {
                try
                {
                    while (1)
                    {
                        auto msg = std::make_shared<message>();
                        uint64_t name_size;
                        uint64_t data_size;

                        socket_->read(&msg->type, sizeof(msg->type));
                        socket_->read(&msg->uuid, sizeof(msg->uuid));
                        socket_->read(&name_size, sizeof(name_size));
                        socket_->read(&data_size, sizeof(data_size));

                        msg->name.resize(name_size);
                        msg->data.resize(data_size);

                        if (name_size)
                            socket_->read(&msg->name[0], name_size);

                        if (data_size)
                            socket_->read(&msg->data[0], data_size);

                        //响应消息
                        if (msg->type == message_type::RESPONSE)
                        {
                            uuid_promise_map_.at(msg->uuid)->set_value(msg);
                            uuid_promise_map_.erase(msg->uuid);
                        }
                        else
                        {
                            try
                            {
                                auto &handler = message_handler_.at(msg->name);

                                if (level_ >= handler.first)
                                {
                                    if (std::holds_alternative<message_handler_t>(handler.second))
                                    {
                                        boost::fibers::fiber(std::get<message_handler_t>(handler.second), message_args_t{this->shared_from_this(), msg}).detach();
                                    }
                                    else
                                    {
                                        std::get<std::shared_ptr<message_queue_t>>(handler.second)->push(message_args_t(this->shared_from_this(), std::move(msg)));
                                    }
                                }
                            }
                            catch (const std::exception &e)
                            {
                                if (is_open_)
                                    this->exception_handler_(*this, e);
                            }
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    if (is_open_)
                        this->exception_handler_(*this, e);
                }
            }

            void push(const std::shared_ptr<message> &msg_ptr, const std::shared_ptr<promise> &promise_ptr)
            {
                write_pipe_.push({msg_ptr, promise_ptr});
            }

        public:
            session(const std::shared_ptr<socket_t> &socket, message_map_t &message_handler)
                : socket_(socket), write_pipe_(1024), message_handler_(message_handler)
            {
                close_handler_ = [](session &s) {};
                exception_handler_ = [](session &s, const std::exception &e) {};

                read_fiber_ = boost::fibers::fiber(&session::do_read, this);
                write_fiber_ = boost::fibers::fiber(&session::do_write, this);
            }

            ~session()
            {
                std::cout << __func__ << std::endl;
                close();

                read_fiber_.join();
                write_fiber_.join();
            }

            void close()
            {
                bool exp = true;
                if (is_open_.compare_exchange_strong(exp, false))
                {
                    close_handler_(*this);

                    socket_->close();
                    write_pipe_.close();
                }
            }

            auto &on_colse()
            {
                return close_handler_;
            }

            auto &on_exception()
            {
                return exception_handler_;
            }

            void unicast(const std::shared_ptr<message> &msg)
            {
                msg->type = message_type::NOTICE;
                push(msg, nullptr);
            }

            future request(const std::shared_ptr<message> &msg)
            {
                msg->type = message_type::REQUEST;
                auto promise = std::make_shared<mio::mq::promise>(1);
                push(msg, promise);
                return promise->get_future();
            }

            void response(const std::shared_ptr<message> &msg)
            {
                msg->type = message_type::RESPONSE;
                push(msg, nullptr);
            }

            void unrequest(const boost::uuids::uuid &uuid)
            {
                uuid_promise_map_.erase(uuid);
            }

            void set_level(size_t level)
            {
                level_ = level;
            }
        };
    }
}