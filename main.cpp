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
const wchar_t* MENU_NAME = L"\u53EC\u5524\u5927\u5C06\u602A\u517D\u6467\u6BC1"; // "召唤大将怪兽摧毁"
const wchar_t* MSG_REG_SUCCESS = L"\u2705 \u53F3\u952E\u83DC\u5355\u5B89\u88C5\u6210\u529F\uFF01"; // "✅ 右键菜单安装成功！"
const wchar_t* MSG_UNREG_SUCCESS = L"\U0001F5D1 \u53F3\u952E\u83DC\u5355\u5DF2\u5378\u8F7D\uFF01"; // "🗑️ 右键菜单已卸载！"
const wchar_t* MSG_PROMPT = L"\u662F\u5426\u8981\u5B89\u88C5[\u5927\u5C06\u602A\u517D]\u53F3\u952E\u83DC\u5355\uFF1F\n\n[Yes]: \u5B89\u88C5\n[No]: \u5378\u8F7D\n[Cancel]: \u9000\u51FA"; 

// 动画中的文本
const wchar_t* TEXT_LOCKING = L"\u76EE\u6807\u9501\u5B9A\uFF1A"; // "目标锁定："
const wchar_t* TEXT_BOOM = L"\u5927\u5C06\u602A\u517D\u6B63\u5728\u6467\u6BC1"; // "大将怪兽正在摧毁"

// 高雅的文件占用提示 ("此物正萦绕于尘世之务，暂不可去。请解其羁绊，再行超度。")
const wchar_t* MSG_IN_USE = L"\u6B64\u7269\u6B63\u8426\u7ED5\u4E8E\u5C18\u4E16\u4E4B\u52A1\uFF0C\u6682\u4E0D\u53EF\u53BB\u3002\n\n\u8BF7\u89E3\u5176\u7F81\u7ECA\uFF0C\u518D\u884C\u8D85\u5EA6\u3002";
const wchar_t* TITLE_NOTICE = L"\u5927\u5C06\u602A\u517D\u63D0\u793A"; // "大将怪兽提示"

// =========================================================================================
// 【全局变量与结构体定义】
// =========================================================================================
std::wstring targetFile = L""; // 目标文件的完整路径
std::wstring fileName = L"";   // 目标文件的纯文件名（用于动画显示）
int tick = 0;                  // 动画帧计数器
const int ANIM_TOTAL_TICKS = 90; // 动画总帧数

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
    // 创建右键菜单主键
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"", 0, REG_SZ, (BYTE*)MENU_NAME, (lstrlenW(MENU_NAME) + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (BYTE*)exePath, (lstrlenW(exePath) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
    // 创建 command 子键执行程序
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
// 【核心功能：带状态拦截的文件删除】
// 返回 0 表示成功，非 0 表示失败（多半是被占用）
// =========================================================================================
int SendToTrash(const std::wstring& path) {
    std::wstring doubleNullPath = path + L'\0';
    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.wFunc = FO_DELETE;          // 执行删除操作
    fileOp.pFrom = doubleNullPath.c_str();
    // 允许撤销(进回收站) | 不显示确认框 | 静默模式 | 不显示系统报错(由我们自己接管错误)
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&fileOp);
}

// =========================================================================================
// 【图形渲染引擎：处理双缓冲与特效】
// =========================================================================================
void RenderFrame(HWND hwnd, HDC hdcBase) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right;
    int height = rect.bottom;

    // 创建 GDI 双缓冲，彻底消除屏幕闪烁
    HDC hdcMem = CreateCompatibleDC(hdcBase);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcBase, width, height);
    HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

    Graphics g(hdcMem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // 绘制纯黑背景（配合窗口自身的 215 级透明度，形成暗场遮罩）
    g.Clear(Color(15, 15, 15));

    // 计算屏幕绝对中心点，无论跨越几个显示器，都在视野正中央
    int cx = width / 2;
    int cy = height / 2;

    // 屏幕震动逻辑 (当激光命中的那 10 帧里，中心坐标剧烈抖动)
    if (tick >= 35 && tick <= 45) {
        cx += (rand() % 30) - 15;
        cy += (rand() % 30) - 15;
    }

    // ------------------------------------------------------------------
    // 阶段1：瞄准锁定阶段 (0 ~ 34 帧)
    // ------------------------------------------------------------------
    if (tick < 35) {
        float progress = tick / 35.0f;          // 进度 0.0 ~ 1.0
        float scale = 4.0f - (progress * 3.0f); // 准星由大快速缩小，营造锁定感
        float angle = tick * 10.0f;             // 准星旋转速度
        
        g.TranslateTransform(cx, cy);
        g.RotateTransform(angle);
        g.ScaleTransform(scale, scale);

        // 绘制科幻红圈准星
        Pen aimPen(Color(255, 255, 50, 50), 3.0f);
        g.DrawEllipse(&aimPen, -60, -60, 120, 120);
        g.DrawLine(&aimPen, -80, 0, -40, 0);
        g.DrawLine(&aimPen, 40, 0, 80, 0);
        g.DrawLine(&aimPen, 0, -80, 0, -40);
        g.DrawLine(&aimPen, 0, 40, 0, 80);
        
        // 绘制中心锁定点
        SolidBrush centerBrush(Color(255, 255, 0, 0));
        g.FillEllipse(&centerBrush, -5, -5, 10, 10);
        g.ResetTransform();

        // 绘制悬浮的文件名文字 ("目标锁定: XXX.txt")
        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(255, 100, 100)); // 红色警告字
        HFONT hFont = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                  DEFAULT_PITCH, L"Microsoft YaHei");
        SelectObject(hdcMem, hFont);
        std::wstring lockStr = std::wstring(TEXT_LOCKING) + L" " + fileName;
        RECT textRect = { 0, cy + 120, width, height }; // 放在准星下方
        DrawTextW(hdcMem, lockStr.c_str(), -1, &textRect, DT_CENTER | DT_TOP | DT_SINGLELINE);
        DeleteObject(hFont);
    } 
    // ------------------------------------------------------------------
    // 阶段2：开火判定阶段 (精确的第 35 帧)
    // ------------------------------------------------------------------
    else if (tick == 35) {
        int result = SendToTrash(targetFile); // 执行物理打击
        
        // 如果返回非0，说明文件被系统占用或其他原因无法删除
        if (result != 0) {
            // 弹出高雅文言文提示框
            MessageBoxW(hwnd, MSG_IN_USE, TITLE_NOTICE, MB_OK | MB_ICONWARNING);
            PostQuitMessage(0); // 直接终止动画并退出
            return; 
        }

        // 删除成功，初始化 150 个向外喷射的火花粒子用于爆炸
        for (int i = 0; i < 150; i++) {
            float angle = (rand() % 360) * 3.14159f / 180.0f;
            float speed = (rand() % 45) + 15.0f; // 初始爆炸初速极快
            Particle p;
            p.x = cx; p.y = cy;
            p.vx = cos(angle) * speed;
            p.vy = sin(angle) * speed;
            p.life = 255; // 粒子生命周期(同时作为透明度)
            particles.push_back(p);
        }
    }

    // ------------------------------------------------------------------
    // 阶段3：爆炸光效与文字渲染阶段 (第 35 帧之后)
    // ------------------------------------------------------------------
    if (tick >= 35) {
        int expTick = tick - 35;
        
        // 1. 轨道激光柱 (持续 15 帧后消失)
        if (expTick < 15) {
            int laserAlpha = max(0, 255 - expTick * 17);
            SolidBrush laserBrush(Color(laserAlpha, 255, 100, 100)); // 外层红色激光
            g.FillRectangle(&laserBrush, (int)(cx - 40), 0, 80, (int)cy);
            
            SolidBrush coreBrush(Color(laserAlpha, 255, 255, 255));  // 内层高亮白核
            g.FillRectangle(&coreBrush, (int)(cx - 15), 0, 30, (int)cy);
        }

        // 2. 冲击波光环 (迅速扩散)
        int waveRadius = expTick * 60;
        int waveAlpha = max(0, 255 - expTick * 6);
        Pen wavePen(Color(waveAlpha, 255, 150, 0), 20.0f);
        g.DrawEllipse(&wavePen, cx - waveRadius, cy - waveRadius, waveRadius * 2, waveRadius * 2);

        // 3. 爆炸核心火球
        int fireballRadius = max(0, 250 - expTick * 6);
        SolidBrush fireballBrush(Color(waveAlpha, 255, 80, 0));
        g.FillEllipse(&fireballBrush, cx - fireballRadius, cy - fireballRadius, fireballRadius * 2, fireballRadius * 2);

        // 4. 计算并绘制粒子飞溅系统 (带物理减速)
        for (auto& p : particles) {
            if (p.life > 0) {
                p.x += p.vx;
                p.y += p.vy;
                p.vx *= 0.88f; // 空气阻力：逐帧减慢速度
                p.vy *= 0.88f;
                p.life -= 6;   // 亮度/生命值衰减
                
                int alpha = max(0, p.life);
                SolidBrush pBrush(Color(alpha, 255, 200, 50)); // 金色火花
                g.FillEllipse(&pBrush, p.x - 5, p.y - 5, 10.0f, 10.0f);
            }
        }

        // 5. 爆出霸气中文字幕："滚吧，垃圾文件！"
        SetBkMode(hdcMem, TRANSPARENT);
        int textAlpha = waveAlpha; // 文字和冲击波一起逐渐消失
        SetTextColor(hdcMem, RGB(255, 220, 50)); 
        HFONT hFont = CreateFontW(72, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                  DEFAULT_PITCH, L"Microsoft YaHei");
        SelectObject(hdcMem, hFont);
        RECT textRect = { 0, cy + 100, width, height };
        DrawTextW(hdcMem, TEXT_BOOM, -1, &textRect, DT_CENTER | DT_TOP | DT_SINGLELINE);
        DeleteObject(hFont);
    }

    // 最终：将双缓冲内存图像直接拷贝到屏幕，彻底杜绝闪烁
    BitBlt(hdcBase, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

// =========================================================================================
// 【主窗口过程：拦截消息，驱动动画】
// =========================================================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: 
            // 设定窗口为 215/255 的半透明度，实现“蒙上一层暗影”的效果
            SetLayeredWindowAttributes(hwnd, 0, 215, LWA_ALPHA); 
            // 开启定时器驱动动画，每 30 毫秒刷新一帧 (约等于 33 FPS)
            SetTimer(hwnd, 1, 30, NULL); 
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0); // 允许按 ESC 强行退出
            break;

        case WM_TIMER:
            tick++;
            if (tick > ANIM_TOTAL_TICKS) {
                KillTimer(hwnd, 1);
                PostQuitMessage(0); // 动画播放完毕自动退出
            } else {
                InvalidateRect(hwnd, NULL, FALSE); // 触发重绘
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RenderFrame(hwnd, hdc); // 交给渲染引擎
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

    // 解析系统通过右键菜单传进来的参数 (即文件路径)
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) {
        targetFile = argv[1];
        
        // 字符串处理：从完整路径中提取纯文件名，用于动画展示
        size_t pos = targetFile.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            fileName = targetFile.substr(pos + 1);
        } else {
            fileName = targetFile;
        }
    }
    LocalFree(argv);

    // 如果没有路径参数，说明用户直接双击了 EXE -> 调出安装/卸载界面
    if (targetFile.empty()) {
        int msgboxID = MessageBoxW(NULL, MSG_PROMPT, TITLE_NOTICE, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (msgboxID == IDYES) RegisterContextMenu();
        else if (msgboxID == IDNO) UnregisterContextMenu();
        return 0;
    }

    // 初始化 GDI+ 画图引擎
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MonsterDeleterClass";
    wc.hCursor = LoadCursor(NULL, IDC_CROSS); // 鼠标变成十字狙击光标
    RegisterClassW(&wc);

    // 动态获取当前所有显示器合并后的虚拟屏幕尺寸和起点 (完美支持多屏联动)
    vScreenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    vScreenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    vScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    vScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // 创建置顶的无边框透明窗口，盖住所有屏幕
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"MonsterDeleterClass", L"Monster Deleter",
        WS_POPUP | WS_VISIBLE,
        vScreenX, vScreenY, vScreenWidth, vScreenHeight,
        NULL, NULL, hInstance, NULL
    );

    // 经典 Windows 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 释放资源
    GdiplusShutdown(gdiplusToken);
    return 0;
}
