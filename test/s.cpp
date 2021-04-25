#include <iostream>
#include "mio/network/tcp.hpp"

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
    m.on_acceptor() = [&](const std::string &address, const std::shared_ptr<session> &s) {
        list.push_back(s);
        std::cout << address << std::endl;

        m.add_group("test", s);
    };

    m.on_exception() = [&](const std::exception & e) {
        std::cout << e.what() << std::endl;
    };

    m.bind<protocol>("ipv4:127.0.0.1:9999");
    // sleep(9999);
    // m.unbind("ipv4:127.0.0.1:9999");
/*
    m.registered("call", 0, [&](std::pair<std::weak_ptr<manager::session>, std::shared_ptr<message>> msg){
        
        auto msg_res = std::make_shared<mio::mq::message>();
        msg_res->name = "call";
        msg_res->data= msg.second->data;
        msg_res->uuid = msg.second->uuid;
        msg_res->type = mio::mq::message_type::RESPONSE;
        msg.first.lock()->response(msg_res);
        //msg.first.lock()->close();
    });

    m.bind<protocol>("ipv4:127.0.0.1:9999");
*/
    mio::mq::message msg;
    msg.type = mio::mq::message_type::NOTICE;

    msg.name = "test";

    while (1)    
    {
        auto s = m.load_balanc("test");
        if(s)
            s->unicast(std::make_shared<mio::mq::message>(msg));
        sleep(1);
    }
    
    return 0;
}