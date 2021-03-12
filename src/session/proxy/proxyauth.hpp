#ifndef _PROXYAUTH_H_
#define _PROXYAUTH_H_
#include <boost/asio.hpp>
#include "core/config.h"
using namespace boost;
using namespace std;
class ProxyDelegate:public std::enable_shared_from_this<ProxyDelegate>{
    protected:
        asio::ip::tcp::socket in_socket,out_socket;
        const Config& config;
        virtual void async_auth(std::function<void (const int32_t error)> resultHandler) =0;
    public:
        ProxyDelegate(asio::ip::tcp::socket in_socket,asio::ip::tcp::socket out_socket,const Config &config)
        :in_socket(std::move(in_socket)),
        out_socket(std::move(out_socket)),
        config(config)
        {
            
        };
        virtual void async_finished(std::function<void (asio::ip::tcp::socket in_socket,asio::ip::tcp::socket out_socket, const int32_t error)> completeHandler) final {
            this->async_auth([this,completeHandler](const int32_t error){
                completeHandler(std::move(in_socket),std::move(out_socket),error);
            });
        };
};
#endif