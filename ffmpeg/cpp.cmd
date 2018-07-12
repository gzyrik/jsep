@echo off
IF NOT DEFINED INCLUDE call "%VS140COMNTOOLS%..\..\VC\vcvarsall.bat"
cl /nologo /c /X /I %~dp0std /I "%1" /EP /Dav_always_inline="" /E /Tc "%2" 2>NUL
IF ERRORLEVEL 1 echo "** %2"