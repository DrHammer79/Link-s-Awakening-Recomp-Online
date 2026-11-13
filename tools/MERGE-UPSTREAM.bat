@echo off
rem LA-Online: merge a new LAS Recompiled release into our source without losing our work.
rem Usage: MERGE-UPSTREAM.bat "C:\path\LASRecomp-beta_5.4-source" "C:\path\LA-Online-V13-source" "C:\path\LASRecomp-NEW-source" "C:\path\merged"
where git >nul 2>nul || (echo git is required. In MSYS2 run: pacman -S git & pause & exit /b 2)
where python >nul 2>nul && (python "%~dp0merge_upstream.py" %* ) || (py "%~dp0merge_upstream.py" %* )
pause
