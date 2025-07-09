@echo off
echo ======================================
echo     快速Release构建 - 生产版本
echo ======================================
echo.

echo [1/4] 清理项目...
call gradlew clean --no-daemon --quiet

echo [2/4] 构建Release APK...
call gradlew assembleRelease --no-daemon --parallel
if errorlevel 1 (
    echo Release构建失败！
    pause
    exit /b 1
)

echo [3/4] 检查APK文件...
if exist app\build\outputs\apk\release\app-release-unsigned.apk (
    echo ✅ Release APK构建成功！
) else (
    echo ❌ Release APK文件未找到
    pause
    exit /b 1
)

echo [4/4] 构建完成！
echo.
echo ======================================
echo      Release APK位置：
echo      %cd%\app\build\outputs\apk\release\app-release-unsigned.apk
echo ======================================
echo.
echo ⚠️  注意：Release版本需要签名才能安装
echo 📱 Debug版本可直接安装：app\build\outputs\apk\debug\app-debug.apk
echo.
pause
