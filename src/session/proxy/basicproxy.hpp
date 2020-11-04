#ifndef _BASICPROXY_HPP_
#define _BASICPROXY_HPP_
#include "proxyauth.hpp"
using namespace std;
using namespace boost;
class BasicProxy 
:public ProxyDelegate{
  public:
        BasicProxy(asio::ip::tcp::socket in_socket,asio::ip::tcp::socket out_socket,const Config& config):
        ProxyDelegate(std::move(in_socket),std::move(out_socket),config)
        {};

        void async_auth(std::function<void (const system::error_code error)> resultHandler)
        {
                const string basic_http_request_info = "CONNECT " + config.remote_addr + ":" + to_string(config.remote_port) + " HTTP/1.1\r\n" /*+ "Proxy-Connection: Keep-Alive\r\n"*/ +
                                (!config.client_proxy.basic_auth.empty() ? (config.client_proxy.basic_auth + "\r\n") : "") +
                                "Content-Length: 0\r\n\r\n";
                auto data = make_shared<string>(basic_http_request_info);
                
                auto self = shared_from_this();
                asio::async_write(out_socket,asio::buffer(*data),[self,this,resultHandler](const system::error_code error, size_t) {
                        if (error) {
                                Log::log_with_endpoint(in_socket.remote_endpoint(),"send proxy connection request info failed:" + error.message(),Log::ERROR);
                                resultHandler(error);
                                return;
                        }
                        auto buf = make_shared<asio::streambuf>();
                        auto self = shared_from_this();
                        asio::async_read_until(out_socket,*buf,"\r\n",[self,this,buf,resultHandler](system::error_code ec,
                        std::size_t ){
                                if(ec){
                                        Log::log_with_endpoint(in_socket.remote_endpoint(),"read proxy connection response info failed:" + ec.message(),Log::ERROR);
                                        resultHandler(ec);
                                        return;
                                }
                                std::istream is_res(buf.get());
                                string http_version;
                                unsigned int status_code;
                                string status_message;
                                is_res >> http_version;
                                is_res >> status_code;
                                is_res >> status_message;
                                if(status_code == 200){
                                        system::error_code no_error_code;
                                        resultHandler(no_error_code);
                                } else {
                                        auto transferred_code=status_code > 200? to_string(status_code):"502";
                                        auto data_copy = make_shared<string>(http_version + " " +  transferred_code + " " + status_message + "\r\nConnection: Close\r\n");
                                        auto self = shared_from_this();
                                        asio::async_write(in_socket, asio::buffer(*data_copy), [self,this,resultHandler,transferred_code](const system::error_code , size_t) {

                                                Log::log_with_endpoint(in_socket.remote_endpoint(),"proxy response failed:" + transferred_code,Log::ERROR);
                                                resultHandler(asio::error::connection_refused);
                                        });
                                }
                        });
                });
                
        }
        virtual ~BasicProxy(){}

};
#endif