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

INCLUDES=-I$(PWD)/luajit/src/

LIBLUA = -L$(PWD)/luajit/src/ $(INCLUDES)
SYS = $(shell gcc -dumpmachine)


ifneq (, $(findstring i686-apple-darwin, $(SYS)))
MACGCC = -pagezero_size 10000 -image_base 100000000
else
MACGCC = -Wl,-E
endif

ifndef $(PREFIX)
PREFIX = /usr/local/alilua
endif

all: alilua

alilua : main.o
	$(CC) objs/merry/*.o objs/deps/*.o objs/*.o -L$(PWD)/luajit/src/ $(PWD)/luajit/src/libluajit.a $(LP) $(MACGCC) $(CFLAGS) $(DEBUG) -o $@

main.o:
	[ -f coevent/src/coevent.h ] || (git submodule init && git submodule update)
	[ -f coevent/merry/merry.h ] || (cd coevent && git submodule init && git submodule update)
	[ -d objs ] || mkdir objs;
	[ -d objs/merry ] || mkdir objs/merry;
	[ -f luajit/src/libluajit.a ] || (cd luajit && make)
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
	[ -f lua-libs/llmdb.so ] || (cd coevent/lua-libs/lightningmdb && make LIBLUA="$(LIBLUA)" && cp llmdb.so ../../../lua-libs/ && make clean);

.PHONY : clean zip install noopt hardmode

clean:
	rm -rf objs;
	rm -rf alilua;
	rm -rf lua-libs/*.so;
	cd luajit && make clean;

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
