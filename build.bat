@echo off
REM Builds the .geode file (needs Geode CLI + SDK installed)
geode sdk install-binaries
geode build
echo Done. Your .geode file is in .\build\
pause
