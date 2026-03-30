CXXFLAGS=-Og

all:	libcheritree.so libcheritreestub.a

libcheritree.so: src/cheritree.cpp src/capabilities.cpp src/mapping.cpp src/symbol.cpp
	c++ -shared $(CXXFLAGS) \
		src/cheritree.cpp src/capabilities.cpp src/mapping.cpp src/symbol.cpp \
		-Wl,--version-script=src/cheritree.map \
		-nodefaultlibs -lc -Wl,-Bstatic -lc++ -lcxxrt -lgcc_eh -Wl,-Bdynamic \
		-o libcheritree.so

libcheritreestub.a: src/stubs.S
	cc -c src/stubs.S -o stubs.o
	ar -rc libcheritreestub.a stubs.o

clean:
	rm -f libcheritree.so stubs.o libcheritreestub.a
