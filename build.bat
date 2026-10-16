@echo off
rem Builds Choosen.exe with the Visual Studio C++ compiler (MSVC).
rem   build.bat           release build
rem   build.bat test      release build, then run the self-test
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
)
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
rem Choosen V3 (web3\, Svelte): its three built files are embedded like the Version 1 page.
pushd web3 || exit /b 1
if not exist node_modules (call npm ci --ignore-scripts --no-audit --no-fund || (popd & exit /b 1))
call npm run build --silent || (popd & exit /b 1)
popd
rc /nologo /fo build\choosen.res res\choosen.rc || exit /b 1
cl /nologo /O2 /GL /EHsc /std:c++20 /utf-8 /W3 /MT /DUNICODE /D_UNICODE /Fo:build\ /Fe:Choosen.exe ^
  src\main.cpp src\core.cpp src\core_library.cpp src\core_journal.cpp src\core_schedule.cpp src\core_api.cpp src\core_export.cpp src\http.cpp src\thumbs.cpp src\common.cpp src\common_win.cpp src\api_routes.cpp src\qr.cpp src\platform_win.cpp src\remote.cpp src\selftest.cpp ^
  build\choosen.res ^
  /link /LTCG /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup ^
  ws2_32.lib ole32.lib oleaut32.lib shell32.lib shlwapi.lib windowscodecs.lib user32.lib gdi32.lib || exit /b 1
if /i not "%~1"=="test" exit /b 0
pushd web3 && call npm test --silent || (popd & exit /b 1)
popd
"%~dp0Choosen.exe" --selftest
set RC=%errorlevel%
type build\selftest.txt
exit /b %RC%
