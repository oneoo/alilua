alilua-serv
======
A epoll based web server, inculded lua/coevent support (Only support Linux platform)

Install
--------
$tar zxf alilua-*.tar.gz
$cd alilua-*
$sudo make install clean

Using LuaJit
--------
$sudo make install clean LUA=-lluajit-5.1

Start
======

$sudo alilua --daemon --bind=8080

Config
======

$vi /usr/local/sbin/host-route.lua

Limits
======

Response body length < 900KB