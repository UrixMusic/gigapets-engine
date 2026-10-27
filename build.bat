@echo off
set NAME=%1
set SRC=tests\%NAME%.cpp
if not exist %SRC% set SRC=tools\%NAME%.cpp
if not exist %SRC% set SRC=src\app\%NAME%.cpp
g++ -std=c++17 -Wall -Wextra -g -Isrc/engine -Isrc/loaders -Ithird_party -ID:/libs/sdl2/x86_64-w64-mingw32/include -LD:/libs/sdl2/x86_64-w64-mingw32/lib %SRC% third_party\lua\liblua.a -o build\%NAME%.exe -lmingw32 -lSDL2main -lSDL2 || exit /b 1
build\%NAME%.exe %2 %3 %4 %5