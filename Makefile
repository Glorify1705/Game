SHELL := bash
.ONESHELL:
.SHELLFLAGS := -euo pipefail -c
.DELETE_ON_ERROR:
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --no-builtin-rules

.RECIPEPREFIX = >

APPNAME ?= game
OBJDIR ?= obj

LUADIR := vendor/lua-5.4.4
FBSDIR := vendor/flatbuffers-23.1.21
BOX2DDIR := vendor/box2d

LUALIB := $(OBJDIR)/liblua.a
BOX2DLIB := $(OBJDIR)/libbox2d.a

CXX ?= g++
INCLUDES += -I$(LUADIR)/src -I$(FBSDIR)/include -I$(BOX2DDIR)/include
CXXFLAGS += -std=c++17 -O1 -Wall -Werror -Wextra -DNDEBUG -DGAME_WITH_ASSERTS -fno-omit-frame-pointer -ggdb $(shell sdl2-config --cflags) $(INCLUDES)

LDFLAGS += -pthread
LDLIBS += $(shell sdl2-config --libs) -lGL -ldl -lSDL2_mixer $(LUALIB) $(BOX2DLIB)

SRCS := $(wildcard src/*.cc) src/vec.h src/mat.h src/assets_generated.h
OBJS := $(patsubst src/%.cc, $(OBJDIR)/%.o, $(filter %.cc, $(SRCS)))

ASSETS := $(wildcard assets/*.lua assets/*.ogg assets/*.qoi assets/*.xml)

all: $(APPNAME) .ccls

.ccls: Makefile
> printf "clang\n" > .ccls
> printf "%s\n" $(CXXFLAGS) >> .ccls
> printf "%s\n" $(LDFLAGS) >> .ccls
> printf "%s\n" $(LDLIBS) >> .ccls

$(APPNAME): usrbin/$(APPNAME)

bin/flatc: $(FBSDIR)/CMakeLists.txt | $(OBJDIR)
> cmake -S$(FBSDIR) -B$(FBSDIR) -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
> $(MAKE) -C$(FBSDIR) -j
> cp $(FBSDIR)/flatc $@
> touch $<

src/assets_generated.h: src/assets.fbs bin/flatc
> bin/flatc -o src --cpp-std c++17 --cpp-static-reflection --cpp $<

usrbin/$(APPNAME): obj/assets.o $(OBJS) $(LUALIB) $(BOX2DLIB)
> mkdir -p usrbin
> $(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(LUALIB): $(LUADIR)/Makefile | $(OBJDIR)
> $(MAKE) -C $(LUADIR) all
> cp $(LUADIR)/src/liblua.a $(OBJDIR)
> touch $<

$(BOX2DLIB): $(BOX2DDIR)/CMakeLists.txt | $(OBJDIR)
> cmake -S$(BOX2DDIR) -B$(BOX2DDIR) -DBOX2D_BUILD_DOCS=OFF -G "Unix Makefiles" \
	-DCMAKE_BUILD_TYPE=Release -DBOX2D_BUILD_UNIT_TESTS=OFF -DBOX2D_BUILD_TESTBED=OFF -DBOX2D_BUILD_DOCS=OFF
> $(MAKE) -C$(BOX2DDIR)
> cp $(BOX2DDIR)/bin/libbox2d.a $(BOX2DLIB)

$(OBJDIR):
> mkdir -p -- $(OBJDIR)

$(OBJDIR)/%.o: src/%.cc Makefile | $(OBJDIR)
> $(CXX) $(CXXFLAGS) -MM -MT $@ src/$*.cc | sed "s/$(shell printf '\t')/>/" >$(OBJDIR)/$*.dep
> $(CXX) $(CXXFLAGS) -Wall -c -o $@ src/$*.cc

clean:
> rm -rf -- $(OBJDIR)

usrbin/assets.bin: usrbin/$(APPNAME) $(ASSETS)
> mkdir -p usrbin
> $< packer $@ $(ASSETS)

run: usrbin/assets.bin usrbin/$(APPNAME)
> usrbin/$(APPNAME) usrbin/assets.bin

asan: clean
> CXX="clang++" CXXFLAGS="-fsanitize=address -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address" $(MAKE)

pprof: clean
> LDFLAGS="-Wl,--no-as-needed -lprofiler -Wl,--as-needed" $(MAKE)
> CPUPROFILE=/tmp/run-$(shell date +"%Y-%m-%d-%H-%M-%s").prof usrbin/game usrbin/assets.bin

.PHONY: all clean run asan

-include $(OBJS:.o=.dep)
