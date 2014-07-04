aLiLua
======

A epoll/kqueue based web server, inculded lua/coevent support (support Linux/MacOS/BSD platform)

 - WebSockets supported
 - HTTPS supported

Install
-------

### Requirements

 - openssl headers

Ubuntu:

```bash
$ sudo apt-get install libssl-dev
```

Fedora:

```bash
sudo yum install openssl-devel
```

### Install aLiLua

```bash 
$ git clone https://github.com/oneoo/alilua.git
$ cd alilua
$ sudo make install clean
```

Start
-----

```bash
$ sudo alilua --daemon --bind=8080
```


Options
-------

```
	--bind=127.0.0.1:80  server bind. or --bind=80 for bind at 0.0.0.0:80
	--ssl-bind           ssl server bind.
	--ssl-cert           ssl Certificate file path
	--ssl-key            ssl PrivateKey file path
	--ssl-ca             ssl Client Certificate file path
	--daemon             process mode
	--process=number     workers
	--log=file path      error log
	--accesslog=...      access log
	--host-route         Special route file path
	--app				 Special app file path
	--code-cache-ttl     number of code cache time(sec) default 60 sec
	--cache-size         size of YAC shared memory cache (1m or 4096000k)
```

Default Config file: `/usr/local/alilua/host-route.lua`

Limits
======

```
Response header length:		< 3KB
Response body length:		unlimited
```

Docs
=====

http://alilua.com/docs.html

Mailing list
============
https://groups.google.com/forum/?hl=en#!forum/alilua
