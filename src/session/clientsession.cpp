/*
 * This file is part of the trojan project.
 * Trojan is an unidentifiable mechanism that helps you bypass GFW.
 * Copyright (C) 2017-2020  The Trojan Authors.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "clientsession.h"
#include "proto/trojanrequest.h"
#include "proto/udppacket.h"
#include "ssl/sslsession.h"
#include <iostream>
using namespace std;
using namespace boost::asio::ip;
using namespace boost::asio::ssl;



ClientSession::ClientSession(const Config &config, boost::asio::io_context &io_context, context &ssl_context) :
    Session(config, io_context),
    status(HANDSHAKE),
    first_packet_recv(false),
    // proxyDelegate(nullptr),
    in_socket(io_context),//from local app send to this client
    out_socket_ssl(io_context,ssl_context) //to remote_address from this client
    {
    }


tcp::socket& ClientSession::accept_socket() {
    return in_socket;
}

void ClientSession::init_ssl(){

    auto ssl = out_socket_ssl.native_handle();
    if (!config.ssl.sni.empty()) {
        SSL_set_tlsext_host_name(ssl, config.ssl.sni.c_str());
    }
    if (config.ssl.reuse_session) {
        SSL_SESSION *session = SSLSession::get_session();
        if (session) {
            SSL_set_session(ssl, session);
        }
    }
}

void ClientSession::start() {
    boost::system::error_code ec;
    start_time = time(nullptr);
    in_endpoint = in_socket.remote_endpoint(ec);
    if (ec) {
        destroy();
        return;
    }
    init_ssl();
    in_async_read();
}

void ClientSession::in_async_read() {
    auto self = shared_from_this();
    in_socket.async_read_some(boost::asio::buffer(in_read_buf, MAX_LENGTH), [this, self](const boost::system::error_code error, size_t length) {
        if (error == boost::asio::error::operation_aborted) {
            return;
        }
        if (error) {
            destroy();
            return;
        }
        in_recv(string((const char*)in_read_buf, length));
    });
}

void ClientSession::in_async_write(const string &data,function<void(void)> after_write) {
    auto self = shared_from_this();
    auto data_copy = make_shared<string>(data);
    boost::asio::async_write(in_socket, boost::asio::buffer(*data_copy), [this, self, data_copy,after_write](const boost::system::error_code error, size_t) {
        if (error) {
            destroy();
            return;
        }
        after_write();
    });
}

void ClientSession::out_async_read() {
    auto self = shared_from_this();
    out_socket_ssl.async_read_some(boost::asio::buffer(out_read_buf, MAX_LENGTH), [this, self](const boost::system::error_code error, size_t length) {
        if (error) {
            destroy();
            return;
        }
        out_recv(string((const char*)out_read_buf, length));
    });
}

void ClientSession::out_async_write(const string &data) {
    auto self = shared_from_this();
    auto data_copy = make_shared<string>(data);
    boost::asio::async_write(out_socket_ssl, boost::asio::buffer(*data_copy), [this, self, data_copy](const boost::system::error_code error, size_t) {
        if (error) {
            destroy();
            return;
        }
        out_sent();
    });
}

void ClientSession::udp_async_read() {
    auto self = shared_from_this();
    udp_socket.async_receive_from(boost::asio::buffer(udp_read_buf, MAX_LENGTH), udp_recv_endpoint, [this, self](const boost::system::error_code error, size_t length) {
        if (error == boost::asio::error::operation_aborted) {
            return;
        }
        if (error) {
            destroy();
            return;
        }
        udp_recv(string((const char*)udp_read_buf, length), udp_recv_endpoint);
    });
}

void ClientSession::udp_async_write(const string &data, const udp::endpoint &endpoint) {
    auto self = shared_from_this();
    auto data_copy = make_shared<string>(data);
    udp_socket.async_send_to(boost::asio::buffer(*data_copy), endpoint, [this, self, data_copy](const boost::system::error_code error, size_t) {
        if (error) {
            destroy();
            return;
        }
        udp_sent();
    });
}

inline void split(string str,string sep,function<bool(string)> fn){
    char* cstr = const_cast<char*>(str.c_str());
    char* current;
    current=strtok(cstr,sep.c_str());
    while(current!=nullptr){
        if(!fn(current)){
            break;
        }
        current=strtok(nullptr,sep.c_str());
    }
}

void ClientSession::precheck_iphost(string& hostStr,int32_t& port){
        //lazy check if host is ipv4 or domain
    bool isIPV4 = true;
    int8_t segIndex = 0;
    char segValue[] = {0,0,0,0};
    for(string::iterator it=hostStr.begin();it!=hostStr.end();++it){
        if(segIndex > 3){
            isIPV4 = false;
            break;
        }
        if(*it == '.'){
            segIndex++;
        } else if (isdigit(*it)){
            segValue[segIndex] = segValue[segIndex] * 10 + (((int)*it) - '0');
            if(segValue[segIndex] <= 0){
                isIPV4 = false;
                break;
            }
        } else {
            isIPV4 = false;
            break;
        }
    }
    if(segIndex != 3){
            isIPV4 = false;
    }
    out_write_buf = config.password.cbegin()->first + "\r\n\x01" + (isIPV4?'\x01':'\x03') ;
    if(isIPV4){
        out_write_buf.append({segValue[0],segValue[1],segValue[2],segValue[3]});
    } else {
        out_write_buf.append({(char) hostStr.length()}).append(hostStr);
    }
    out_write_buf.append({(char)((port&0xff00) >> 8),(char)(port&0x00ff),'\r','\n'});
    status= REQUEST;
    is_udp = false;
    Log::log_with_endpoint(in_endpoint, "request connect to " + hostStr + ":" + to_string(port) , Log::INFO);
}


void ClientSession::in_recv(const string &data) {
    switch (status) {
        case HANDSHAKE: {
            if (data.length() >= 2 && data[0] == 5 && data.length() == (unsigned int)(unsigned char)data[1] + 2) {
                //socks version 5
                bool has_method = false;
                for (int i = 2; i < data[1] + 2; ++i) {
                    if (data[i] == 0) {
                        has_method = true;
                        break;
                    }
                }
                // https://tools.ietf.org/html/rfc1928
                //   o  X'00' NO AUTHENTICATION REQUIRED
                //   o  X'01' GSSAPI
                //   o  X'02' USERNAME/PASSWORD
                //   o  X'03' to X'7F' IANA ASSIGNED
                //   o  X'80' to X'FE' RESERVED FOR PRIVATE METHODS
                //   o  X'FF' NO ACCEPTABLE METHODS
                if (!has_method) {
                    Log::log_with_endpoint(in_endpoint, "unsupported auth method", Log::ERROR);
                    in_async_write(string("\x05\xff", 2), [this](){
                        in_sent();
                    });
                    status = INVALID;
                    return;
                }
                proxyType = SOCKS5;
                in_async_write(string("\x05\x00", 2), [this](){
                        in_sent();
                });
                break;
            } else if(data.length() > 25){
                // http proxy
                size_t lfIndex = data.find(' ');
                if(lfIndex != string::npos && lfIndex < 8){
                    string method = data.substr(0,lfIndex);
                    int32_t port;
                    string hostStr;
                    if(method == "CONNECT"){
                        size_t cmIndex,secondLfIndex;
                        if((cmIndex = data.find(':'))!= string::npos && (secondLfIndex=data.find(' ',cmIndex))!= string::npos){
                            hostStr = data.substr(lfIndex+1,cmIndex - lfIndex -1);
                            auto portStr = data.substr(cmIndex + 1, secondLfIndex - cmIndex - 1);
                            
                            try{
                                port = stoi(portStr);
                            }catch(invalid_argument e){
                                Log::log_with_endpoint(in_endpoint, "unknown protocol", Log::ERROR);
                                destroy();
                                return;
                            }
                            proxyType = HTTPS;
                            precheck_iphost(hostStr,port);
                            in_sent();
                        }
                    } else if(method == "GET" ||
                     method == "POST" ||
                     method == "PUT"||
                     method == "DELETE" ||
                     method == "PATCH" ||
                     method == "HEAD" ||
                     method == "OPTIONS") {
                        split(data,"\r\n",[this,data,&port,&hostStr](string line){
                            int32_t firstcommaIndex;
                            if((firstcommaIndex=line.find("Host: ")) == 0){
                                int32_t lastcommaIndex;
                                if((lastcommaIndex=line.rfind(":")) > 4){
                                    auto portStr = line.substr(lastcommaIndex+1);
                                    bool isValidPort = true;
                                    if(portStr.length() < 6){
                                        for(string::iterator it=portStr.begin();it!=portStr.end();++it){
                                            if(!isdigit(*it)){
                                                isValidPort = false;
                                                break;
                                            }
                                        }
                                    } else {
                                        isValidPort = false;
                                    }
                                    
                                    if(isValidPort){
                                        port = stoi(portStr);
                                        hostStr = line.substr(6,lastcommaIndex-6);
                                        proxyType = HTTP;
                                        precheck_iphost(hostStr,port);
                                    } else {
                                        string badReq = "HTTP/1.1 400 Bad Request\r\n\r\n";
                                        in_async_write(badReq, [this](){
                                                destroy();
                                        });
                                    }
                                } else {
                                    port = 80;
                                    hostStr = line.substr(6);
                                    proxyType = HTTP;
                                    precheck_iphost(hostStr,port);
                                }
                                return false;
                            }
                            return true;
                        });
                        if(proxyType == HTTP){
                            auto slashIndex = data.substr(method.length() + 8).find("/");
                            out_write_buf += method + " " +data.substr(slashIndex+method.length() + 8);
                            in_sent();
                        }
                     }
                } else {
                    Log::log_with_endpoint(in_endpoint, "unknown protocol", Log::ERROR);
                    destroy();
                    return;
                }
                break;
            } else {
                Log::log_with_endpoint(in_endpoint, "unknown protocol", Log::ERROR);
                destroy();
                return;
            }
            
        }
        case REQUEST: {
            if (data.length() < 7 || data[0] != 5 || data[2] != 0) {
                Log::log_with_endpoint(in_endpoint, "bad request", Log::ERROR);
                destroy();
                return;
            }
            out_write_buf = config.password.cbegin()->first + "\r\n" + data[1] + data.substr(3) + "\r\n";
            TrojanRequest req;
            if (req.parse(out_write_buf) == -1) {
                Log::log_with_endpoint(in_endpoint, "unsupported command", Log::ERROR);
                in_async_write(string("\x05\x07\x00\x01\x00\x00\x00\x00\x00\x00", 10), [this](){
                        in_sent();
                });
                status = INVALID;
                return;
            }
            is_udp = req.command == TrojanRequest::UDP_ASSOCIATE;
            if (is_udp) {
                udp::endpoint bindpoint(in_socket.local_endpoint().address(), 0);
                boost::system::error_code ec;
                udp_socket.open(bindpoint.protocol(), ec);
                if (ec) {
                    destroy();
                    return;
                }
                udp_socket.bind(bindpoint);
                Log::log_with_endpoint(in_endpoint, "requested UDP associate to " + req.address.address + ':' + to_string(req.address.port) + ", open UDP socket " + udp_socket.local_endpoint().address().to_string() + ':' + to_string(udp_socket.local_endpoint().port()) + " for relay", Log::INFO);
                in_async_write(string("\x05\x00\x00", 3) + SOCKS5Address::generate(udp_socket.local_endpoint()), [this](){
                    in_sent();
                });
            } else {
                Log::log_with_endpoint(in_endpoint, "requested connection to " + req.address.address + ':' + to_string(req.address.port), Log::INFO);
                in_async_write(string("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10), [this](){
                    in_sent();
                });
            }
            break;
        }
        case CONNECT: {
            sent_len += data.length();
            first_packet_recv = true;
            out_write_buf += data;
            if(proxyType == HTTPS){
                after_ssl_handshake();
            }
            break;
        }
        case FORWARD: {
            sent_len += data.length();
            out_async_write(data);
            break;
        }
        case UDP_FORWARD: {
            Log::log_with_endpoint(in_endpoint, "unexpected data from TCP port", Log::ERROR);
            destroy();
            break;
        }
        default: break;
    }
}

void ClientSession::in_sent() {
    switch (status) {
        case HANDSHAKE: {
            status = REQUEST;
            in_async_read();
            break;
        }
        case REQUEST: {
            if(proxyType == SOCKS5){
                status = CONNECT;
                in_async_read();
                if (is_udp) {
                    udp_async_read();
                }
            }

            auto self = shared_from_this();
            if(!config.client_proxy.host.empty()){
                Log::log_with_endpoint(in_endpoint,"using client proxy " + config.client_proxy.host + ":" + to_string(config.client_proxy.port) + ",method "+ config.client_proxy.method +" for connection.",Log::INFO);
                resolver.async_resolve(config.client_proxy.host,to_string(config.client_proxy.port),[this, self](const boost::system::error_code error, tcp::resolver::results_type results){
                    if (error || results.empty())
                    {
                        Log::log_with_endpoint(in_endpoint, "cannot resolve client proxy hostname " + config.client_proxy.host + ": " + error.message(), Log::ERROR);
                        destroy();
                        return;
                    }

                    auto iterator = results.begin();
                    Log::log_with_endpoint(in_endpoint, config.client_proxy.host + " is resolved to " + iterator->endpoint().address().to_string(), Log::ALL);
                    boost::system::error_code ec;
                    auto endpoint =iterator->endpoint();
                    out_socket_ssl.next_layer().open(endpoint.protocol(),ec);
                    
                    if(ec){
                        Log::log_with_endpoint(in_endpoint, "cannot open connection to client proxy " + config.client_proxy.host + ": " + ec.message(), Log::ERROR);
                        destroy();
                        return;
                    }
                    out_socket_ssl.next_layer().async_connect(*iterator, [this, self](const boost::system::error_code error) {
                        if(error){
                            Log::log_with_endpoint(in_endpoint, "cannot connect to client proxy " + config.client_proxy.host + ": " + error.message(), Log::ERROR);
                            destroy();
                            return;
                        }
                        if(config.client_proxy.method == "BASIC"){
                            auto self = shared_from_this();
                            make_shared<BasicProxy>(std::move(in_socket),std::move(out_socket_ssl.next_layer()),config)->async_finished([self,this](asio::ip::tcp::socket ins_,asio::ip::tcp::socket outs_,const int32_t error){
                            in_socket = std::move(ins_);
                            out_socket_ssl.next_layer()=std::move(outs_);
                            if(error != 0){
                                if(proxyType != SOCKS5){
                                    if(error < 0) {
                                        in_async_write(string("HTTP/1.1 502 Bad Gateway\r\n\r\n", 28), [this](){
                                            destroy();
                                        });
                                    } else {
                                        string bad_response = "HTTP/1.1 " + to_string(error) + " Error Request\r\n\r\n";
                                        in_async_write(string(bad_response, bad_response.length()), [this](){
                                            destroy();
                                        });
                                    }
                                }
                                return;
                            } else {
                                ssl_handshake();
                            }
                        });
                        } else {
                            Log::log_with_endpoint(in_endpoint, "client proxy auth method not supported:" + config.client_proxy.method, Log::ERROR);
                            destroy();
                            return;
                        }
                    });
                });
            } else {
                resolver.async_resolve(config.remote_addr, to_string(config.remote_port), [this, self](const boost::system::error_code error, tcp::resolver::results_type results) {
                    if (error)
                    {
                        Log::log_with_endpoint(in_endpoint, "cannot resolve remote server hostname " + config.remote_addr + ": " + error.message(), Log::ERROR);
                        destroy();
                        return;
                    }
                    auto iterator = results.begin();
                    Log::log_with_endpoint(in_endpoint, config.remote_addr + " is resolved to " + iterator->endpoint().address().to_string(), Log::ALL);
                    boost::system::error_code ec;
                    out_socket_ssl.next_layer().open(iterator->endpoint().protocol(), ec);
                    if (ec)
                    {
                        destroy();
                        return;
                    }
                    if (config.tcp.no_delay)
                    {
                        out_socket_ssl.next_layer().set_option(tcp::no_delay(true));
                    }
                    if (config.tcp.keep_alive)
                    {
                        out_socket_ssl.next_layer().set_option(boost::asio::socket_base::keep_alive(true));
                    }
#ifdef TCP_FASTOPEN_CONNECT
                    if (config.tcp.fast_open)
                    {
                        using fastopen_connect = boost::asio::detail::socket_option::boolean<IPPROTO_TCP, TCP_FASTOPEN_CONNECT>;
                        boost::system::error_code ec;
                        out_socket_ssl.next_layer().set_option(fastopen_connect(true), ec);
                    }
#endif // TCP_FASTOPEN_CONNECT
                    out_socket_ssl.next_layer().async_connect(*iterator, [this, self](const boost::system::error_code error) {
                        if (error)
                        {
                            if(proxyType != SOCKS5){
                                in_async_write(string("HTTP/1.1 502 Bad Gateway\r\n\r\n", 28), [this](){
                                    destroy();
                                });

                            } else {
                                destroy();
                            }
                            Log::log_with_endpoint(in_endpoint, "cannot establish connection to remote server " + config.remote_addr + ':' + to_string(config.remote_port) + ": " + error.message(), Log::ERROR);
                            return;
                        }
                        ssl_handshake();
                    });
                });
            }
            
            break;
        }
        case FORWARD: {
            out_async_read();
            break;
        }
        case INVALID: {
            destroy();
            break;
        }
        default: break;
    }
};

inline  void ClientSession::after_ssl_handshake(){
        if (config.ssl.reuse_session)
            {
                auto ssl = out_socket_ssl.native_handle();
                if (!SSL_session_reused(ssl))
                {
                    Log::log_with_endpoint(in_endpoint, "SSL session not reused");
                }
                else
                {
                    Log::log_with_endpoint(in_endpoint, "SSL session reused");
                }
            }
            boost::system::error_code ec;
            if (is_udp)
            {
                if (!first_packet_recv)
                {
                    udp_socket.cancel(ec);
                }
                status = UDP_FORWARD;
            }
            else
            {
                if (!first_packet_recv)
                {
                    in_socket.cancel(ec);
                }
                status = FORWARD;
            }

            out_async_read();
            out_async_write(out_write_buf);
};


void ClientSession::ssl_handshake(){
    auto self = shared_from_this();
    out_socket_ssl.async_handshake(stream_base::client, [this, self](const boost::system::error_code error) {
        if (error)
        {
            Log::log_with_endpoint(in_endpoint, "SSL handshake failed with " + config.remote_addr + ':' + to_string(config.remote_port) + ": " + error.message(), Log::ERROR);
            destroy();
            return;
        }
        Log::log_with_endpoint(in_endpoint, "tunnel established");
        if(proxyType == HTTPS){
            in_async_write(string("HTTP/1.1 200 Connection established\r\n\r\n", 39), [this](){
                status = CONNECT;
                in_async_read();
            });
        }else {
            after_ssl_handshake();
        }
    });
}

void ClientSession::out_recv(const string &data) {
    if (status == FORWARD) {
        recv_len += data.length();
        in_async_write(data, [this](){
            in_sent();
        });
    } else if (status == UDP_FORWARD) {
        udp_data_buf += data;
        udp_sent();
    }
}

void ClientSession::out_sent() {
    if (status == FORWARD) {
        in_async_read();
    } else if (status == UDP_FORWARD) {
        udp_async_read();
    }
}

void ClientSession::udp_recv(const string &data, const udp::endpoint&) {
    if (data.length() == 0) {
        return;
    }
    if (data.length() < 3 || data[0] || data[1] || data[2]) {
        Log::log_with_endpoint(in_endpoint, "bad UDP packet", Log::ERROR);
        destroy();
        return;
    }
    SOCKS5Address address;
    size_t address_len;
    bool is_addr_valid = address.parse(data.substr(3), address_len);
    if (!is_addr_valid) {
        Log::log_with_endpoint(in_endpoint, "bad UDP packet", Log::ERROR);
        destroy();
        return;
    }
    size_t length = data.length() - 3 - address_len;
    Log::log_with_endpoint(in_endpoint, "sent a UDP packet of length " + to_string(length) + " bytes to " + address.address + ':' + to_string(address.port));
    string packet = data.substr(3, address_len) + char(uint8_t(length >> 8)) + char(uint8_t(length & 0xFF)) + "\r\n" + data.substr(address_len + 3);
    sent_len += length;
    if (status == CONNECT) {
        first_packet_recv = true;
        out_write_buf += packet;
    } else if (status == UDP_FORWARD) {
        out_async_write(packet);
    }
}

void ClientSession::udp_sent() {
    if (status == UDP_FORWARD) {
        UDPPacket packet;
        size_t packet_len;
        bool is_packet_valid = packet.parse(udp_data_buf, packet_len);
        if (!is_packet_valid) {
            if (udp_data_buf.length() > MAX_LENGTH) {
                Log::log_with_endpoint(in_endpoint, "UDP packet too long", Log::ERROR);
                destroy();
                return;
            }
            out_async_read();
            return;
        }
        Log::log_with_endpoint(in_endpoint, "received a UDP packet of length " + to_string(packet.length) + " bytes from " + packet.address.address + ':' + to_string(packet.address.port));
        SOCKS5Address address;
        size_t address_len;
        bool is_addr_valid = address.parse(udp_data_buf, address_len);
        if (!is_addr_valid) {
            Log::log_with_endpoint(in_endpoint, "udp_sent: invalid UDP packet address", Log::ERROR);
            destroy();
            return;
        }
        string reply = string("\x00\x00\x00", 3) + udp_data_buf.substr(0, address_len) + packet.payload;
        udp_data_buf = udp_data_buf.substr(packet_len);
        recv_len += packet.length;
        udp_async_write(reply, udp_recv_endpoint);
    }
}

void ClientSession::destroy() {
    if (status == DESTROY) {
        return;
    }
    status = DESTROY;
    Log::log_with_endpoint(in_endpoint, "disconnected, " + to_string(recv_len) + " bytes received, " + to_string(sent_len) + " bytes sent, lasted for " + to_string(time(nullptr) - start_time) + " seconds", Log::INFO);
    boost::system::error_code ec;
    resolver.cancel();
    if (in_socket.is_open()) {
        in_socket.cancel(ec);
        in_socket.shutdown(tcp::socket::shutdown_both, ec);
        in_socket.close(ec);
    }

    if (udp_socket.is_open()) {
        udp_socket.cancel(ec);
        udp_socket.close(ec);
    }
    if (out_socket_ssl.next_layer().is_open())
    {
        auto self = shared_from_this();
        auto ssl_shutdown_cb = [this, self](const boost::system::error_code error) {
            if (error == boost::asio::error::operation_aborted)
            {
                return;
            }
            boost::system::error_code ec;
            ssl_shutdown_timer.cancel();
            out_socket_ssl.next_layer().cancel(ec);
            out_socket_ssl.next_layer().shutdown(tcp::socket::shutdown_both, ec);
            out_socket_ssl.next_layer().close(ec);
        };
        out_socket_ssl.next_layer().cancel(ec);
        out_socket_ssl.async_shutdown(ssl_shutdown_cb);
        ssl_shutdown_timer.expires_after(chrono::seconds(SSL_SHUTDOWN_TIMEOUT));
        ssl_shutdown_timer.async_wait(ssl_shutdown_cb);
    }
    
}
