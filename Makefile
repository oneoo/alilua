CC = gcc
LUA = -llua
CFLAGS = -lm -ldl -lpthread -lz -lssl -lcrypto
#-lluajit-5.1

all:main.o
	$(CC) -g -DLUA_USER_LINUX objs/*.o -o alilua $(CFLAGS) $(LUA) -Wl,-E -lrt;

main.o:
	[ -d objs ] || mkdir objs;
	cd objs && $(CC) -g -c -DLUA_USER_LINUX ../common/*.c $(CFLAGS) $(LUA);
	cd objs && $(CC) -g -c -DLUA_USER_LINUX ../se/*.c $(CFLAGS) $(LUA);
	cd objs && $(CC) -g -c -DLUA_USER_LINUX ../src/*.c $(CFLAGS) $(LUA);
	cd objs && $(CC) -g -c -DLUA_USER_LINUX ../deps/*.c $(CFLAGS) $(LUA);
	cd objs && $(CC) -g -c -DLUA_USER_LINUX ../deps/yac/*.c $(CFLAGS) $(LUA);
	cd objs && $(CC) -g -c -DLUA_USER_LINUX ../deps/fastlz/*.c $(CFLAGS) $(LUA);
	cd objs && $(CC) -g -c -DLUA_USER_LINUX ../coevent/*.c $(CFLAGS) $(LUA);

clean:
	rm -rf objs/*.o
	rm -rf alilua

zip:
	git archive --format zip --prefix alilua/ -o alilua-`git log --date=short --pretty=format:"%ad" -1`.zip HEAD

install:all
	$(CC) -O3 -DLUA_USER_LINUX objs/*.o -o alilua $(CFLAGS) $(LUA) -Wl,-E -lrt;
	strip alilua;
	! [ -f /usr/local/sbin/alilua ] || mv /usr/local/sbin/alilua /usr/local/sbin/alilua.old
	cp alilua /usr/local/sbin/
	cp script.lua /usr/local/sbin/
	cp alilua-index.lua /usr/local/sbin/
	[ -f /usr/local/sbin/host-route.lua ] || cp host-route.lua /usr/local/sbin/
	cp lua-libs/* /usr/lib64/lua/5.1/
	cp lua-libs/* /usr/local/lib/lua/5.1/
	cp lua-libs/* /usr/local/share/lua/5.1/
