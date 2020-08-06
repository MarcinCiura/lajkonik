.PHONY: clean gendeps
.PRECIOUS: base.o controller%.o havannah%.o lajkonik%.o mcts%.o playout%.o

CC := g++
CFLAGS := -x c -O2 -fomit-frame-pointer -std=c99 -pedantic -W -Wall -Wextra -DNDEBUG
CXXFLAGS := -O3 -fomit-frame-pointer -ansi -W -Wall -Wextra -Wshadow
LDFLAGS := -ldl -lreadline -lpthread -m64

# Execute 'make DEBUG=...' to change the debug level.
DEBUG ?= 1
ifeq "$(DEBUG)" "0"
  CXXFLAGS += -DNDEBUG
else ifeq "$(DEBUG)" "2"
  CXXFLAGS := -g
else ifeq "$(DEBUG)" "3"
  CXXFLAGS := -O3 -pg -fprofile-arcs -ftest-coverage
  LDFLAGS += -pg -fprofile-arcs
else ifeq "$(DEBUG)" "4"
  CXXFLAGS := -g -O0 -coverage
  LDFLAGS += -coverage
else ifeq "$(DEBUG)" "5"
  CXXFLAGS := -g -DDUMP_PLAYOUTS
endif

# Use awk instead of bash arays since sh does not have them.
GCC_VERSION := $(shell $(CC) --version | head -1 | awk '{print $$3}')
# 1 means that the inequality is false.
GCC_HAS_ATOMIC_OPS := 1
GCC_HAS_MARCH_NATIVE := 1

OS_TYPE := $(shell uname -s)
ifeq "$(OS_TYPE)" "Darwin"
#  CC := clang++
  CPU_COUNT := $(shell sysctl -n hw.ncpu)
  RAM_SIZE := $(shell sysctl -n hw.usermem)
else ifeq "$(OS_TYPE)" "Linux"
  CPU_COUNT := $(shell grep -c ^processor /proc/cpuinfo)
  RAM_SIZE := $(shell free -b | head -2 | tail -1 | awk '{print $$2}')
endif

NUM_THREADS ?= $(shell echo $(CPU_COUNT) | awk '{print int(0.7*$$1+1)}')
LOG2_NUM_ENTRIES ?= $(shell echo $(RAM_SIZE) | awk '{x=int(log($$1/32/2)/log(2));if(x>26)x=26;print x}')

CXXFLAGS += -DLOG2_NUM_ENTRIES=$(LOG2_NUM_ENTRIES)

ifeq "$(GCC_HAS_ATOMIC_OPS)" "1"
  CXXFLAGS += -DNUM_THREADS=$(NUM_THREADS)
else
  CXXFLAGS += -DNUM_THREADS=1
endif

ifeq "$(GCC_HAS_MARCH_NATIVE)" "1"
  CFLAGS += -march=native
  CXXFLAGS += -march=native
else
  CFLAGS += -m64
  CXXFLAGS += -m64
endif

all: lajkonik-5 lajkonik-8

lajkonik-%: lajkonik%.o mongoose.o base.o patterns.o \
 define-playout-patterns.o controller%.o frontend%.o \
 havannah%.o mcts%.o playout%.o
	$(CC) $^ $(LDFLAGS) -o $@

self-play-%: self-play%.o base.o patterns.o define-playout-patterns.o \
 controller%.o havannah%.o mcts%.o playout%.o
	$(CC) $^ $(LDFLAGS) -o $@

test: test10.o havannah10.o base.o
	$(CC) $^ $(LDFLAGS) -o $@

# Edited output of make gendeps.
controller%.o: controller.cc controller.h havannah.h base.h options.h \
 mcts.h wfhashmap.h
	$(CC) $(CXXFLAGS) -DSIDE_LENGTH=$* -c $< -o $@

frontend%.o: frontend.cc frontend.h controller.h havannah.h \
 define-playout-patterns.h
	$(CC) $(CXXFLAGS) -DSIDE_LENGTH=$* -c $< -o $@

havannah%.o: havannah.cc havannah.h base.h rng.h
	$(CC) $(CXXFLAGS) -DSIDE_LENGTH=$* -c $< -o $@

test%.o: test.cc fct.h havannah.h base.h
	$(CC) $(CXXFLAGS) -DSIDE_LENGTH=$* -c $< -o $@

lajkonik%.o: lajkonik.cc controller.h havannah.h base.h options.h \
 define-playout-patterns.h patterns.h rng.h mcts.h mongoose.h playout.h
	$(CC) $(CXXFLAGS) -DSIDE_LENGTH=$* -c $< -o $@

mcts%.o: mcts.cc mcts.h havannah.h base.h options.h playout.h patterns.h \
 rng.h wfhashmap.h
	$(CC) $(CXXFLAGS) -DSIDE_LENGTH=$* -c $< -o $@

playout%.o: playout.cc playout.h havannah.h base.h options.h patterns.h \
 rng.h
	$(CC) $(CXXFLAGS) -DSIDE_LENGTH=$* -c $< -o $@

self-play%.o: self-play.cc controller.h havannah.h base.h options.h \
 define-playout-patterns.h patterns.h rng.h mcts.h playout.h
	$(CC) $(CXXFLAGS) -DSIDE_LENGTH=$* -c $< -o $@

base.o: base.cc base.h
define-playout-patterns.o: define-playout-patterns.cc \
 define-playout-patterns.h patterns.h base.h rng.h \
 define-playout-patterns.inc define-experimental-playout-patterns.inc
mongoose.o: mongoose.c mongoose.h
patterns.o: patterns.cc patterns.h base.h rng.h

clean:
	$(RM) *.o *.gcda *.gcno *gcov gmon.out lajkonik-* self-play-* test

fresh: clean all

gendeps:
	ls -1 *.cc *.c | xargs -L 1 cc -M -MM
