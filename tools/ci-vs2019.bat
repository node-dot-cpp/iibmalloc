rem script to build with Visual Studio
rem run from root as 'tools\ci-msvc2019'

rmdir /S /Q build\vs2019
mkdir build\vs2019
cd build\vs2019

cmake -DSAFEMEMORY_TEST=ON -G "Visual Studio 16 2019" ..\..
@if ERRORLEVEL 1 exit /b %ERRORLEVEL%

cmake --build . --config Release
@if ERRORLEVEL 1 exit /b %ERRORLEVEL%

ctest --output-on-failure -C Release
@if ERRORLEVEL 1 exit /b %ERRORLEVEL%
