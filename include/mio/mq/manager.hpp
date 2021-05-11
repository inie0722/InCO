#pragma once

#include <memory>
#include <utility>
#include <string>
#include <variant>
#include <vector>
#include <atomic>
#include <thread>
#include <list>
#include <functional>

#include <boost/functional/hash.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/fiber/all.hpp>

#include <mio/mq/detail/round_robin.hpp>
#include <mio/mq/detail/basic_socket.hpp>
#include <mio/mq/detail/container.hpp>
#include <mio/mq/message.hpp>
#include <mio/mq/future.hpp>
#include <mio/mq/transfer.hpp>
#include <mio/mq/session.hpp>

namespace mio
{
    namespace mq
    {
        class manager
        {
        public:
            using socket_t = detail::basic_socket;
            using acceptor_t = detail::basic_acceptor;

            using message_args_t = session::message_args_t;
            using message_handler_t = session::message_handler_t;
            using message_queue_t = session::message_queue_t;

            //当接受客户端 或 连接上服务器将 触发的回调
            using acceptor_handler_t = std::function<void(const std::string &address, const std::shared_ptr<session> &session_ptr)>;
            using exception_handler_t = std::function<void(const std::exception &e)>;

        private:
            using message_map_t = session::message_map_t;
            using session_list_t = std::list<std::weak_ptr<session>>;
            struct group
            {
                boost::fibers::mutex mutex;
                session_list_t list;
                session_list_t::iterator load_balanc_it;
            };

            using group_map_t = detail::fiber_unordered_map<std::string, group>;
            using acceptor_map_t = detail::fiber_unordered_map<std::string, std::pair<std::unique_ptr<boost::fibers::fiber>, std::unique_ptr<acceptor_t>>>;

            acceptor_handler_t acceptor_handler_;
            exception_handler_t exception_handler_;

            //收到消息的处理程序
            message_map_t message_handler_;

            //组列表
            group_map_t group_map_;

            //管理所有bind地址
            boost::fibers::mutex acceptor_map_mutex_;
            acceptor_map_t acceptor_map_;

            //asio调度器分配
            std::atomic<size_t> io_context_count_;
            std::vector<std::shared_ptr<boost::asio::io_context>> io_context_;
            std::vector<std::thread> thread_;
            size_t thread_size_;

            auto get_io_context()
            {
                return io_context_[io_context_count_.fetch_add(1) % thread_size_];
            }

        public:
            manager(size_t thread_size)
            {
                thread_size_ = thread_size;
                for (size_t i = 0; i < thread_size; i++)
                {
                    io_context_.push_back(std::make_shared<boost::asio::io_context>());
                    thread_.push_back(std::thread([&, i]() {
                        boost::fibers::use_scheduling_algorithm<boost::fibers::asio::round_robin>(std::shared_ptr(io_context_[i]));

                        while (1)
                        {
                            try
                            {
                                io_context_[i]->run();
                                break;
                            }
                            catch (const std::exception &e)
                            {
                                exception_handler_(e);
                            }
                        }
                    }));
                }
            }

            ~manager()
            {
                //关闭所有acceptor
                acceptor_map_mutex_.lock();
                for (auto &acceptor : acceptor_map_)
                {
                    acceptor.second.second->close();
                    acceptor.second.first->join();
                }
                acceptor_map_mutex_.unlock();

                for (size_t i = 0; i < thread_size_; i++)
                {
                    io_context_[i]->stop();
                    thread_[i].join();
                }
            }

            auto &on_acceptor()
            {
                return acceptor_handler_;
            }

            auto &on_exception()
            {
                return exception_handler_;
            }

            auto &registered(const std::string &name)
            {
                return message_handler_[name];
            }

            void logout(const std::string &name)
            {
                message_handler_.erase(name);
            }

            template <typename Protocol>
            void bind(const std::string &address)
            {
                //绑定地址
                acceptor_map_[address].second = std::make_unique<typename transfer<Protocol>::acceptor>(*get_io_context());
                acceptor_map_[address].second->bind(address);

                auto io_context = get_io_context();
                io_context->post([&, address]() {
                    acceptor_map_[address].first = std::make_unique<boost::fibers::fiber>([&, address]() {
                        try
                        {
                            while (1)
                            {
                                auto socket_io_context = get_io_context();
                                auto socket = acceptor_map_[address].second->accept(*socket_io_context);

                                socket_io_context->post([&, address, socket]() {
                                    auto session_ptr = std::make_shared<session>(socket, this->message_handler_);
                                    acceptor_handler_(address, session_ptr);
                                });
                            }
                        }
                        catch (const std::exception &e)
                        {
                            exception_handler_(e);
                        }
                    });
                });
            }

            void unbind(const std::string &address)
            {
                acceptor_map_[address].second->close();
                acceptor_map_[address].first->join();
                acceptor_map_.erase(address);
            }

            template <typename Protocol>
            std::shared_ptr<session> connect(const std::string &address)
            {
                auto socket = std::make_unique<typename transfer<Protocol>::socket>(*get_io_context());
                socket->connect(address);
                return std::make_shared<session>(std::move(socket), this->message_handler_);
            }

            session_list_t::iterator add_group(const std::string &group_name, const std::weak_ptr<session> &session_ptr)
            {
                std::lock_guard(group_map_[group_name].mutex);
                group_map_[group_name].list.push_front(session_ptr);
                if (group_map_[group_name].list.size() == 1)
                {
                    group_map_[group_name].load_balanc_it = group_map_[group_name].list.begin();
                }

                return group_map_[group_name].list.begin();
            }

            void remove_group(const std::string &group_name, session_list_t::iterator it)
            {
                std::lock_guard(group_map_[group_name].mutex);
                if (group_map_[group_name].load_balanc_it == it)
                {
                    ++group_map_[group_name].load_balanc_it;
                }
                group_map_[group_name].list.erase(it);
            }

            void broadcast(const std::string &group_name, const std::shared_ptr<message> &msg)
            {
                std::lock_guard(group_map_[group_name].mutex);
                for (auto &it : group_map_[group_name].list)
                {
                    it.lock()->unicast(msg);
                }
            }

            future request(const std::string &group_name, const std::shared_ptr<message> &msg)
            {
                std::lock_guard(group_map_[group_name].mutex);
                auto promise = std::make_shared<mio::mq::promise>(group_map_[group_name].list.size());
                for (auto &it : group_map_[group_name].list)
                {
                    it.lock()->push(msg, promise);
                }

                return promise->get_future();
            }

            std::shared_ptr<session> load_balanc(const std::string &group_name)
            {
                std::lock_guard(group_map_[group_name].mutex);
                if (group_map_[group_name].list.empty())
                    return nullptr;

                auto &it = group_map_[group_name].load_balanc_it;

                if (it == group_map_[group_name].list.end())
                {
                    it = group_map_[group_name].list.begin();
                }
                return it++->lock();
            }
        };
    } // namespace mq
} // namespace mio
