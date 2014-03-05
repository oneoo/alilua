SYS := $(shell gcc -dumpmachine)
CC = gcc
OPTIMIZATION = -O3

CFLAGS = -lm -ldl -lpthread -lz -lssl -lcrypto $(HARDMODE)
ifeq (, $(findstring linux, $(SYS)))
CFLAGS = -liconv -lm -ldl -lpthread -lz -lssl -lcrypto $(HARDMODE)
endif

DEBUG = -g -ggdb

ifneq ($(SMPDEBUG),)
DEBUG = -g -ggdb -D SMPDEBUG
endif

ifeq ($(LUAJIT),)
ifeq ($(LUA),)
LIBLUA = -llua -L/usr/lib
else
LIBLUA = -L$(LUA) -llua
endif
else
LIBLUA = -L$(LUAJIT) -lluajit-5.1
SYS = $(shell gcc -dumpmachine)
ifneq (, $(findstring i686-apple-darwin, $(SYS)))
LIBLUA = -L$(LUAJIT) -lluajit-5.1
endif
endif

ifneq (, $(findstring i686-apple-darwin, $(SYS)))
MACGCC =  -pagezero_size 10000 -image_base 100000000
endif

ifndef $(PREFIX)
PREFIX = /usr/local/alilua
endif

INCLUDES=-I/usr/local/include -I/usr/local/include/luajit-2.0 -I/usr/local/include/luajit-2.1

all: alilua

alilua : main.o
	$(CC) objs/merry/*.o objs/deps/*.o objs/*.o -o $@ $(CFLAGS) $(DEBUG) $(LIBLUA) $(MACGCC)

main.o:
	[ -f coevent/src/coevent.h ] || (git submodule init && git submodule update)
	[ -f coevent/merry/merry.h ] || (cd coevent && git submodule init && git submodule update)
	[ -d objs ] || mkdir objs;
	[ -d objs/merry ] || mkdir objs/merry;
	cd objs/merry && $(CC) -fPIC -c ../../coevent/merry/common/*.c $(DEBUG) $(INCLUDES);
	cd objs/merry && $(CC) -fPIC -c ../../coevent/merry/se/*.c $(DEBUG) $(INCLUDES);
	cd objs/merry && $(CC) -fPIC -c ../../coevent/merry/*.c $(DEBUG) $(INCLUDES);
	cd objs && $(CC) -fPIC -c ../coevent/src/*.c $(DEBUG) $(INCLUDES);
	cd objs && $(CC) -fPIC -c ../src/*.c $(DEBUG) $(INCLUDES);
	[ -d objs/deps ] || mkdir objs/deps;
	cd objs/deps && $(CC) -fPIC -c ../../deps/*.c $(DEBUG) $(INCLUDES);
	cd objs/deps && $(CC) -fPIC -c ../../deps/yac/*.c $(DEBUG) $(INCLUDES);
	cd objs/deps && $(CC) -fPIC -c ../../deps/fastlz/*.c $(DEBUG) $(INCLUDES);

	[ -f lua-libs/bit.so ] || (cd coevent/lua-libs/LuaBitOp-1.0.2 && make LIBLUA="$(LIBLUA)" && cp bit.so ../../../lua-libs/ && make clean);
	[ -f lua-libs/cjson.so ] || (cd coevent/lua-libs/lua-cjson-2.1.0 && make LIBLUA="$(LIBLUA)" && cp cjson.so ../../../lua-libs/ && make clean);
	[ -f lua-libs/zlib.so ] || (cd coevent/lua-libs/lzlib && make LIBLUA="$(LIBLUA)" && cp zlib.so ../../../lua-libs/ && make clean && rm -rf *.o);
	[ -f lua-libs/llmdb.so ] || (cd coevent/lua-libs/lightningmdb && make LIBLUA="$(LIBLUA)" && cp llmdb.so ../../../lua-libs/);

.PHONY : clean zip install noopt hardmode

clean:
	rm -rf objs;
	rm -rf alilua;
	rm -rf lua-libs/*.so;

zip:
	git archive --format zip --prefix alilua/ -o alilua-`git log --date=short --pretty=format:"%ad" -1`.zip HEAD

install:all
	$(MAKE) DEBUG=$(OPTIMIZATION);
	strip alilua;
	[ -d $(PREFIX) ] || mkdir $(PREFIX);
	[ -d $(PREFIX)/lua-libs ] || mkdir $(PREFIX)/lua-libs;
	! [ -f $(PREFIX)/alilua ] || mv $(PREFIX)/alilua $(PREFIX)/alilua.old
	cp alilua $(PREFIX)/
	cp script.lua $(PREFIX)/
	cp alilua-index.lua $(PREFIX)/
	[ -f $(PREFIX)/host-route.lua ] || cp host-route.lua $(PREFIX)/
	cp lua-libs/*.lua $(PREFIX)/lua-libs/
	cp lua-libs/*.so $(PREFIX)/lua-libs/

noopt:
	$(MAKE) OPTIMIZATION=""

hardmode:
	$(MAKE) HARDMODE="-std=c99 -pedantic -Wall"