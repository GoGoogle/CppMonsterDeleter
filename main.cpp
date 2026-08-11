#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <math.h>
#include <time.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// =========================================================================================
// 【硬编码 Unicode 字符串区域】 (彻底杜绝任何环境下的中文乱码问题)
// =========================================================================================
const wchar_t* MENU_NAME = L"\u53EC\u5524\u5927\u5C06\u602A\u517D\u6467\u6BC1(&X)"; // "召唤大将怪兽摧毁(&X)"
const wchar_t* MSG_REG_SUCCESS = L"\u2705 \u53F3\u952E\u83DC\u5355\u5B89\u88C5\u6210\u529F\uFF01"; // "✅ 右键菜单安装成功！"
const wchar_t* MSG_UNREG_SUCCESS = L"\U0001F5D1 \u53F3\u952E\u83DC\u5355\u5DF2\u5378\u8F7D\uFF01"; // "🗑️ 右键菜单已卸载！"
const wchar_t* MSG_PROMPT = L"\u662F\u5426\u8981\u5B89\u88C5[\u5927\u5C06\u602A\u517D(&X)]\u53F3\u952E\u83DC\u5355\uFF1F\n\n[Yes]: \u5B89\u88C5\n[No]: \u5378\u8F7D\n[Cancel]: \u9000\u51FA"; 

// 动画中的文本
const wchar_t* TEXT_LOCKING = L"\u76EE\u6807\u9501\u5B9A\uFF1A"; // "目标锁定："
const wchar_t* TEXT_BOOM = L"\u5927\u5C06\u602A\u517D\u6B63\u5728\u6467\u6BC1"; // "大将怪兽正在摧毁"

// 高雅的文件占用提示（附带按 X 键破局指引）
const wchar_t* TEXT_IN_USE = L"\u6B64\u7269\u6B63\u8426\u7ED5\u4E8E\u5C18\u4E16\u4E4B\u52A1\uFF0C\u6682\u4E0D\u53EF\u53BB\u3002\n\u8BF7\u6309 \u3010X\u3011 \u952E\u5F3A\u884C\u89E3\u9664\u7F81\u7ECA\uFF0C\u518D\u884C\u8D85\u5EA6\u3002";
const wchar_t* TITLE_NOTICE = L"\u5927\u5C06\u602A\u517D\u63D0\u793A"; // "大将怪兽提示"

// =========================================================================================
// 【全局变量与状态定义】
// =========================================================================================
std::wstring targetFile = L"";   // 目标文件的完整路径
std::wstring fileName = L"";     // 目标文件的纯文件名（用于动画显示）
int tick = 0;                    // 动画帧计数器
const int ANIM_TOTAL_TICKS = 90; // 正常动画总帧数

// 占用状态控制：当文件被占用时切换为 10 秒长效显示模式 (约 330 帧)
bool isFileInUse = false;        
const int IN_USE_TOTAL_TICKS = 330; 

// 记录多屏幕系统的虚拟坐标起点和宽高
int vScreenX = 0;
int vScreenY = 0;
int vScreenWidth = 0;
int vScreenHeight = 0;

// 物理爆炸粒子结构体
struct Particle {
    float x, y, vx, vy;
    int life;
};
std::vector<Particle> particles;

// =========================================================================================
// 【功能函数：注册/卸载注册表】
// =========================================================================================
void RegisterContextMenu() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    HKEY hKey;
    std::wstring keyPath = L"Software\\Classes\\*\\shell\\SummonMonster";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"", 0, REG_SZ, (BYTE*)MENU_NAME, (lstrlenW(MENU_NAME) + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (BYTE*)exePath, (lstrlenW(exePath) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
    std::wstring cmdPath = keyPath + L"\\command";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, cmdPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        std::wstring command = std::wstring(L"\"") + exePath + L"\" \"%1\"";
        RegSetValueExW(hKey, L"", 0, REG_SZ, (BYTE*)command.c_str(), (command.length() + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
    MessageBoxW(NULL, MSG_REG_SUCCESS, L"Monster Deleter", MB_OK | MB_ICONINFORMATION);
}

void UnregisterContextMenu() {
    std::wstring cmdPath = L"Software\\Classes\\*\\shell\\SummonMonster\\command";
    RegDeleteKeyW(HKEY_CURRENT_USER, cmdPath.c_str());
    std::wstring keyPath = L"Software\\Classes\\*\\shell\\SummonMonster";
    RegDeleteKeyW(HKEY_CURRENT_USER, keyPath.c_str());
    MessageBoxW(NULL, MSG_UNREG_SUCCESS, L"Monster Deleter", MB_OK | MB_ICONINFORMATION);
}

// =========================================================================================
// 【核心功能：标准文件删除】
// =========================================================================================
int SendToTrash(const std::wstring& path) {
    std::wstring doubleNullPath = path + L'\0';
    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.wFunc = FO_DELETE;          
    fileOp.pFrom = doubleNullPath.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&fileOp);
}

// =========================================================================================
// 【核心功能：强力解除占用并删除 (自包含独立实现)】
// =========================================================================================
bool ForceUnlockAndDelete(const std::wstring& path) {
    std::wstring psCmd = L"powershell -NoProfile -Command \"$path = \'" + path + L"\'; "
        L"$lockers = Get-Process | Where-Object { try { $_.Modules.FileName -eq $path } catch { $false } }; "
        L"if ($lockers) { $lockers | Stop-Process -Force }; "
        L"Add-Type -AssemblyName Microsoft.VisualBasic; "
        L"[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($path, 'OnlyErrorDialogs', 'SendToRecycleBin');\"";

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;

    if (CreateProcessW(NULL, (LPWSTR)psCmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 4000); 
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs == INVALID_FILE_ATTRIBUTES);
}

// =========================================================================================
// 【图形渲染引擎：处理双缓冲与特效】
// =========================================================================================
void RenderFrame(HWND hwnd, HDC hdcBase) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right;
    int height = rect.bottom;

    HDC hdcMem = CreateCompatibleDC(hdcBase);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcBase, width, height);
    HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

    Graphics g(hdcMem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    g.Clear(Color(15, 15, 15));

    int cx = width / 2;
    int cy = height / 2;

    if (!isFileInUse && tick >= 35 && tick <= 45) {
        cx += (rand() % 30) - 15;
        cy += (rand() % 30) - 15;
    }

    // ------------------------------------------------------------------
    // 特殊状态：文件被占用时，展示 10 秒长效提示画面
    // ------------------------------------------------------------------
    if (isFileInUse) {
        SetBkMode(hdcMem, TRANSPARENT);
        
        SetTextColor(hdcMem, RGB(255, 80, 80));
        HFONT hTitleFont = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                     DEFAULT_PITCH, L"Microsoft YaHei");
        SelectObject(hdcMem, hTitleFont);
        RECT titleRect = { 0, cy - 120, width, height };
        DrawTextW(hdcMem, TEXT_LOCKING, -1, &titleRect, DT_CENTER | DT_TOP | DT_SINGLELINE);
        DeleteObject(hTitleFont);

        SetTextColor(hdcMem, RGB(255, 215, 0));
        HFONT hBodyFont = CreateFontW(38, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                    DEFAULT_PITCH, L"Microsoft YaHei");
        SelectObject(hdcMem, hBodyFont);
        RECT bodyRect = { 100, cy - 30, width - 100, height };
        DrawTextW(hdcMem, TEXT_IN_USE, -1, &bodyRect, DT_CENTER | DT_TOP | DT_WORDBREAK);
        DeleteObject(hBodyFont);

        BitBlt(hdcBase, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);
        return;
    }

    // ------------------------------------------------------------------
    // 阶段1：瞄准锁定阶段 (0 ~ 34 帧)
    // ------------------------------------------------------------------
    if (tick < 35) {
        float progress = tick / 35.0f;          
        float scale = 4.0f - (progress * 3.0f); 
        float angle = tick * 10.0f;             
        
        g.TranslateTransform(cx, cy);
        g.RotateTransform(angle);
        g.ScaleTransform(scale, scale);

        Pen aimPen(Color(255, 255, 50, 50), 3.0f);
        g.DrawEllipse(&aimPen, -60, -60, 120, 120);
        g.DrawLine(&aimPen, -80, 0, -40, 0);
        g.DrawLine(&aimPen, 40, 0, 80, 0);
        g.DrawLine(&aimPen, 0, -80, 0, -40);
        g.DrawLine(&aimPen, 0, 40, 0, 80);
        
        SolidBrush centerBrush(Color(255, 255, 0, 0));
        g.FillEllipse(&centerBrush, -5, -5, 10, 10);
        g.ResetTransform();

        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(255, 100, 100)); 
        HFONT hFont = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                  DEFAULT_PITCH, L"Microsoft YaHei");
        SelectObject(hdcMem, hFont);
        std::wstring lockStr = std::wstring(TEXT_LOCKING) + L" " + fileName;
        RECT textRect = { 0, cy + 120, width, height }; 
        DrawTextW(hdcMem, lockStr.c_str(), -1, &textRect, DT_CENTER | DT_TOP | DT_SINGLELINE);
        DeleteObject(hFont);
    } 
    // ------------------------------------------------------------------
    // 阶段2：开火判定阶段 (精确的第 35 帧)
    // ------------------------------------------------------------------
    else if (tick == 35) {
        int result = SendToTrash(targetFile); 
        
        if (result != 0) {
            isFileInUse = true;
            tick = 0; 
            BitBlt(hdcBase, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
            return; 
        }

        for (int i = 0; i < 150; i++) {
            float angle = (rand() % 360) * 3.14159f / 180.0f;
            float speed = (rand() % 45) + 15.0f; 
            Particle p;
            p.x = cx; p.y = cy;
            p.vx = cos(angle) * speed;
            p.vy = sin(angle) * speed;
            p.life = 255; 
            particles.push_back(p);
        }
    }

    // ------------------------------------------------------------------
    // 阶段3：爆炸光效与文字渲染阶段 (第 35 帧之后)
    // ------------------------------------------------------------------
    if (tick >= 35) {
        int expTick = tick - 35;
        
        if (expTick < 15) {
            int laserAlpha = max(0, 255 - expTick * 17);
            SolidBrush laserBrush(Color(laserAlpha, 255, 100, 100)); 
            g.FillRectangle(&laserBrush, (int)(cx - 40), 0, 80, (int)cy);
            
            SolidBrush coreBrush(Color(laserAlpha, 255, 255, 255));  
            g.FillRectangle(&coreBrush, (int)(cx - 15), 0, 30, (int)cy);
        }

        int waveRadius = expTick * 60;
        int waveAlpha = max(0, 255 - expTick * 6);
        Pen wavePen(Color(waveAlpha, 255, 150, 0), 20.0f);
        g.DrawEllipse(&wavePen, cx - waveRadius, cy - waveRadius, waveRadius * 2, waveRadius * 2);

        int fireballRadius = max(0, 250 - expTick * 6);
        SolidBrush fireballBrush(Color(waveAlpha, 255, 80, 0));
        g.FillEllipse(&fireballBrush, cx - fireballRadius, cy - fireballRadius, fireballRadius * 2, fireballRadius * 2);

        for (auto& p : particles) {
            if (p.life > 0) {
                p.x += p.vx;
                p.y += p.vy;
                p.vx *= 0.88f; 
                p.vy *= 0.88f;
                p.life -= 6;   
                
                int alpha = max(0, p.life);
                SolidBrush pBrush(Color(alpha, 255, 200, 50)); 
                g.FillEllipse(&pBrush, p.x - 5, p.y - 5, 10.0f, 10.0f);
            }
        }

        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(255, 220, 50)); 
        HFONT hFont = CreateFontW(72, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                  DEFAULT_PITCH, L"Microsoft YaHei");
        SelectObject(hdcMem, hFont);
        RECT textRect = { 0, cy + 100, width, height };
        DrawTextW(hdcMem, TEXT_BOOM, -1, &textRect, DT_CENTER | DT_TOP | DT_SINGLELINE);
        DeleteObject(hFont);
    }

    BitBlt(hdcBase, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

// =========================================================================================
// 【主窗口过程：拦截消息，驱动动画与快捷键】
// =========================================================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: 
            SetLayeredWindowAttributes(hwnd, 0, 215, LWA_ALPHA); 
            SetTimer(hwnd, 1, 30, NULL); 
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                PostQuitMessage(0); 
            } 
            else if (isFileInUse && (wParam == 'X' || wParam == 'x')) {
                if (ForceUnlockAndDelete(targetFile)) {
                    isFileInUse = false;
                    tick = 35; 
                    particles.clear();
                    
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    int cx = rect.right / 2;
                    int cy = rect.bottom / 2;
                    for (int i = 0; i < 150; i++) {
                        float angle = (rand() % 360) * 3.14159f / 180.0f;
                        float speed = (rand() % 45) + 15.0f; 
                        Particle p;
                        p.x = cx; p.y = cy;
                        p.vx = cos(angle) * speed;
                        p.vy = sin(angle) * speed;
                        p.life = 255; 
                        particles.push_back(p);
                    }
                }
            }
            break;

        case WM_TIMER:
            tick++;
            if ((isFileInUse && tick > IN_USE_TOTAL_TICKS) || (!isFileInUse && tick > ANIM_TOTAL_TICKS)) {
                KillTimer(hwnd, 1);
                PostQuitMessage(0); 
            } else {
                InvalidateRect(hwnd, NULL, FALSE); 
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RenderFrame(hwnd, hdc); 
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

// =========================================================================================
// 【程序入口点】
// =========================================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    srand((unsigned int)time(NULL));

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) {
        targetFile = argv[1];
        
        size_t pos = targetFile.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            fileName = targetFile.substr(pos + 1);
        } else {
            fileName = targetFile;
        }
    }
    LocalFree(argv);

    if (targetFile.empty()) {
        int msgboxID = MessageBoxW(NULL, MSG_PROMPT, TITLE_NOTICE, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (msgboxID == IDYES) RegisterContextMenu();
        else if (msgboxID == IDNO) UnregisterContextMenu();
        return 0;
    }

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MonsterDeleterClass";
    wc.hCursor = LoadCursor(NULL, IDC_CROSS); 
    RegisterClassW(&wc);

    vScreenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    vScreenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    vScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    vScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"MonsterDeleterClass", L"Monster Deleter",
        WS_POPUP | WS_VISIBLE,
        vScreenX, vScreenY, vScreenWidth, vScreenHeight,
        NULL, NULL, hInstance, NULL
    );

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}
