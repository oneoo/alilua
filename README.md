alilua
======
A epoll based web server, inculded lua/coevent support (Only support Linux platform)

Install
--------

###Requires

Ubuntu:

sudo apt-get install libssl-dev

Fedora:

sudo yum install openssl-devel

###Install LuaJit

wget http://luajit.org/download/LuaJIT-2.0.2.tar.gz

tar zxf LuaJIT-2.0.2.tar.gz

cd LuaJIT-2.0.2

make

sudo make install

make clean

sudo ldconfig

###Install aLiLua

$git clone https://github.com/yo2oneoo/alilua.git

$cd alilua

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
