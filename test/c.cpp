#include <iostream>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "mio/mq/manager.hpp"
#include <mio/network/tcp.hpp>

#include "mio/mq/manager.hpp"

using namespace mio::mq;

struct protocol
{
    using socket_t = mio::network::tcp::socket;
    using acceptor_t = mio::network::tcp::acceptor;
};

int main(void)
{
    std::list<std::shared_ptr<session>> list;
    mio::mq::manager m(1);

    m.registered("test") = {0, [&](std::pair<std::weak_ptr<mio::mq::session>, std::shared_ptr<message>> msg){
        std::cout << &msg.second->name[0] << std::endl;
    }};

    m.on_connect() = [&](const std::string &address, const std::shared_ptr<mio::mq::session> &session){
        list.push_back(session);
        std::cout << address << std::endl;
    };

    m.on_exception() = [&](const std::exception &e) {
        std::cout << e.what() << std::endl;
    };

    m.connect<protocol>("ipv4:127.0.0.1:9999");
    
    while(1)
    {
        sleep(1);
    }
        
    return 0;
}