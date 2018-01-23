::call "E:\Visual Studio 2017\VC\Auxiliary\Build\vcvars64.bat"

cd "C:\Users\Karin\Desktop\Halite-CPP"
call make.bat
cd "C:\Users\Karin\Desktop\Halite-CPP"
halite.exe -d "384 256" ".\Latest\MyBot.exe" ".\v40\MyBot.exe"
:: 240 160
:: 384 256
pause