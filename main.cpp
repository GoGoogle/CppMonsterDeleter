#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

using namespace Gdiplus;

// 全局变量
std::wstring targetFile = L"";
bool isAnimating = false;
int currentFrame = 0;
const int totalFrames = 15; // 5x3 的精灵图
Image* spriteSheet = nullptr;

// 播放 MP3/WAV 音效 (mciSendString)
void PlayAudio(const std::wstring& relativePath) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring dir = exePath;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring fullPath = dir + L"\\" + relativePath;
    std::wstring cmd = L"play \"" + fullPath + L"\"";
    mciSendStringW(cmd.c_str(), NULL, 0, NULL);
}

// 注册右键菜单
void RegisterContextMenu() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    HKEY hKey;
    std::wstring keyPath = L"Software\\Classes\\*\\shell\\SummonMonster";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"", 0, REG_SZ, (BYTE*)L"召唤大将怪兽摧毁", sizeof(L"召唤大将怪兽摧毁"));
        RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (BYTE*)exePath, (lstrlenW(exePath) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
    
    std::wstring cmdPath = keyPath + L"\\command";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, cmdPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        std::wstring command = std::wstring(L"\"") + exePath + L"\" \"%1\"";
        RegSetValueExW(hKey, L"", 0, REG_SZ, (BYTE*)command.c_str(), (command.length() + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
    MessageBoxW(NULL, L"右键菜单注册成功！请右击任意文件测试。", L"Monster Deleter", MB_OK | MB_ICONINFORMATION);
}

// 安全移至回收站
void SendToTrash(const std::wstring& path) {
    std::wstring doubleNullPath = path + L'\0';
    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = doubleNullPath.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&fileOp);
}

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 180, LWA_ALPHA); // 初始透明度
            SetCursor(LoadCursor(NULL, IDC_CROSS)); // 十字狙击光标
            PlayAudio(L"assets\\音频\\bgm(1).mp3");
            break;
            
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0); // Esc 退出
            break;

        case WM_LBUTTONDOWN:
            if (!isAnimating && !targetFile.empty()) {
                isAnimating = true;
                SendToTrash(targetFile); // 删除文件
                PlayAudio(L"assets\\音频\\爆炸.MP4"); // 播放爆炸声
                SetTimer(hwnd, 1, 100, NULL); // 启动动画定时器(约10fps)
            }
            break;

        case WM_TIMER:
            if (isAnimating) {
                currentFrame++;
                if (currentFrame >= totalFrames) {
                    KillTimer(hwnd, 1);
                    PostQuitMessage(0); // 动画结束退出程序
                } else {
                    InvalidateRect(hwnd, NULL, TRUE); // 触发重绘
                }
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            Graphics graphics(hdc);
            
            // 绘制半透明黑色背景
            SolidBrush bgBrush(Color(180, 0, 0, 0));
            graphics.FillRectangle(&bgBrush, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

            // 绘制提示文字
            if (!isAnimating) {
                FontFamily fontFamily(L"Microsoft YaHei");
                Font font(&fontFamily, 36, FontStyleBold, UnitPixel);
                SolidBrush textBrush(Color(255, 255, 255, 255));
                graphics.DrawString(L"请点击屏幕确认摧毁目标文件", -1, &font, PointF(GetSystemMetrics(SM_CXSCREEN)/2 - 200, GetSystemMetrics(SM_CYSCREEN)/2 - 100), &textBrush);
            }

            // 绘制怪兽动画
            if (isAnimating && spriteSheet) {
                int frameW = spriteSheet->GetWidth() / 5;
                int frameH = spriteSheet->GetHeight() / 3;
                int col = currentFrame % 5;
                int row = currentFrame / 5;
                
                // 绘制在屏幕中央
                int destX = (GetSystemMetrics(SM_CXSCREEN) - frameW) / 2;
                int destY = (GetSystemMetrics(SM_CYSCREEN) - frameH) / 2;
                
                graphics.DrawImage(spriteSheet, Rect(destX, destY, frameW, frameH), 
                                   col * frameW, row * frameH, frameW, frameH, UnitPixel);
            }
            EndPaint(hwnd, &ps);
            break;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 检查参数
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) targetFile = argv[1];

    if (targetFile.empty()) {
        RegisterContextMenu(); // 无参数则注册菜单
        return 0;
    }

    // 初始化 GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // 加载资源 (需要保证 assets 文件夹和 exe 在同级目录)
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring dir = exePath;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring spritePath = dir + L"\\assets\\怪兽动画_transparent.png"; // 这里替换为你的序列帧路径
    spriteSheet = new Image(spritePath.c_str());

    // 注册窗口类
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MonsterDeleterClass";
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    RegisterClassW(&wc);

    // 创建全屏置顶无边框窗口 (WS_EX_LAYERED 支持透明)
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"MonsterDeleterClass", L"Monster Deleter",
        WS_POPUP | WS_VISIBLE,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL
    );

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理
    delete spriteSheet;
    GdiplusShutdown(gdiplusToken);
    LocalFree(argv);
    return 0;
}
