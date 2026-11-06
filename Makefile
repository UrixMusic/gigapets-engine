# Parallel, incremental build of the game:   make -j12      (needs w64devkit's make; build.bat gigapets still works as before)
# The program is three .cpp files compiled apart (GP_SPLIT): a change to one of them, or to a header only it uses, rebuilds just that one.
#   make -j12                  the development build  -> build/gigapets.exe
#   make -j12 NATIVE=1         ... with what native mods need (padding at every function, exported symbols) -> build/gigapets-native.exe
#   make -j12 RELEASE=1 NATIVE=1   the release (optimised, no console, native mods on)             -> build/GigaPets.exe   (package.bat uses this)
#   GPDEBUG=1 adds -g
SDL    ?= D:/libs/sdl2/x86_64-w64-mingw32
CXX    ?= g++
comma  := ,
# the release is built without inlining: every function keeps its own address, so a native mod can replace any of them (it costs little at 30 steps a second)
RELOPT ?= -fno-inline
VARIANT := $(if $(RELEASE),-release)$(if $(NATIVE),-native)
FLAGS  := -std=c++17 -w -DGP_NO_MAIN -DGP_SPLIT $(if $(GPDEBUG),-g) $(if $(NATIVE),-fpatchable-function-entry=16) \
          $(if $(RELEASE),-O2 -DNDEBUG $(RELOPT)) -Isrc/engine -Isrc/loaders -Isrc/game -Ithird_party -I$(SDL)/include
LIBS   := third_party/lua/liblua.a -L$(SDL)/lib -lmingw32 -lopengl32 -lSDL2main -lSDL2 -lmincore
# NATIVE=1: every function starts with padding a native mod (manifest "native") can replace it through, and the exe exports its symbols
LINK   := $(if $(NATIVE),-Wl$(comma)--export-all-symbols) $(if $(RELEASE),-mwindows -static-libgcc -static-libstdc++)
OBJ    := build/obj$(VARIANT)
OBJS   := $(OBJ)/gigapets.o $(OBJ)/walk.o $(OBJ)/frontend_demo.o
EXE    := build/$(if $(RELEASE),GigaPets$(if $(NATIVE),,-plain),gigapets$(if $(NATIVE),-native)).exe

all: $(EXE)
$(EXE): $(OBJS)
	$(CXX) $(OBJS) $(LIBS) $(LINK) -o $@
$(OBJ)/%.o: src/app/%.cpp
	@mkdir -p $(OBJ)
	$(CXX) $(FLAGS) -MMD -MP -c $< -o $@
-include $(OBJS:.o=.d)
clean:
	rm -rf build/obj build/obj-native build/obj-release build/obj-release-native
.PHONY: all clean
