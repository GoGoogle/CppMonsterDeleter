#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <math.h>
#include <time.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// 解决乱码的终极绝招：使用 Unicode 纯十六进制硬编码
// \u53EC\u5524\u5927\u5C06\u602A\u517D\u6467\u6BC1 = "召唤大将怪兽摧毁"
const wchar_t* MENU_NAME = L"\u53EC\u5524\u5927\u5C06\u602A\u517D\u6467\u6BC1"; 
const wchar_t* MSG_REG_SUCCESS = L"\u2705 \u53F3\u952E\u83DC\u5355\u5B89\u88C5\u6210\u529F\uFF01"; // ✅ 右键菜单安装成功！
// 修复 MSVC 编译错误：Emoji 必须使用大写 \U 加上 8 位十六进制
const wchar_t* MSG_UNREG_SUCCESS = L"\U0001F5D1 \u53F3\u952E\u83DC\u5355\u5DF2\u5378\u8F7D\uFF01"; // 🗑️ 右键菜单已卸载！
const wchar_t* MSG_PROMPT = L"\u662F\u5426\u8981\u5B89\u88C5[\u5927\u5C06\u602A\u517D]\u53F3\u952E\u83DC\u5355\uFF1F\n\n[Yes]: \u5B89\u88C5\n[No]: \u5378\u8F7D\n[Cancel]: \u9000\u51FA"; // 安装提示信息

std::wstring targetFile = L"";
int tick = 0; // 动画帧计数器
const int ANIM_TOTAL_TICKS = 80;

// 记录文件打击的坐标 (全局变量)
int targetX = 0;
int targetY = 0;
int vScreenX = 0;
int vScreenY = 0;

// 粒子特效结构体
struct Particle {
    float x, y, vx, vy;
    int life;
};
std::vector<Particle> particles;

// 注册右键菜单
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

// 卸载右键菜单
void UnregisterContextMenu() {
    std::wstring cmdPath = L"Software\\Classes\\*\\shell\\SummonMonster\\command";
    RegDeleteKeyW(HKEY_CURRENT_USER, cmdPath.c_str());
    std::wstring keyPath = L"Software\\Classes\\*\\shell\\SummonMonster";
    RegDeleteKeyW(HKEY_CURRENT_USER, keyPath.c_str());
    MessageBoxW(NULL, MSG_UNREG_SUCCESS, L"Monster Deleter", MB_OK | MB_ICONINFORMATION);
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

// 窗口绘制逻辑 (渲染高帧率特效)
void RenderFrame(HWND hwnd, HDC hdcBase) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right;
    int height = rect.bottom;

    // 创建双缓冲，彻底消除屏幕闪烁
    HDC hdcMem = CreateCompatibleDC(hdcBase);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcBase, width, height);
    HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

    Graphics g(hdcMem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // 绘制纯黑背景（配合窗口透明度形成磨砂暗场）
    g.Clear(Color(15, 15, 15));

    // 使用捕获到的鼠标物理坐标作为打击中心
    int cx = targetX;
    int cy = targetY;

    // 屏幕震动效果 (激光命中瞬间，整个坐标系剧烈抖动)
    if (tick >= 35 && tick <= 45) {
        cx += (rand() % 20) - 10;
        cy += (rand() % 20) - 10;
    }

    // 阶段1：准星锁定阶段
    if (tick < 35) {
        float progress = tick / 35.0f; // 0 to 1
        float scale = 3.0f - (progress * 2.0f); // 准星缩小
        float angle = tick * 8.0f; // 旋转角度
        
        g.TranslateTransform(cx, cy);
        g.RotateTransform(angle);
        g.ScaleTransform(scale, scale);

        Pen aimPen(Color(255, 255, 50, 50), 3.0f);
        g.DrawEllipse(&aimPen, -60, -60, 120, 120);
        g.DrawLine(&aimPen, -80, 0, -40, 0);
        g.DrawLine(&aimPen, 40, 0, 80, 0);
        g.DrawLine(&aimPen, 0, -80, 0, -40);
        g.DrawLine(&aimPen, 0, 40, 0, 80);
        
        // 中心锁定点
        SolidBrush centerBrush(Color(255, 255, 0, 0));
        g.FillEllipse(&centerBrush, -5, -5, 10, 10);
        g.ResetTransform();
    } 
    // 阶段2：开火前夕，生成爆炸粒子、删除文件
    else if (tick == 35) {
        SendToTrash(targetFile); // 物理打击发生

        // 初始化 150 个向外喷射的火花粒子
        for (int i = 0; i < 150; i++) {
            float angle = (rand() % 360) * 3.14159f / 180.0f;
            float speed = (rand() % 35) + 10.0f;
            Particle p;
            p.x = cx; p.y = cy;
            p.vx = cos(angle) * speed;
            p.vy = sin(angle) * speed;
            p.life = 255;
            particles.push_back(p);
        }
    }

    // 阶段3：爆炸特效渲染
    if (tick >= 35) {
        int expTick = tick - 35;
        
        // 1. 轨道激光柱 (瞬间从天而降，然后消散)
        if (expTick < 15) {
            int laserAlpha = max(0, 255 - expTick * 17);
            SolidBrush laserBrush(Color(laserAlpha, 255, 100, 100)); // 亮红色激光
            g.FillRectangle(&laserBrush, cx - 30, 0, 60, cy);
            
            // 激光核心
            SolidBrush coreBrush(Color(laserAlpha, 255, 255, 255)); 
            g.FillRectangle(&coreBrush, cx - 10, 0, 20, cy);
        }

        // 2. 冲击波光环 (迅速扩大并变淡)
        int waveRadius = expTick * 40;
        int waveAlpha = max(0, 255 - expTick * 6);
        Pen wavePen(Color(waveAlpha, 255, 150, 0), 15.0f);
        g.DrawEllipse(&wavePen, cx - waveRadius, cy - waveRadius, waveRadius * 2, waveRadius * 2);

        // 3. 爆炸核心火球
        int fireballRadius = max(0, 200 - expTick * 5);
        SolidBrush fireballBrush(Color(waveAlpha, 255, 80, 0));
        g.FillEllipse(&fireballBrush, cx - fireballRadius, cy - fireballRadius, fireballRadius * 2, fireballRadius * 2);

        // 4. 粒子飞溅系统
        for (auto& p : particles) {
            if (p.life > 0) {
                p.x += p.vx;
                p.y += p.vy;
                p.vx *= 0.90f; // 空气阻力减速
                p.vy *= 0.90f;
                p.life -= 8;   // 寿命衰减
                
                int alpha = max(0, p.life);
                SolidBrush pBrush(Color(alpha, 255, 200, 50));
                g.FillEllipse(&pBrush, p.x - 4, p.y - 4, 8.0f, 8.0f);
            }
        }
    }

    // 将双缓冲内存图像直接拷贝到屏幕，消除闪烁
    BitBlt(hdcBase, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // 在程序启动的瞬间，抓取当前鼠标坐标（也就是用户右键点击文件的位置）
            POINT pt;
            GetCursorPos(&pt);
            // 减去虚拟屏幕左上角的偏移量，以完美兼容多显示器
            targetX = pt.x - vScreenX;
            targetY = pt.y - vScreenY;

            SetLayeredWindowAttributes(hwnd, 0, 215, LWA_ALPHA); // 整体半透明暗场
            SetTimer(hwnd, 1, 30, NULL); // 30ms 刷新率 (约 33 帧/秒)
            break;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0); 
            break;

        case WM_TIMER:
            tick++;
            if (tick > ANIM_TOTAL_TICKS) {
                KillTimer(hwnd, 1);
                PostQuitMessage(0); // 动画结束
            } else {
                // 请求系统重绘
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    srand((unsigned int)time(NULL));

    // 解析右键传进来的文件路径
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) {
        targetFile = argv[1];
    }
    LocalFree(argv);

    // 双击运行：安装/卸载菜单
    if (targetFile.empty()) {
        int msgboxID = MessageBoxW(NULL, MSG_PROMPT, L"Monster Deleter", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (msgboxID == IDYES) RegisterContextMenu();
        else if (msgboxID == IDNO) UnregisterContextMenu();
        return 0;
    }

    // GDI+ 初始化
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MonsterDeleterClass";
    wc.hCursor = LoadCursor(NULL, IDC_CROSS); // 变为十字光标
    RegisterClassW(&wc);

    // 获取虚拟屏幕(所有显示器总和)的坐标和大小，支持多屏无缝打击
    vScreenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    vScreenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

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
