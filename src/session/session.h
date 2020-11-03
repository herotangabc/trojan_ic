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

#ifndef _SESSION_H_
#define _SESSION_H_

#include <ctime>
#include <memory>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include "core/config.h"

class Session : public std::enable_shared_from_this<Session> {
protected:
    enum {
        MAX_LENGTH = 8192,
        SSL_SHUTDOWN_TIMEOUT = 30
    };
    const Config& config;
    uint8_t in_read_buf[MAX_LENGTH];
    uint8_t out_read_buf[MAX_LENGTH];
    uint8_t udp_read_buf[MAX_LENGTH];
    uint64_t recv_len;
    uint64_t sent_len;
    time_t start_time;
    std::string out_write_buf;
    std::string udp_data_buf;
    boost::asio::ip::tcp::resolver resolver;
    boost::asio::ip::tcp::endpoint in_endpoint;
    boost::asio::ip::udp::socket udp_socket;
    boost::asio::ip::udp::endpoint udp_recv_endpoint;
    boost::asio::steady_timer ssl_shutdown_timer;
public:
    Session(const Config &config, boost::asio::io_context &io_context);
    virtual boost::asio::ip::tcp::socket& accept_socket() = 0;
    virtual void start() = 0;
    virtual ~Session();
};

#endif // _SESSION_H_
