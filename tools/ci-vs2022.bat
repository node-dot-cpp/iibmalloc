rem script to build with Visual Studio
rem run from root as 'tools\ci-msvc2022'

rmdir /S /Q build\vs2022
mkdir build\vs2022
cd build\vs2022

cmake -DSAFEMEMORY_TEST=ON -G "Visual Studio 17 2022" ..\..
@if ERRORLEVEL 1 exit /b %ERRORLEVEL%

cmake --build . --config Release
@if ERRORLEVEL 1 exit /b %ERRORLEVEL%

ctest --output-on-failure -C Release
@if ERRORLEVEL 1 exit /b %ERRORLEVEL%
