#pragma execution_character_set("utf-8") // 强制 MSVC 使用 UTF-8 处理字符

#include <windows.h>
#include <shellapi.h>
#include <string>

// 全局状态
std::wstring targetFile = L"";
int currentFrame = 0;
const int totalFrames = 7;

// 纯文本颜文字序列帧动画
const wchar_t* frames[] = {
    L"【召唤大将怪兽...】\n\n             ( -_-)  ",
    L"【锁定目标文件...】\n\n             ( O_O)  ",
    L"【拔出武器！】\n\n             ( Ò_Ó)▄︻┻┳═一",
    L"【开火！！！】\n\n             ( Ò_Ó)▄︻┻┳═一  💥💥💥",
    L"【目标已粉碎！】\n\n             ( ￣▽￣)╭",
    L"【事了拂衣去...】\n\n             ( ︶_︶)",
    L"【深藏功与名。】\n\n"
};

// 注册右键菜单
void RegisterContextMenu() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    HKEY hKey;
    std::wstring keyPath = L"Software\\Classes\\*\\shell\\SummonMonster";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"", 0, REG_SZ, (BYTE*)L"召唤大将怪兽摧毁(字符版)", sizeof(L"召唤大将怪兽摧毁(字符版)"));
        RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (BYTE*)exePath, (lstrlenW(exePath) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }

    std::wstring cmdPath = keyPath + L"\\command";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, cmdPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        std::wstring command = std::wstring(L"\"") + exePath + L"\" \"%1\"";
        RegSetValueExW(hKey, L"", 0, REG_SZ, (BYTE*)command.c_str(), (command.length() + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
    MessageBoxW(NULL, L"✅ 右键菜单安装成功！请右击任意文件测试。", L"Monster Deleter", MB_OK | MB_ICONINFORMATION);
}

// 卸载右键菜单
void UnregisterContextMenu() {
    std::wstring cmdPath = L"Software\\Classes\\*\\shell\\SummonMonster\\command";
    RegDeleteKeyW(HKEY_CURRENT_USER, cmdPath.c_str());
    
    std::wstring keyPath = L"Software\\Classes\\*\\shell\\SummonMonster";
    LONG res = RegDeleteKeyW(HKEY_CURRENT_USER, keyPath.c_str());
    
    if (res == ERROR_SUCCESS) {
        MessageBoxW(NULL, L"🗑️ 右键菜单已成功卸载！", L"Monster Deleter", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(NULL, L"⚠️ 未发现注册项，或卸载失败。", L"Monster Deleter", MB_OK | MB_ICONWARNING);
    }
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

// 窗口过程函数 (负责渲染与动画)
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            // 设定窗口整体半透明度 (0为全透明，255为不透明)
            SetLayeredWindowAttributes(hwnd, 0, 210, LWA_ALPHA);
            SetTimer(hwnd, 1, 500, NULL); // 每500毫秒切换一帧
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0); // Esc 退出
            break;

        case WM_TIMER:
            currentFrame++;
            if (currentFrame == 3) {
                // 在第4帧 (开火时) 执行文件删除
                SendToTrash(targetFile);
            }
            if (currentFrame >= totalFrames) {
                KillTimer(hwnd, 1);
                PostQuitMessage(0); // 动画结束退出程序
            } else {
                InvalidateRect(hwnd, NULL, TRUE); // 触发重绘
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // 绘制纯黑背景
            RECT rect;
            GetClientRect(hwnd, &rect);
            HBRUSH hBrush = CreateSolidBrush(RGB(15, 15, 15));
            FillRect(hdc, &rect, hBrush);
            DeleteObject(hBrush);

            // 设置文字样式
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 255, 100)); // 骇客绿颜色
            HFONT hFont = CreateFontW(50, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            SelectObject(hdc, hFont);

            // 绘制动画帧
            std::wstring displayStr = frames[currentFrame];
            displayStr += L"\n\n目标: " + targetFile;
            
            DrawTextW(hdc, displayStr.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            DeleteObject(hFont);
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
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) {
        targetFile = argv[1]; // 获取右键点击的文件路径
    }
    LocalFree(argv);

    // 如果没有附带文件参数（即直接双击运行），则弹出安装/卸载交互框
    if (targetFile.empty()) {
        int msgboxID = MessageBoxW(NULL, 
            L"是否要安装大将怪兽右键菜单？\n\n【是 (Yes)】：安装菜单\n【否 (No)】：卸载菜单\n【取消 (Cancel)】：退出程序", 
            L"大将怪兽设置 (Monster Deleter)", 
            MB_YESNOCANCEL | MB_ICONQUESTION);

        if (msgboxID == IDYES) {
            RegisterContextMenu();
        } else if (msgboxID == IDNO) {
            UnregisterContextMenu();
        }
        return 0;
    }

    // --- 以下为动画显示逻辑 ---

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MonsterDeleterClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    // 创建全屏置顶无边框窗口 (WS_EX_LAYERED 支持透明)
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"MonsterDeleterClass", L"Monster Deleter",
        WS_POPUP | WS_VISIBLE,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL
    );

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
