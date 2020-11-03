# trojan_ic

copy from [trojan-gfw](https://github.com/trojan-gfw/trojan)

add support for connect trojan server via http proxy

add some property for config.json

config.json:

```json
{
    "run_type": "client",
    "local_addr": "127.0.0.1",
    "local_port": 1080,
    "remote_addr": "example.com",
    "remote_port": 443,
    "password": [
        "password1"
    ],
    "client_proxy": {
        "host":"ip or hostname",
        "port":8080,
        "_comment":"basic Auth type only till now",
        "user":"user",
        "password":"password"
    },
    "log_level": 1,
    "ssl": {
        "verify": true,
        "verify_hostname": true,
        "cert": "",
        "cipher": "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:DHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA:AES128-SHA:AES256-SHA:DES-CBC3-SHA",
        "cipher_tls13": "TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_256_GCM_SHA384",
        "sni": "",
        "alpn": [
            "h2",
            "http/1.1"
        ],
        "reuse_session": true,
        "session_ticket": false,
        "curves": ""
    },
    "tcp": {
        "no_delay": true,
        "keep_alive": true,
        "reuse_port": false,
        "fast_open": false,
        "fast_open_qlen": 20
    }
}
```

if `client_proxy.host` is empty,then direct connect the trojan server;
if `client_proxy.user` or `client_proxy.user` is empty then connect the proxy server without authentication info.

support **http tunel Basic authentication** till now.
built and run OK with MacOs(10.14.6) (VS code) as a sock5 proxy. no verfication for other OS.
can see more detail in `CMakeLists.txt` for build component.

## In the Feature

- refactor http proxy connect request with fully aysnc mode.
- support **http tunel NTML authentication**
- suppert http proxy for client mode it self,because it's deficult for common user to user socks5 proxy on windows 10 system.

I'm a newer to C++, don't ask me for C++ questions.

## License

[GPLv3](LICENSE)
