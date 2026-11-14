@echo off
setlocal
set "ROOT=E:\C++\dbghelp"
set "PR=%ROOT%\InjectorCpp"
set "PATH=%ROOT%\tools\msvc\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%ROOT%\tools\msvc\include;%ROOT%\tools\sdk\inc\um;%ROOT%\tools\sdk\inc\shared;%ROOT%\tools\sdk\ucrt\Windows Kits\10\Include\10.0.22621.0\ucrt"
set "LIB=%ROOT%\tools\msvc\lib\x64;%ROOT%\tools\sdk\libs_store\Windows Kits\10\Lib\10.0.22621.0\um\x64;%ROOT%\tools\sdk\ucrt\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64"

if not exist "%PR%\build" mkdir "%PR%\build"
pushd "%PR%"

cl.exe /nologo /EHsc /std:c++17 /W3 /utf-8 /I"src" tests\test_qq.cpp src\qq\locate.cpp src\util\paths.cpp src\util\strutil.cpp src\util\logger.cpp src\util\fsutil.cpp /Fe:build\test_qq.exe /Fo:build\ ole32.lib advapi32.lib
if errorlevel 1 (
  popd
  exit /b 1
)
build\test_qq.exe
set "TEST_EXIT=%ERRORLEVEL%"
popd
exit /b %TEST_EXIT%
