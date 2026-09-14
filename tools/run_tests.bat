@echo off
REM ============================================================================
REM  Compila y ejecuta la suite de tests de DSP.
REM    run_tests.bat          barrido rapido (~4 s)
REM    run_tests.bat --full   barrido exhaustivo (minutos)
REM ============================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0\.."

cmake --build build --config Release --target ReverseVerbTests
if errorlevel 1 (
    echo.
    echo *** Fallo la COMPILACION de los tests.
    exit /b 1
)

set "EXE=build\Release\ReverseVerbTests.exe"
if not exist "%EXE%" set "EXE=build\ReverseVerbTests.exe"

if not exist "%EXE%" (
    echo.
    echo *** No encuentro el ejecutable de tests. Buscado en:
    echo       build\Release\ReverseVerbTests.exe
    echo       build\ReverseVerbTests.exe
    exit /b 1
)

echo.
"%EXE%" %*
set RESULT=%errorlevel%

echo.
if %RESULT%==0 (
    echo === SUITE DE DSP OK ===
    echo.
    echo Tests a nivel de procesador...
    cmake --build build --config Release --target ReverseVerbHostTests
    if errorlevel 1 (
        echo *** Fallo la COMPILACION de los tests de procesador.
        exit /b 1
    )
    set "HEXE=build\Release\ReverseVerbHostTests.exe"
    if not exist "!HEXE!" set "HEXE=build\ReverseVerbHostTests_artefacts\Release\ReverseVerbHostTests.exe"
    if not exist "!HEXE!" set "HEXE=build\ReverseVerbHostTests"
    "!HEXE!"
    set RESULT=!errorlevel!
)

echo.
if !RESULT!==0 (
    echo === TODO OK ===
) else (
    REM Distinguir "tests en rojo" de "el proceso murio" ahorra mucho tiempo:
    REM el ejecutable devuelve 1 si algun test falla, y cualquier otra cosa
    REM significa que se cayo antes de terminar.
    if !RESULT!==1 (
        echo *** HAY TESTS FALLANDO  ^(ver arriba cuales^)
    ) else (
        echo *** EL EJECUTABLE MURIO  ^(codigo !RESULT!^)
        echo     No es un test en rojo: el proceso termino de forma anormal.
        echo     Ejecutalo a mano para ver donde se cae:
        echo       %EXE%
    )
)
exit /b !RESULT!
