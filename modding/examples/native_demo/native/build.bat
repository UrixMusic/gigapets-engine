@echo off
rem Build demo.dll. Needs the same g++ as the game was built with, and the game's source folder (SRC) and SDL2's include folder.
if "%SRC%"=="" set SRC=..\..\..\..\src
if "%SDL%"=="" set SDL=D:/libs/sdl2/x86_64-w64-mingw32
g++ -std=c++17 -shared -O1 -I%SRC% -I%SRC%/engine -I%SRC%/loaders -I%SRC%/game -I%SDL%/include/SDL2 -I%SDL%/include demo.cpp -o demo.dll -L%SDL%/lib -lSDL2 || exit /b 1
