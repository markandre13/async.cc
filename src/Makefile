APP=tests

MEM=-fsanitize=address -fsanitize=leak

LLVM_DIR=$(shell for x in /opt/homebrew/opt/llvm /usr/local/opt/llvm ; do if test -d $$x ; then echo $$x ; break ; fi ; done)
CXX=$(LLVM_DIR)/bin/clang++
CFLAGS=-std=c++23 $(MEM) -O0 -g \
	-Wall -Wextra -Werror=return-type -Werror=shadow -Wno-deprecated-anon-enum-enum-conversion \
	-I$(LLVM_DIR)/include/c++ \
	-I../upstream/kaffeeklatsch/src

LDFLAGS=-L$(LLVM_DIR)/lib/c++ -Wl,-rpath,$(LLVM_DIR)/lib/c++ \
	-L/usr/local/lib $(MEM) -g

SRC = async.spec.cc ../upstream/kaffeeklatsch/src/kaffeeklatsch.cc

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

async.spec.o: async.hh
../upstream/kaffeeklatsch/src/kaffeeklatsch.o: ../upstream/kaffeeklatsch/src/kaffeeklatsch.hh
