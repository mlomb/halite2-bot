::call "E:\Visual Studio 2017\VC\Auxiliary\Build\vcvars64.bat"

cd "C:\Users\Karin\Desktop\Halite-CPP"
call make.bat
cd "C:\Users\Karin\Desktop\Halite-CPP"
::-W 384 -H 256
::-W 240 -H 160
py hlt_client/client.py gym -r ".\Latest\MyBot.exe" -r ".\v40\MyBot.exe"  -b halite -i 3000
pause