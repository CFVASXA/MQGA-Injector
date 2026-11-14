@echo off
setlocal
set "ROOT=E:\C++\dbghelp"
set "PR=%ROOT%\InjectorCpp"
set "PATH=%ROOT%\tools\msvc\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%ROOT%\tools\msvc\include;%ROOT%\tools\sdk\inc\um;%ROOT%\tools\sdk\inc\shared;%ROOT%\tools\sdk\ucrt\Windows Kits\10\Include\10.0.22621.0\ucrt"
set "LIB=%ROOT%\tools\msvc\lib\x64;%ROOT%\tools\sdk\libs_store\Windows Kits\10\Lib\10.0.22621.0\um\x64;%ROOT%\tools\sdk\ucrt\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64"
cl.exe /nologo /EHsc /std:c++17 /W3 /utf-8 /Zi ^
  /I"%PR%\src" ^
  /Fo"%PR%\build\\" /Fe"%PR%\build\test_zip_only.exe" ^
  "%PR%\tests\test_zip_only.cpp" "%PR%\src\net\unzip.cpp" "%PR%\src\util\strutil.cpp" ^
  ole32.lib advapi32.lib
