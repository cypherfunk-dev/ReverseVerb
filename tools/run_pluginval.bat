@echo off
REM ============================================================================
REM  Descarga pluginval si hace falta y valida el VST3.
REM
REM  pluginval es el validador estandar de la industria (Tracktion). Prueba
REM  cosas que a mano no se te ocurren: bloques de tamano irregular, sample
REM  rates extremos, automatizacion desde otro hilo, abrir y cerrar el editor
REM  cientos de veces, y llamadas fuera de orden que un DAW real hace aunque la
REM  documentacion diga que no.
REM
REM    run_pluginval.bat        nivel 8 (recomendado)
REM    run_pluginval.bat 10     nivel maximo, mas lento y mas exigente
REM ============================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0\.."

set "LEVEL=%~1"
if "%LEVEL%"=="" set "LEVEL=8"

set "PVDIR=%~dp0pluginval"
set "PVEXE=%PVDIR%\pluginval.exe"
set "VST3=%CD%\build\ReverseVerb_artefacts\Release\VST3\ReverseVerb.vst3"

if not exist "%VST3%" (
    echo *** No encuentro el VST3 en:
    echo     %VST3%
    echo.
    echo Compilalo primero:
    echo     cmake --build build --config Release --target ReverseVerb_VST3
    exit /b 1
)

REM --- localizar pluginval ----------------------------------------------------
REM Se buscan las ubicaciones razonables antes de descargar nada: puede que ya
REM lo tengas bajado a mano. NO se hace una busqueda recursiva desde la raiz
REM del proyecto porque build\_deps contiene el arbol entero de JUCE y tardaria
REM una eternidad.
set "PVEXE="
for %%P in (
    "%PVDIR%\pluginval.exe"
    "%CD%\pluginval.exe"
    "%CD%\pluginval\pluginval.exe"
    "%CD%\pluginval_Windows\pluginval.exe"
    "%CD%\tools\pluginval.exe"
) do if not defined PVEXE if exist "%%~P" set "PVEXE=%%~P"

if not defined PVEXE (
    for /r "%PVDIR%" %%F in (pluginval.exe) do if not defined PVEXE set "PVEXE=%%F"
)

if not defined PVEXE (
    where pluginval.exe >nul 2>&1
    if not errorlevel 1 for /f "delims=" %%F in ('where pluginval.exe') do if not defined PVEXE set "PVEXE=%%F"
)

if not defined PVEXE (
    echo No encuentro pluginval. Descargando...
    if not exist "%PVDIR%" mkdir "%PVDIR%"

    REM Todo en UNA linea a proposito. El caracter ^ de continuacion de cmd NO
    REM se consume dentro de un argumento entrecomillado: se lo pasa tal cual a
    REM PowerShell, que lo interpreta como un comando inexistente.
    REM $ProgressPreference silencia la barra de progreso, que en
    REM Invoke-WebRequest ralentiza la descarga una barbaridad.
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; $ErrorActionPreference='Stop'; Invoke-WebRequest -Uri 'https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip' -OutFile '%PVDIR%\pv.zip'; Expand-Archive -Path '%PVDIR%\pv.zip' -DestinationPath '%PVDIR%' -Force; Remove-Item '%PVDIR%\pv.zip'"

    if errorlevel 1 (
        echo.
        echo *** Fallo la descarga. Bajalo a mano de:
        echo     https://github.com/Tracktion/pluginval/releases
        echo     y deja pluginval.exe en cualquiera de estas rutas:
        echo       %PVDIR%\
        echo       %CD%\
        exit /b 1
    )

    for /r "%PVDIR%" %%F in (pluginval.exe) do if not defined PVEXE set "PVEXE=%%F"
)

if not defined PVEXE (
    echo *** Sigo sin encontrar pluginval.exe.
    echo     Dejalo en %PVDIR%\ o en %CD%\
    exit /b 1
)

echo.
echo Validando con nivel de exigencia %LEVEL%...
echo   plugin:    %VST3%
echo   pluginval: !PVEXE!
echo.

REM --validate-in-process da mensajes utiles cuando algo peta. Sin el, el
REM proceso hijo muere y solo ves un codigo de salida.
"!PVEXE!" --strictness-level %LEVEL% --validate-in-process --validate "%VST3%"
set RESULT=%errorlevel%

echo.
if %RESULT%==0 (
    echo === VALIDACION SUPERADA ===
) else (
    echo *** VALIDACION FALLIDA - no lo uses en un proyecto con trabajo dentro ***
)
exit /b %RESULT%
