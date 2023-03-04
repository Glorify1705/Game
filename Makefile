SHELL := bash
.ONESHELL:
.SHELLFLAGS := -euo pipefail -c
.DELETE_ON_ERROR:
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --no-builtin-rules

.RECIPEPREFIX = >

APPNAME ?= game
OBJDIR ?= out

LUADIR := vendor/luajit
FBSDIR := vendor/flatbuffers
BOX2DDIR := vendor/box2d

LUALIB := $(OBJDIR)/libluajit.a
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

$(OBJDIR)/flatc: $(FBSDIR)/CMakeLists.txt | $(OBJDIR)
> cmake -S$(FBSDIR) -B$(FBSDIR)/build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
	-DFLATBUFFERS_BUILD_TESTS=OFF -DFLATBUFFERS_INSTALL=OFF -DFLATBUFFERS_BUILD_FLATHASH=OFF
> $(MAKE) -C$(FBSDIR)/build -j
> cp $(FBSDIR)/build/flatc $@

src/assets_generated.h: src/assets.fbs $(OBJDIR)/flatc
> $(OBJDIR)/flatc -o src --cpp-std c++17 --cpp-static-reflection --cpp $<

usrbin/$(APPNAME): $(OBJS) $(LUALIB) $(BOX2DLIB)
> mkdir -p usrbin
> $(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(LUALIB): $(LUADIR)/Makefile | $(OBJDIR)
> make -C $(LUADIR) all
> cp $(LUADIR)/src/*.a $(OBJDIR)

$(BOX2DLIB): $(BOX2DDIR)/CMakeLists.txt | $(OBJDIR)
> cmake -S$(BOX2DDIR) -B$(BOX2DDIR)/build -DBOX2D_BUILD_DOCS=OFF -G "Unix Makefiles" \
	-DCMAKE_BUILD_TYPE=Release -DBOX2D_BUILD_UNIT_TESTS=OFF -DBOX2D_BUILD_TESTBED=OFF -DBOX2D_BUILD_DOCS=OFF
> $(MAKE) -C$(BOX2DDIR)/build
> cp $(BOX2DDIR)/build/bin/libbox2d.a $(BOX2DLIB)

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
