CC = gcc
OPTIMIZATION = -O3
CFLAGS = $(OPTIMIZATION) -lm -ldl -lpthread -lz -lssl -lcrypto $(HARDMODE) -DLUA_USER_LINUX
DEBUG = -g -rdynamic -ggdb
ifeq ($(LUAJIT),)
ifeq ($(LUA),)
LIBLUA = -llua
else
LIBLUA = -L$(LUA) -llua
endif
else
LIBLUA = -L$(LUAJIT) -lluajit-5.1
endif
ifndef $(PREFIX)
PREFIX = /usr/local/alilua
endif
#package.path = package.path .. ";../entity.lua"
all: alilua

alilua : main.o
	$(CC)  objs/*.o -o $@ $(CFLAGS) $(DEBUG) $(LIBLUA)

main.o:
	[ -d objs ] || mkdir objs;
	cd objs && $(CC) -c ../common/*.c $(CFLAGS) $(DEBUG) $(LIBLUA);
	cd objs && $(CC) -c ../se/*.c $(CFLAGS) $(DEBUG) $(LIBLUA);
	cd objs && $(CC) -c ../src/*.c $(CFLAGS) $(DEBUG) $(LIBLUA);
	cd objs && $(CC) -c ../deps/*.c $(CFLAGS) $(DEBUG) $(LIBLUA);
	cd objs && $(CC) -c ../deps/yac/*.c $(CFLAGS) $(DEBUG) $(LIBLUA);
	cd objs && $(CC) -c ../deps/fastlz/*.c $(CFLAGS) $(DEBUG) $(LIBLUA);
	cd objs && $(CC) -c ../coevent/*.c $(CFLAGS) $(DEBUG) $(LIBLUA);

	cd lua-libs/LuaBitOp-1.0.2 && make && cp bit.so ../ && make clean;
	cd lua-libs/lua-cjson-2.1.0 && make && cp cjson.so ../ && make clean;
	cd lua-libs/lzlib && make && cp zlib.so ../ && make clean;


.PHONY : clean zip install noopt hardmode

clean:
	rm -rf objs
	rm -rf alilua
	rm -rf lua-libs/*.so

zip:
	git archive --format zip --prefix alilua/ -o alilua-`git log --date=short --pretty=format:"%ad" -1`.zip HEAD

install:all
	$(MAKE) DEBUG="";
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