@echo off
rem 主程序构建：完整 GUI 注入器
setlocal
set "ROOT=E:\C++\dbghelp"
set "PR=%ROOT%\InjectorCpp"
set "PATH=%ROOT%\tools\msvc\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%ROOT%\tools\msvc\include;%ROOT%\tools\sdk\inc\um;%ROOT%\tools\sdk\inc\shared;%ROOT%\tools\sdk\inc\winrt;%ROOT%\tools\sdk\ucrt\Windows Kits\10\Include\10.0.22621.0\ucrt;%USERPROFILE%\.nuget\packages\microsoft.web.webview2\1.0.3351.48\build\native\include"
set "LIB=%ROOT%\tools\msvc\lib\x64;%ROOT%\tools\sdk\libs_store\Windows Kits\10\Lib\10.0.22621.0\um\x64;%ROOT%\tools\sdk\ucrt\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64"
set "SRC=%PR%\src"

if not exist "%PR%\build" mkdir "%PR%\build"

cl.exe /nologo /EHsc /std:c++17 /W3 /utf-8 /DUNICODE /D_UNICODE ^
  /I"%SRC%" ^
  /Fo"%PR%\build\\" ^
  "%PR%\src\main.cpp" ^
  "%PR%\src\ui\window.cpp" "%PR%\src\ui\bridge.cpp" "%PR%\src\ui\pickfolder.cpp" ^
  "%PR%\src\qq\locate.cpp" ^
  "%PR%\src\net\http.cpp" "%PR%\src\net\download.cpp" "%PR%\src\net\unzip.cpp" ^
  "%PR%\src\inject\asar.cpp" "%PR%\src\inject\templates.cpp" "%PR%\src\inject\service.cpp" ^
  "%PR%\src\util\paths.cpp" "%PR%\src\util\strutil.cpp" "%PR%\src\util\logger.cpp" "%PR%\src\util\config.cpp" "%PR%\src\util\fsutil.cpp" ^
  /link /SUBSYSTEM:WINDOWS /OUT:"%PR%\build\MQGAInjector.exe" ^
  ole32.lib oleaut32.lib advapi32.lib user32.lib
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK: %PR%\build\MQGAInjector.exe
