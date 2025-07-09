@echo off
echo ======================================
echo     智能药品管理App - 快速构建脚本
echo ======================================
echo.

echo [1/3] 清理项目...
call gradlew clean --no-daemon --quiet
if errorlevel 1 (
    echo 清理失败！
    pause
    exit /b 1
)

echo [2/3] 构建Debug APK...
call gradlew assembleDebug --no-daemon --parallel --offline
if errorlevel 1 (
    echo 构建失败！
    pause
    exit /b 1
)

echo [3/3] 构建完成！
echo.
echo ======================================
echo      APK文件位置：
echo      %cd%\app\build\outputs\apk\debug\app-debug.apk
echo ======================================
echo.
echo 文件大小：
for %%I in (app\build\outputs\apk\debug\app-debug.apk) do echo %%~zI 字节 (%%~zI/1024/1024 MB)
echo.
echo 构建时间：%date% %time%
echo.
echo 可以直接将 app-debug.apk 安装到Android设备上测试！
echo.
pause
