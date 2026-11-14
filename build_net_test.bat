@echo off
setlocal
set "ROOT=E:\C++\dbghelp"
set "PR=%ROOT%\InjectorCpp"
set "PATH=%ROOT%\tools\msvc\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%ROOT%\tools\msvc\include;%ROOT%\tools\sdk\inc\um;%ROOT%\tools\sdk\inc\shared;%ROOT%\tools\sdk\ucrt\Windows Kits\10\Include\10.0.22621.0\ucrt"
set "LIB=%ROOT%\tools\msvc\lib\x64;%ROOT%\tools\sdk\libs_store\Windows Kits\10\Lib\10.0.22621.0\um\x64;%ROOT%\tools\sdk\ucrt\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64"
set "SRC=%PR%\src"

if not exist "%PR%\build" mkdir "%PR%\build"

cl.exe /nologo /EHsc /std:c++17 /W3 /utf-8 ^
  /I"%SRC%" ^
  /Fo"%PR%\build\\" /Fe"%PR%\build\test_net.exe" ^
  "%PR%\tests\test_net.cpp" ^
  "%PR%\src\net\http.cpp" "%PR%\src\net\download.cpp" "%PR%\src\net\unzip.cpp" ^
  "%PR%\src\util\paths.cpp" "%PR%\src\util\strutil.cpp" "%PR%\src\util\logger.cpp" "%PR%\src\util\config.cpp" "%PR%\src\util\fsutil.cpp" ^
  ole32.lib advapi32.lib user32.lib
if errorlevel 1 exit /b 1
"%PR%\build\test_net.exe"
