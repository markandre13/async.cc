APP=tests

#MEM=-fsanitize=address -fsanitize=leak

CXX=/usr/local/opt/llvm/bin/clang++
CFLAGS=-std=c++23 -fmodules $(MEM) -O0 -g \
	-Wall -Wextra -Wno-deprecated-anon-enum-enum-conversion \
	-I/usr/local/opt/llvm/include/c++ \
	-I../upstream/kaffeeklatsch/src

LDFLAGS=-L/usr/local/opt/llvm/lib/c++ -Wl,-rpath,/usr/local/opt/llvm/lib/c++ \
	-L/usr/local/lib $(MEM) -g

SRC = task.spec.cc ../upstream/kaffeeklatsch/src/kaffeeklatsch.cc

OBJ = $(SRC:.cc=.o)

.SUFFIXES: .cc .o

all: $(APP)

depend:
	makedepend -I. -Y $(SRC)

run:
	./$(APP)

clean:
	rm -f $(OBJ)

$(APP): $(OBJ)
	@echo "linking..."
	$(CXX) $(LDFLAGS) $(LIB) $(OBJ) -o $(APP)

.cc.o:
	@echo compiling $*.cc ...
	$(CXX) $(CFLAGS) -c -o $*.o $*.cc

# DO NOT DELETE

task.spec.o: task.hh
