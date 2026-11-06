@echo off
set NAME=%1
rem debug info (-g) makes the compile more than twice as slow: set GPDEBUG=1 when you need gdb
set DBG=
if "%GPDEBUG%"=="1" set DBG=-g
set SRC=tests\%NAME%.cpp
if not exist %SRC% set SRC=tools\%NAME%.cpp
if not exist %SRC% set SRC=src\app\%NAME%.cpp
g++ -std=c++17 -Wall -Wextra %DBG% -Isrc/engine -Isrc/loaders -Ithird_party -ID:/libs/sdl2/x86_64-w64-mingw32/include -LD:/libs/sdl2/x86_64-w64-mingw32/lib %SRC% third_party\lua\liblua.a -o build\%NAME%.exe -lmingw32 -lopengl32 -lSDL2main -lSDL2 -lmincore || exit /b 1
build\%NAME%.exe %2 %3 %4 %5