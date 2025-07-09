@echo off
echo ======================================
echo 快速重构建和调试APK
echo ======================================

echo.
echo 1. 清理项目...
call gradlew clean

echo.
echo 2. 构建Debug APK...
call gradlew assembleDebug

if %ERRORLEVEL% neq 0 (
    echo ❌ 构建失败！
    pause
    exit /b 1
)

echo.
echo ✅ 构建成功！
echo 📱 APK位置: app\build\outputs\apk\debug\app-debug.apk

echo.
echo 3. 检查已连接设备...
adb devices

echo.
echo 是否要安装到设备？(y/n)
set /p install_choice=

if /i "%install_choice%"=="y" (
    echo.
    echo 4. 安装APK到设备...
    adb install -r app\build\outputs\apk\debug\app-debug.apk
    
    if %ERRORLEVEL% equ 0 (
        echo ✅ 安装成功！
        echo.
        echo 5. 启动应用...
        adb shell am start -n com.example.myapplication/.MainActivity
        
        echo.
        echo 6. 显示应用日志 (按Ctrl+C退出)...
        echo    关注以下标签的日志:
        echo    - MqttManager: MQTT连接和数据接收
        echo    - MedicineViewModel: ViewModel数据流
        echo    - EnvironmentMonitorScreen: UI数据更新
        echo.
        timeout /t 3 /nobreak
        adb logcat -s MqttManager:* MedicineViewModel:* EnvironmentMonitorScreen:*
    ) else (
        echo ❌ 安装失败！
    )
) else (
    echo 跳过安装步骤
)

echo.
echo ======================================
echo 测试完成
echo ======================================
echo.
echo 💡 调试提示:
echo    1. 确保MQTT服务器配置正确
echo    2. 检查网络连接
echo    3. 使用test_mqtt_data_flow.py发送测试数据
echo    4. 观察应用日志中的数据流
echo.
pause
