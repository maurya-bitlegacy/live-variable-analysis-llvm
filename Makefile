LLVM_CONFIG=llvm-config

CXX=clang++
CXXFLAGS=`$(LLVM_CONFIG) --cppflags` -fPIC -fno-rtti
LDFLAGS=`$(LLVM_CONFIG) --ldflags`

all: pass_LVA.so

pass_LVA.so: pass_LVA.o
	$(CXX) -shared pass_LVA.o -o pass_LVA.so $(LDFLAGS)

pass_LVA.o: pass_LVA.cpp
	$(CXX) -c pass_LVA.cpp -o pass_LVA.o $(CXXFLAGS)

clean:
	rm -f *.o *.so
