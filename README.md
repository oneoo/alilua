aLiLua
======
A epoll/kqueue based web server, inculded lua/coevent support (support Linux/MacOS/BSD platform)

*WebSocket supported

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

CommandLine Options
======

	--bind=127.0.0.1:80  server bind. or --bind=80 for bind at 0.0.0.0:80
	--daemon             process mode
	--process=number     workers
	--log=file path      access log
	--host-route         Special route file path
	--code-cache-ttl     number of code cache time(sec) default 60 sec
	--cache-size         size of YAC shared memory cache (1m or 4096000k)

Config
======

$vi /usr/local/alilua/host-route.lua

Limits
======

Response header length < 3KB

Response body length < 900KB

Documents
======

[http://alilua.com/docs.html](http://alilua.com/docs.html)

Maillist
======
[https://groups.google.com/forum/?hl=en#!forum/alilua](https://groups.google.com/forum/?hl=en#!forum/alilua)
