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

#include "config.h"
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string.hpp>
//#include <openssl/sha.h>
#include <openssl/evp.h>
using namespace std;
using namespace boost::property_tree;
#define uchar (unsigned char)


    // Config::Config(Config && other) noexcept
    // {
    // }
void Config::load(const string &filename) {
    ptree tree;
    read_json(filename, tree);
    populate(tree);
}

void Config::populate(const std::string &JSON) {
    istringstream s(JSON);
    ptree tree;
    read_json(s, tree);
    populate(tree);
}

void Config::populate(const ptree &tree) {
    string rt = tree.get("run_type", string("client"));
    if (rt == "server") {
        run_type = SERVER;
    } else if (rt == "forward") {
        run_type = FORWARD;
    } else if (rt == "nat") {
        run_type = NAT;
    } else if (rt == "client") {
        run_type = CLIENT;
    } else {
        throw runtime_error("wrong run_type in config file");
    }
    if(run_type == CLIENT){
        client_proxy.host = tree.get("client_proxy.host", string());
        if(!client_proxy.host.empty()){
            auto method_ = boost::to_upper_copy(tree.get("client_proxy.method", "BASIC"));
            if(method_ != "BASIC" && method_ != "NTLM")
            {
                throw runtime_error("wrong proxy method in config file");
            } else {
                client_proxy.method = method_;
            }
            client_proxy.port = tree.get("client_proxy.port", 8080);
            client_proxy.user = tree.get("client_proxy.user", string());
            client_proxy.password = tree.get("client_proxy.password", string());

            if (client_proxy.method == "BASIC")
            {
                if (!client_proxy.user.empty() && !client_proxy.password.empty())
                {
                    client_proxy.basic_auth = "Proxy-Authorization: Basic " + encode_base64(client_proxy.user + ":" + client_proxy.password, Base64Encoding);
                } else {
                    client_proxy.basic_auth = string();
                }
            }
        }
    }
    local_addr = tree.get("local_addr", string());
    local_port = tree.get("local_port", uint16_t());
    remote_addr = tree.get("remote_addr", string());
    remote_port = tree.get("remote_port", uint16_t());
    target_addr = tree.get("target_addr", string());
    target_port = tree.get("target_port", uint16_t());
    map<string, string>().swap(password);
    for (auto& item: tree.get_child("password")) {
        string p = item.second.get_value<string>();
        password[SHA224(p)] = p;
    }

    

    udp_timeout = tree.get("udp_timeout", 60);
    log_level = static_cast<Log::Level>(tree.get("log_level", 1));
    ssl.verify = tree.get("ssl.verify", true);
    ssl.verify_hostname = tree.get("ssl.verify_hostname", true);
    ssl.cert = tree.get("ssl.cert", string());
    ssl.key = tree.get("ssl.key", string());
    ssl.key_password = tree.get("ssl.key_password", string());
    ssl.cipher = tree.get("ssl.cipher", string());
    ssl.cipher_tls13 = tree.get("ssl.cipher_tls13", string());
    ssl.prefer_server_cipher = tree.get("ssl.prefer_server_cipher", true);
    ssl.sni = tree.get("ssl.sni", string());
    ssl.alpn = "";
    for (auto& item: tree.get_child("ssl.alpn")) {
        string proto = item.second.get_value<string>();
        ssl.alpn += (char)((unsigned char)(proto.length()));
        ssl.alpn += proto;
    }
    ssl.reuse_session = tree.get("ssl.reuse_session", true);
    ssl.session_ticket = tree.get("ssl.session_ticket", false);
    ssl.session_timeout = tree.get("ssl.session_timeout", long(600));
    ssl.plain_http_response = tree.get("ssl.plain_http_response", string());
    ssl.curves = tree.get("ssl.curves", string());
    ssl.dhparam = tree.get("ssl.dhparam", string());
    tcp.prefer_ipv4 = tree.get("tcp.prefer_ipv4", false);
    tcp.no_delay = tree.get("tcp.no_delay", true);
    tcp.keep_alive = tree.get("tcp.keep_alive", true);
    tcp.reuse_port = tree.get("tcp.reuse_port", false);
    tcp.fast_open = tree.get("tcp.fast_open", false);
    tcp.fast_open_qlen = tree.get("tcp.fast_open_qlen", 20);
    mysql.enabled = tree.get("mysql.enabled", false);
    mysql.server_addr = tree.get("mysql.server_addr", string("127.0.0.1"));
    mysql.server_port = tree.get("mysql.server_port", uint16_t(3306));
    mysql.database = tree.get("mysql.database", string("trojan"));
    mysql.username = tree.get("mysql.username", string("trojan"));
    mysql.password = tree.get("mysql.password", string());
}

bool Config::sip003() {
    char *JSON = getenv("SS_PLUGIN_OPTIONS");
    if (JSON == nullptr) {
        return false;
    }
    populate(JSON);
    switch (run_type) {
        case SERVER:
            local_addr = getenv("SS_REMOTE_HOST");
            local_port = atoi(getenv("SS_REMOTE_PORT"));
            break;
        case CLIENT:
        case NAT:
            throw runtime_error("SIP003 with wrong run_type");
        case FORWARD:
            remote_addr = getenv("SS_REMOTE_HOST");
            remote_port = atoi(getenv("SS_REMOTE_PORT"));
            local_addr = getenv("SS_LOCAL_HOST");
            local_port = atoi(getenv("SS_LOCAL_PORT"));
            break;
    }
    return true;
}

string Config::encode_base64(const  string sourceData, int options = Base64Encoding)
{
    const char alphabet_base64[] = "ABCDEFGH" "IJKLMNOP" "QRSTUVWX" "YZabcdef"
                                   "ghijklmn" "opqrstuv" "wxyz0123" "456789+/";
    const char alphabet_base64url[] = "ABCDEFGH" "IJKLMNOP" "QRSTUVWX" "YZabcdef"
                                      "ghijklmn" "opqrstuv" "wxyz0123" "456789-_";
    const char *const alphabet = options & Base64UrlEncoding ? alphabet_base64url : alphabet_base64;
    const char padchar = '=';
    int padlen = 0;
 
    std::string tmp;
    tmp.resize((sourceData.size() + 2) / 3 * 4);
 
    size_t i = 0;
    char *out = &tmp[0];
    while (i < sourceData.size()) {
        // encode 3 bytes at a time
        int chunk = 0;
        chunk |= int(uchar(sourceData.data()[i++])) << 16;
        if (i == sourceData.size()) {
            padlen = 2;
        } else {
            chunk |= int(uchar(sourceData.data()[i++])) << 8;
            if (i == sourceData.size())
                padlen = 1;
            else
                chunk |= int(uchar(sourceData.data()[i++]));
        }
 
        int j = (chunk & 0x00fc0000) >> 18;
        int k = (chunk & 0x0003f000) >> 12;
        int l = (chunk & 0x00000fc0) >> 6;
        int m = (chunk & 0x0000003f);
        *out++ = alphabet[j];
        *out++ = alphabet[k];
 
        if (padlen > 1) {
            if ((options & OmitTrailingEquals) == 0)
                *out++ = padchar;
        } else {
            *out++ = alphabet[l];
        }
        if (padlen > 0) {
            if ((options & OmitTrailingEquals) == 0)
                *out++ = padchar;
        } else {
            *out++ = alphabet[m];
        }
    }
    assert((options & OmitTrailingEquals) || (out == tmp.size() + tmp.data()));
    if (options & OmitTrailingEquals)
        tmp.resize(out - tmp.data());
    return tmp;
}


string Config::SHA224(const string &message) {
    uint8_t digest[EVP_MAX_MD_SIZE];
    char mdString[(EVP_MAX_MD_SIZE << 1) + 1];
    unsigned int digest_len;
    EVP_MD_CTX *ctx;
    if ((ctx = EVP_MD_CTX_new()) == nullptr) {
        throw runtime_error("could not create hash context");
    }
    if (!EVP_DigestInit_ex(ctx, EVP_sha224(), nullptr)) {
        EVP_MD_CTX_free(ctx);
        throw runtime_error("could not initialize hash context");
    }
    if (!EVP_DigestUpdate(ctx, message.c_str(), message.length())) {
        EVP_MD_CTX_free(ctx);
        throw runtime_error("could not update hash");
    }
    if (!EVP_DigestFinal_ex(ctx, digest, &digest_len)) {
        EVP_MD_CTX_free(ctx);
        throw runtime_error("could not output hash");
    }

    for (unsigned int i = 0; i < digest_len; ++i) {
        sprintf(mdString + (i << 1), "%02x", (unsigned int)digest[i]);
    }
    mdString[digest_len << 1] = '\0';
    EVP_MD_CTX_free(ctx);
    return string(mdString);
}
