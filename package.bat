@echo off
rem Builds the release and lays out dist\GigaPets\ (zip that folder to share it).
rem   GigaPets\GigaPets.exe  SDL2.dll  rom\ (empty: the player drops the ROM here)  data\ (saves, settings)  mods\
set OUT=dist\GigaPets
if exist dist rmdir /s /q dist
mkdir %OUT%\rom %OUT%\data %OUT%\mods || exit /b 1
g++ -std=c++17 -O2 -DNDEBUG -mwindows -Isrc/engine -Isrc/loaders -Ithird_party -ID:/libs/sdl2/x86_64-w64-mingw32/include -LD:/libs/sdl2/x86_64-w64-mingw32/lib src\app\gigapets.cpp third_party\lua\liblua.a -o %OUT%\GigaPets.exe -static-libgcc -static-libstdc++ -lmingw32 -lopengl32 -lSDL2main -lSDL2 -lmincore || exit /b 1
copy /y D:\libs\sdl2\x86_64-w64-mingw32\bin\SDL2.dll %OUT%\ >nul
echo Drop your ROM (rom.u7) in the rom folder.> %OUT%\rom\README.txt
echo Put mods here, one folder per mod.> %OUT%\mods\README.txt
rem the modding catalog (one page: guide, Lua API, hooks, systems, data records)
python tools\gen_modding_docs.py && copy /y modding\GigaPets-Modding.html %OUT%\ >nul
if exist mods\testmod rem (the test mod is not shipped)
powershell -NoProfile -Command "Compress-Archive -Path '%OUT%' -DestinationPath dist\GigaPets.zip -Force"
echo Done: dist\GigaPets.zip
