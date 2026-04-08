INCLUDES=-Iexample/lib1 -Iexample/lib2 -Isrc -I../src
CFLAGS=-march=morello -mabi=purecap -g $(INCLUDES)
CXXFLAGS=$(CFLAGS)
LDFLAGS=-Wl,-rpath=.

rebuild: clean all

all:	shared-example c18n-example

shared-example:	example/main.c lib1.so lib2.so lib3.so libcheritree.so libcheritreestub.a
	cc $(CFLAGS) $(LDFLAGS) -rdynamic example/main.c libcheritreestub.a -o shared-example lib1.so lib2.so libcheritree.so
	elfctl -e +nocheric18n shared-example

c18n-example:	example/main.c lib1.so lib2.so lib3.so libcheritree.so libcheritreestub.a
	cc $(CFLAGS) $(LDFLAGS) -rdynamic example/main.c libcheritreestub.a -o c18n-example lib1.so lib2.so libcheritree.so
	elfctl -e +cheric18n c18n-example

libcheritree.so: src/cheritree.c src/mapping.c src/symbol.c \
		src/util.c src/containers.cpp src/stubs.S libcheritreestub.a
	cc -fPIC -c $(CFLAGS) src/cheritree.c src/mapping.c src/symbol.c src/util.c
	c++ -fPIC -c $(CXXFLAGS) src/containers.cpp -o containers.o
	c++ -fPIC -shared $(LDFLAGS) -Wl,--version-script=src/cheritree.map \
		cheritree.o mapping.o symbol.o util.o containers.o stubs.o -o libcheritree.so

libcheritreestub.a: src/stubs.S
	cc -fPIC -c src/stubs.S
	ar -rc libcheritreestub.a stubs.o

lib1.so: example/lib1/lib1.c libcheritreestub.a
	cc -fPIC -shared $(CFLAGS) $(LDFLAGS) -Wl,--version-script=example/lib1/lib1.map example/lib1/lib1.c libcheritreestub.a -o lib1.so

lib2.so: example/lib2/lib2.c libcheritreestub.a
	cc -fPIC -shared $(CFLAGS) $(LDFLAGS) -Wl,--version-script=example/lib2/lib2.map example/lib2/lib2.c libcheritreestub.a -o lib2.so

lib3.so: example/lib3/lib3.c libcheritreestub.a
	cc -fPIC -shared $(CFLAGS) $(LDFLAGS) -Wl,--version-script=example/lib3/lib3.map example/lib3/lib3.c libcheritreestub.a -o lib3.so

clean:
	rm -f lib1.so lib2.so lib3.so libcheritree.so libcheritreestub.a \
		stubs.o cheritree.o mapping.o symbol.o util.o containers.o \
		shared-example c18n-example
