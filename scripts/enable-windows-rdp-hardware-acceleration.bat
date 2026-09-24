@echo off
chcp 65001 >nul
echo 正在开启 RDP H.264 硬件编码...

reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services" /v UseHardwareGraphics /t REG_DWORD /d 1 /f
if errorlevel 1 goto :error
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services\H264Encoding" /v HardwareEncode /t REG_DWORD /d 1 /f
if errorlevel 1 goto :error
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services\H264Encoding" /v PrioritizeAVC444 /t REG_DWORD /d 1 /f
if errorlevel 1 goto :error
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations" /v DWMFRAMEINTERVAL /t REG_DWORD /d 15 /f
if errorlevel 1 goto :error

echo 正在刷新组策略...
gpupdate /force
if errorlevel 1 goto :error
echo.
echo =========================================
echo 执行完成。建议重启电脑并重新连接 RDP 后生效。
echo =========================================
pause
exit /b 0

:error
echo.
echo 配置失败。请右键此脚本并选择“以管理员身份运行”。
pause
exit /b 1
