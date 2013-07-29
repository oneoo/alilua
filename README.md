alilua
======
A epoll based web server, inculded lua/coevent support (Only support Linux platform)

Install
--------
$tar zxf alilua-*.tar.gz

$cd alilua-*

$sudo make install clean


Using LuaJit
--------
$sudo make install LUAJIT=/usr/local/lib

Setting Prefix
--------
$sudo make install PREFIX=/usr/local/alilua

Start
======

$sudo alilua --daemon --bind=8080

Config
======

$vi /usr/local/alilua/host-route.lua

Limits
======

Response header length < 3KB

Response body length < 900KB

Maillist
======
[https://groups.google.com/forum/?hl=en#!forum/alilua](https://groups.google.com/forum/?hl=en#!forum/alilua)
