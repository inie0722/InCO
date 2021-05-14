#pragma once

#include <stdint.h>
#include <memory>

#include <boost/fiber/buffered_channel.hpp>

#include "mio/mq/message.hpp"

namespace mio
{
    namespace mq
    {
        class session;
        class future
        {
        public:
            using message_args_t = std::pair<std::weak_ptr<session>, std::shared_ptr<message>>;

        private:
            friend class promise;
            size_t size_;

            std::shared_ptr<boost::fibers::buffered_channel<message_args_t>> pipe_;

            future(size_t size, const decltype(pipe_) &pipe) : pipe_(pipe)
            {
                size_ = size;
            }

        public:
            message_args_t get()
            {
                message_args_t msg;

                pipe_->pop(msg);
                return msg;
            }

            template <typename Rep, typename Period>
            message_args_t get(const std::chrono::duration<Rep, Period> &timeout_duration)
            {
                message_args_t msg;
                pipe_->pop_wait_for(msg, std::chrono::nanoseconds(timeout_duration));

                return msg;
            }

            size_t size()
            {
                return size_;
            }
        };

        class promise
        {
        public:
            using message_args_t = future::message_args_t;

        private:
            size_t size_;
            std::shared_ptr<boost::fibers::buffered_channel<message_args_t>> pipe_;

        public:
            promise(size_t size)
            {
                size_ = size;
                pipe_ = std::make_shared<boost::fibers::buffered_channel<message_args_t>>(64);
            }

            future get_future()
            {
                return future(size_, pipe_);
            }

            void set_value(const message_args_t &msg)
            {
                pipe_->push(msg);
            }
        };
    } // namespace mq
} // namespace mio