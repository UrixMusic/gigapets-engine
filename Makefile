# Parallel, incremental build of the game:   make -j12      (needs w64devkit's make; build.bat gigapets still works as before)
# The program is three .cpp files compiled apart (GP_SPLIT): a change to one of them, or to a header only it uses, rebuilds just that one.
SDL    ?= D:/libs/sdl2/x86_64-w64-mingw32
CXX    ?= g++
FLAGS  := -std=c++17 -w -DGP_NO_MAIN -DGP_SPLIT $(if $(GPDEBUG),-g) $(if $(NATIVE),-fpatchable-function-entry=16) \
          -Isrc/engine -Isrc/loaders -Isrc/game -Ithird_party -I$(SDL)/include
LIBS   := third_party/lua/liblua.a -L$(SDL)/lib -lmingw32 -lopengl32 -lSDL2main -lSDL2 -lmincore
# NATIVE=1: every function starts with padding a native mod (manifest "native") can replace it through, and the exe exports its symbols
comma  := ,
LINK   := $(if $(NATIVE),-Wl$(comma)--export-all-symbols)
OBJ    := build/obj$(if $(NATIVE),-native)
OBJS   := $(OBJ)/gigapets.o $(OBJ)/walk.o $(OBJ)/frontend_demo.o
EXE    := build/gigapets$(if $(NATIVE),-native).exe

all: $(EXE)
$(EXE): $(OBJS)
	$(CXX) $(OBJS) $(LIBS) $(LINK) -o $@
$(OBJ)/%.o: src/app/%.cpp
	@mkdir -p $(OBJ)
	$(CXX) $(FLAGS) -MMD -MP -c $< -o $@
-include $(OBJS:.o=.d)
clean:
	rm -rf build/obj build/obj-native
.PHONY: all clean
