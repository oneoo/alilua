MODNAME= fastlz
all:$(MODNAME).o
	gcc -shared objs/*.o -o $(MODNAME).so

$(MODNAME).o:
	[ -d objs ] || mkdir objs;
	cd objs && gcc -g -fPIC -c ../*.c;
	`cd ../` && gcc -g -shared objs/*.o -o $(MODNAME).so

install:
	cd objs && gcc -O3 -fPIC -c ../*.c;
	`cd ../` && gcc -shared -O3 objs/*.o -o $(MODNAME).so
	cp $(MODNAME).so $< `lua installpath.lua $(MODNAME)`

clean:
	rm -r objs;
	rm $(MODNAME).so;