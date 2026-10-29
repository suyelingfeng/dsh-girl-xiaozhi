@echo off
rem Repository-owned Windows entry point shared by VS Code tasks and terminal.
rem Optional ignored override keeps developer-specific paths outside Git.
if exist "%~dp0esp-idf.local.cmd" (
    call "%~dp0esp-idf.local.cmd" %*
    exit /b
)
if not defined IDF_PATH (
    echo Open an ESP-IDF v6.1 terminal or run the SDK export script first.
    exit /b 2
)
if defined IDF_PYTHON_ENV_PATH (
    "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" %*
) else (
    python "%IDF_PATH%\tools\idf.py" %*
)
