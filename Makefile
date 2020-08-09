




mbus.so: mbus.o mbus_utils.o
	gcc -shared -lmbus -Wl,-soname,mbus.so -o mbus.so   *.o


mbus.o: mbus.c
	gcc -Wall -fPIC -I/usr/local/include/ -c mbus.c

mbus_utilis.o: mbus_utils.c
	gcc -Wall -fPIC -c mbus_utils.c

clean:
	rm -f mbus.o mbus_utils.o mbus.so


.PHONY: clean

#gcc -Wall -fPIC -c *.c
#    gcc -shared -Wl,-soname,libctest.so.1 -o libctest.so.1.0   *.o
#
#mbus.la:
#	gcc -I. -g -O -c load_copy.c -lcommon
