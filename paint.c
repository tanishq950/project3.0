#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <wchar.h>
#include <stdint.h>
#include <string.h>
#include <windowsx.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

#define APP_NAME L"CG Paint Studio - C Edition"
#define MAX_HISTORY 30
#define TOOLBAR_Y 118
#define CANVAS_X 24
#define CANVAS_Y 244
#define CANVAS_MARGIN 24

typedef enum {
    TOOL_PENCIL, TOOL_BRUSH, TOOL_FILL, TOOL_ERASER, TOOL_TEXT,
    TOOL_LINE, TOOL_RECT, TOOL_ELLIPSE, TOOL_TRIANGLE, TOOL_SELECT,
    TOOL_PICKER, TOOL_ZOOM
} Tool;

static HWND g_window;
static HBITMAP g_canvas;
static int g_canvasW, g_canvasH;
static Tool g_tool = TOOL_PENCIL;
static COLORREF g_color = RGB(35, 55, 90);
static int g_brush = 5;
static BOOL g_fill = FALSE;
static float g_zoom = 1.0f;
static BOOL g_selectionActive = FALSE;
static RECT g_selection;
static BOOL g_drawing = FALSE;
static POINT g_start, g_last;
static HBITMAP g_preview;
static HBITMAP g_undo[MAX_HISTORY];
static HBITMAP g_redo[MAX_HISTORY];
static int g_undoCount, g_redoCount;

static HBITMAP CreateBitmapCanvas(int w, int h) {
    HDC screen = GetDC(NULL), dc;
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
    dc = CreateCompatibleDC(screen);
    SelectObject(dc, bitmap);
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    RECT r = { 0, 0, w, h };
    FillRect(dc, &r, white);
    DeleteObject(white);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    return bitmap;
}

static HBITMAP CopyBitmap(HBITMAP source, int w, int h) {
    HDC screen = GetDC(NULL), src = CreateCompatibleDC(screen), dst = CreateCompatibleDC(screen);
    HBITMAP copy = CreateCompatibleBitmap(screen, w, h);
    SelectObject(src, source);
    SelectObject(dst, copy);
    BitBlt(dst, 0, 0, w, h, src, 0, 0, SRCCOPY);
    DeleteDC(src);
    DeleteDC(dst);
    ReleaseDC(NULL, screen);
    return copy;
}

static void ClearHistory(HBITMAP *history, int *count) {
    while (*count > 0) DeleteObject(history[--(*count)]);
}

static void SaveUndoState(void) {
    if (!g_canvas) return;
    if (g_undoCount == MAX_HISTORY) {
        DeleteObject(g_undo[0]);
        MoveMemory(&g_undo[0], &g_undo[1], sizeof(HBITMAP) * (MAX_HISTORY - 1));
        g_undoCount--;
    }
    g_undo[g_undoCount++] = CopyBitmap(g_canvas, g_canvasW, g_canvasH);
    ClearHistory(g_redo, &g_redoCount);
}

static HDC CanvasDC(void) {
    HDC dc = CreateCompatibleDC(NULL);
    SelectObject(dc, g_canvas);
    return dc;
}

static void DrawShape(HDC dc, POINT a, POINT b, BOOL preview) {
    HPEN pen = CreatePen(PS_SOLID, g_brush, preview ? RGB(120, 120, 120) : g_color);
    HBRUSH brush = g_fill ? CreateSolidBrush(g_color) : (HBRUSH)GetStockObject(HOLLOW_BRUSH);
    HPEN oldPen = SelectObject(dc, pen);
    HBRUSH oldBrush = SelectObject(dc, brush);
    int left = a.x < b.x ? a.x : b.x, right = a.x > b.x ? a.x : b.x;
    int top = a.y < b.y ? a.y : b.y, bottom = a.y > b.y ? a.y : b.y;
    if (g_tool == TOOL_LINE) {
        MoveToEx(dc, a.x, a.y, NULL); LineTo(dc, b.x, b.y);
    } else if (g_tool == TOOL_RECT) {
        Rectangle(dc, left, top, right, bottom);
    } else if (g_tool == TOOL_ELLIPSE) {
        Ellipse(dc, left, top, right, bottom);
    } else if (g_tool == TOOL_TRIANGLE) {
        POINT p[3] = { { (left + right) / 2, top }, { right, bottom }, { left, bottom } };
        Polygon(dc, p, 3);
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    if (g_fill) DeleteObject(brush);
}

static void DrawFreehand(HDC dc, POINT a, POINT b, BOOL erase) {
    COLORREF color = erase ? RGB(255, 255, 255) : g_color;
    int width = g_tool == TOOL_BRUSH ? g_brush * 2 : g_brush;
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN old = SelectObject(dc, pen);
    MoveToEx(dc, a.x, a.y, NULL);
    LineTo(dc, b.x, b.y);
    SelectObject(dc, old);
    DeleteObject(pen);
    if (width > 1) {
        HBRUSH brush = CreateSolidBrush(color);
        HBRUSH oldBrush = SelectObject(dc, brush);
        Ellipse(dc, b.x - width / 2, b.y - width / 2, b.x + width / 2, b.y + width / 2);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
    }
}

static void DrawToolbarButton(HDC dc, int x, int y, int w, int h, const wchar_t *label, BOOL selected) {
    HBRUSH brush = CreateSolidBrush(selected ? RGB(42, 103, 230) : RGB(242, 246, 250));
    RECT r = { x, y, x + w, y + h };
    FillRect(dc, &r, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? RGB(255, 255, 255) : RGB(25, 42, 70));
    DrawTextW(dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawUI(HDC dc, RECT client) {
    HBRUSH bg = CreateSolidBrush(RGB(245, 248, 252));
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(35, 55, 90));
    HFONT title = CreateFontW(42, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
    HFONT oldFont = SelectObject(dc, title);
    TextOutW(dc, 24, 24, L"CG Paint Studio", 15);
    SelectObject(dc, oldFont);
    DeleteObject(title);
    HFONT toolbarFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
    oldFont = SelectObject(dc, toolbarFont);
    SetTextColor(dc, RGB(42, 103, 230));
    TextOutW(dc, 26, 82, L"COMPUTER GRAPHICS PROJECT", 25);
    SetTextColor(dc, RGB(70, 85, 110));
    TextOutW(dc, 28, 132, L"Draw", 4);
    DrawToolbarButton(dc, 80, 126, 44, 34, L"Pencil", g_tool == TOOL_PENCIL);
    DrawToolbarButton(dc, 132, 126, 44, 34, L"Brush", g_tool == TOOL_BRUSH);
    DrawToolbarButton(dc, 184, 126, 44, 34, L"Fill", g_tool == TOOL_FILL);
    DrawToolbarButton(dc, 236, 126, 44, 34, L"Eraser", g_tool == TOOL_ERASER);
    DrawToolbarButton(dc, 288, 126, 44, 34, L"Text", g_tool == TOOL_TEXT);
    DrawToolbarButton(dc, 340, 126, 44, 34, L"Line", g_tool == TOOL_LINE);
    DrawToolbarButton(dc, 392, 126, 44, 34, L"Rect", g_tool == TOOL_RECT);
    DrawToolbarButton(dc, 444, 126, 44, 34, L"Ellipse", g_tool == TOOL_ELLIPSE);
    DrawToolbarButton(dc, 496, 126, 54, 34, L"Polygon", g_tool == TOOL_TRIANGLE);
    SetTextColor(dc, RGB(70, 85, 110));
    TextOutW(dc, 570, 132, L"Color", 5);
    COLORREF colors[] = { RGB(35,55,90), RGB(20,30,45), RGB(235,70,75), RGB(245,155,20), RGB(30,190,100), RGB(45,105,230), RGB(160,80,230), RGB(255,255,255) };
    for (int i = 0; i < 8; ++i) {
        HBRUSH cb = CreateSolidBrush(colors[i]);
        RECT cr = { 624 + i * 32, 130, 648 + i * 32, 154 };
        FillRect(dc, &cr, cb);
        DeleteObject(cb);
        if (colors[i] == g_color) {
            HPEN p = CreatePen(PS_SOLID, 2, RGB(42, 103, 230));
            HPEN op = SelectObject(dc, p);
            HBRUSH oh = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, cr.left - 2, cr.top - 2, cr.right + 2, cr.bottom + 2);
            SelectObject(dc, oh); SelectObject(dc, op); DeleteObject(p);
        }
    }
    TextOutW(dc, 894, 132, L"Size", 4);
    wchar_t sizeText[32];
    swprintf_s(sizeText, 32, L"%d px", g_brush);
    TextOutW(dc, 932, 132, sizeText, (int)wcslen(sizeText));
    RECT track = { 979, 140, 1085, 148 };
    HBRUSH tb = CreateSolidBrush(RGB(210, 214, 220)); FillRect(dc, &track, tb); DeleteObject(tb);
    HBRUSH knob = CreateSolidBrush(RGB(42, 103, 230));
    RECT kr = { 979 + g_brush * 4 - 10, 134, 979 + g_brush * 4 + 6, 154 };
    FillRect(dc, &kr, knob); DeleteObject(knob);
    SetTextColor(dc, RGB(70, 85, 110));
    TextOutW(dc, 1110, 132, g_fill ? L"[x] Fill shapes" : L"[ ] Fill shapes", 15);
    DrawToolbarButton(dc, 1240, 126, 74, 34, L"Undo", FALSE);
    DrawToolbarButton(dc, 1320, 126, 74, 34, L"Redo", FALSE);
    DrawToolbarButton(dc, 1400, 126, 74, 34, L"Clear", FALSE);
    DrawToolbarButton(dc, 1480, 126, 104, 34, L"Save PNG", TRUE);
    TextOutW(dc, 28, 180, L"Edit", 4);
    DrawToolbarButton(dc, 80, 174, 72, 34, L"Select", g_tool == TOOL_SELECT);
    DrawToolbarButton(dc, 160, 174, 72, 34, L"Picker", g_tool == TOOL_PICKER);
    DrawToolbarButton(dc, 240, 174, 72, 34, L"Zoom +", FALSE);
    DrawToolbarButton(dc, 320, 174, 72, 34, L"Zoom -", FALSE);
    DrawToolbarButton(dc, 400, 174, 72, 34, L"Flip H", FALSE);
    DrawToolbarButton(dc, 480, 174, 72, 34, L"Flip V", FALSE);
    DrawToolbarButton(dc, 560, 174, 72, 34, L"Rotate", FALSE);
    DrawToolbarButton(dc, 640, 174, 72, 34, L"Resize", FALSE);
    DrawToolbarButton(dc, 720, 174, 72, 34, L"Skew", FALSE);
    SetTextColor(dc, RGB(70, 85, 110));
    wchar_t zoomText[32]; swprintf_s(zoomText, 32, L"%d%%", (int)(g_zoom * 100));
    TextOutW(dc, 820, 182, zoomText, (int)wcslen(zoomText));
    SelectObject(dc, oldFont);
    DeleteObject(toolbarFont);
    HPEN border = CreatePen(PS_SOLID, 1, RGB(220, 228, 238));
    HPEN op = SelectObject(dc, border);
    HBRUSH oh = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, CANVAS_X - 1, CANVAS_Y - 1, client.right - CANVAS_MARGIN + 1, client.bottom - 24 + 1);
    if (g_selectionActive) {
        HPEN selectPen = CreatePen(PS_DASH, 1, RGB(40, 100, 230));
        HPEN old = SelectObject(dc, selectPen);
        HBRUSH hollow = (HBRUSH)SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, CANVAS_X + g_selection.left, CANVAS_Y + g_selection.top,
            CANVAS_X + g_selection.right, CANVAS_Y + g_selection.bottom);
        SelectObject(dc, hollow); SelectObject(dc, old); DeleteObject(selectPen);
    }
    SelectObject(dc, oh); SelectObject(dc, op); DeleteObject(border);
}

static void RestorePreview(void) {
    if (g_preview && g_canvas) {
        HDC dst = CanvasDC(), src = CreateCompatibleDC(NULL);
        SelectObject(src, g_preview);
        BitBlt(dst, 0, 0, g_canvasW, g_canvasH, src, 0, 0, SRCCOPY);
        DeleteDC(src); DeleteDC(dst);
    }
}

static void PaintToCanvas(POINT p) {
    HDC dc = CanvasDC();
    if (g_tool == TOOL_PENCIL || g_tool == TOOL_BRUSH || g_tool == TOOL_ERASER) DrawFreehand(dc, g_last, p, g_tool == TOOL_ERASER);
    else DrawShape(dc, g_start, p, FALSE);
    DeleteDC(dc);
}

static void FillCanvasArea(POINT start) {
    if (start.x < 0 || start.y < 0 || start.x >= g_canvasW || start.y >= g_canvasH) return;
    HDC dc = CanvasDC();
    COLORREF target = GetPixel(dc, start.x, start.y);
    if (target == g_color) { DeleteDC(dc); return; }
    POINT *queue = (POINT *)HeapAlloc(GetProcessHeap(), 0, (size_t)g_canvasW * g_canvasH * sizeof(POINT));
    unsigned char *seen = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)g_canvasW * g_canvasH);
    if (!queue || !seen) { HeapFree(GetProcessHeap(), 0, queue); HeapFree(GetProcessHeap(), 0, seen); DeleteDC(dc); return; }
    size_t head = 0, tail = 0; queue[tail++] = start;
    while (head < tail) {
        POINT p = queue[head++]; size_t index = (size_t)p.y * g_canvasW + p.x;
        if (seen[index] || GetPixel(dc, p.x, p.y) != target) continue;
        seen[index] = 1; SetPixel(dc, p.x, p.y, g_color);
        if (p.x > 0) queue[tail++] = (POINT){ p.x - 1, p.y };
        if (p.x + 1 < g_canvasW) queue[tail++] = (POINT){ p.x + 1, p.y };
        if (p.y > 0) queue[tail++] = (POINT){ p.x, p.y - 1 };
        if (p.y + 1 < g_canvasH) queue[tail++] = (POINT){ p.x, p.y + 1 };
    }
    HeapFree(GetProcessHeap(), 0, queue); HeapFree(GetProcessHeap(), 0, seen); DeleteDC(dc);
}

static void TransformCanvas(int mode) {
    int nw = (mode == 2) ? g_canvasH : g_canvasW, nh = (mode == 2) ? g_canvasW : g_canvasH;
    HBITMAP next = CreateBitmapCanvas(nw, nh);
    HDC src = CreateCompatibleDC(NULL), dst = CreateCompatibleDC(NULL);
    SelectObject(src, g_canvas); SelectObject(dst, next);
    for (int y = 0; y < g_canvasH; ++y) for (int x = 0; x < g_canvasW; ++x) {
        int nx = x, ny = y;
        if (mode == 0) nx = g_canvasW - 1 - x;
        else if (mode == 1) ny = g_canvasH - 1 - y;
        else { nx = g_canvasH - 1 - y; ny = x; }
        COLORREF pixel = GetPixel(src, x, y); SetPixel(dst, nx, ny, pixel);
    }
    DeleteDC(src); DeleteDC(dst); DeleteObject(g_canvas); g_canvas = next; g_canvasW = nw; g_canvasH = nh;
}

static void ResizeOrSkewCanvas(BOOL skew) {
    int nw = g_canvasW, nh = g_canvasH;
    if (skew) { nw += g_canvasH / 5; }
    else { nw = (int)(g_canvasW * 1.25f); nh = (int)(g_canvasH * 1.25f); }
    HBITMAP next = CreateBitmapCanvas(nw, nh);
    HDC src = CreateCompatibleDC(NULL), dst = CreateCompatibleDC(NULL);
    SelectObject(src, g_canvas); SelectObject(dst, next);
    SetStretchBltMode(dst, HALFTONE);
    StretchBlt(dst, 0, 0, nw, nh, src, 0, 0, g_canvasW, g_canvasH, SRCCOPY);
    DeleteDC(src); DeleteDC(dst); DeleteObject(g_canvas); g_canvas = next; g_canvasW = nw; g_canvasH = nh;
}

static void DeleteSelection(void) {
    if (!g_selectionActive) return;
    HDC dc = CanvasDC(); HBRUSH white = CreateSolidBrush(RGB(255,255,255));
    FillRect(dc, &g_selection, white); DeleteObject(white); DeleteDC(dc); g_selectionActive = FALSE;
}

static void FinishText(POINT p) {
    if (MessageBoxW(g_window, L"The text tool places the word \"Text\" at the selected point.", L"Text tool", MB_OKCANCEL) != IDOK) return;
    HDC dc = CanvasDC();
    HFONT font = CreateFontW(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
    HFONT old = SelectObject(dc, font);
    SetTextColor(dc, g_color); SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, p.x, p.y, L"Text", 4);
    SelectObject(dc, old); DeleteObject(font); DeleteDC(dc);
}

static uint32_t PngCrc(const unsigned char *data, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (-(int)(crc & 1)));
    }
    return ~crc;
}

static void WriteBE32(FILE *file, uint32_t value) {
    unsigned char b[4] = { (unsigned char)(value >> 24), (unsigned char)(value >> 16), (unsigned char)(value >> 8), (unsigned char)value };
    fwrite(b, 1, 4, file);
}

static BOOL SavePNG(const wchar_t *path) {
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_canvasW;
    bi.bmiHeader.biHeight = -g_canvasH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    size_t rgbaSize = (size_t)g_canvasW * g_canvasH * 4;
    unsigned char *pixels = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, rgbaSize);
    unsigned char *scan = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)(g_canvasW * 4 + 1) * g_canvasH);
    if (!pixels || !scan) { HeapFree(GetProcessHeap(), 0, pixels); HeapFree(GetProcessHeap(), 0, scan); return FALSE; }
    HDC dc = CreateCompatibleDC(NULL); SelectObject(dc, g_canvas);
    BOOL readOk = GetDIBits(dc, g_canvas, 0, g_canvasH, pixels, &bi, DIB_RGB_COLORS) != 0;
    DeleteDC(dc);
    if (!readOk) { HeapFree(GetProcessHeap(), 0, pixels); HeapFree(GetProcessHeap(), 0, scan); return FALSE; }
    for (int y = 0; y < g_canvasH; ++y) {
        scan[y * (g_canvasW * 4 + 1)] = 0;
        for (int x = 0; x < g_canvasW; ++x) {
            unsigned char *src = pixels + ((size_t)y * g_canvasW + x) * 4;
            unsigned char *dst = scan + (size_t)y * (g_canvasW * 4 + 1) + 1 + x * 4;
            dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255;
        }
    }
    FILE *file = _wfopen(path, L"wb");
    if (!file) { HeapFree(GetProcessHeap(), 0, pixels); HeapFree(GetProcessHeap(), 0, scan); return FALSE; }
    static const unsigned char signature[8] = { 137,80,78,71,13,10,26,10 };
    fwrite(signature, 1, 8, file);
    unsigned char ihdr[13] = { 0 };
    ihdr[0] = (unsigned char)(g_canvasW >> 24); ihdr[1] = (unsigned char)(g_canvasW >> 16); ihdr[2] = (unsigned char)(g_canvasW >> 8); ihdr[3] = (unsigned char)g_canvasW;
    ihdr[4] = (unsigned char)(g_canvasH >> 24); ihdr[5] = (unsigned char)(g_canvasH >> 16); ihdr[6] = (unsigned char)(g_canvasH >> 8); ihdr[7] = (unsigned char)g_canvasH;
    ihdr[8] = 8; ihdr[9] = 6;
    WriteBE32(file, 13); fwrite("IHDR", 1, 4, file); fwrite(ihdr, 1, 13, file); WriteBE32(file, PngCrc((unsigned char *)"IHDR", 4)); /* overwritten below */
    fseek(file, -4, SEEK_CUR);
    uint32_t ihdrCrc = PngCrc(ihdr, 13); ihdrCrc = PngCrc((unsigned char *)"IHDR", 4) ^ ihdrCrc;
    (void)ihdrCrc;
    fseek(file, 0, SEEK_END);
    /* Rewrite the IHDR chunk cleanly after the compact header write. */
    fclose(file);
    file = _wfopen(path, L"wb");
    if (!file) { HeapFree(GetProcessHeap(), 0, pixels); HeapFree(GetProcessHeap(), 0, scan); return FALSE; }
    fwrite(signature, 1, 8, file); WriteBE32(file, 13); fwrite("IHDR", 1, 4, file); fwrite(ihdr, 1, 13, file);
    unsigned char ihdrData[17]; memcpy(ihdrData, "IHDR", 4); memcpy(ihdrData + 4, ihdr, 13); WriteBE32(file, PngCrc(ihdrData, 17));
    size_t rawSize = (size_t)(g_canvasW * 4 + 1) * g_canvasH;
    size_t blocks = (rawSize + 65534) / 65535, zsize = 2 + rawSize + blocks * 5 + 4;
    unsigned char *z = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, zsize), *zp = z;
    *zp++ = 0x78; *zp++ = 0x01;
    size_t pos = 0;
    while (pos < rawSize) {
        unsigned int n = (unsigned int)((rawSize - pos) > 65535 ? 65535 : rawSize - pos);
        *zp++ = (pos + n == rawSize) ? 1 : 0; *zp++ = (unsigned char)n; *zp++ = (unsigned char)(n >> 8);
        unsigned int inv = 65535 - n; *zp++ = (unsigned char)inv; *zp++ = (unsigned char)(inv >> 8);
        memcpy(zp, scan + pos, n); zp += n; pos += n;
    }
    uint32_t adler = 1; for (size_t i = 0; i < rawSize; ++i) { adler = ((adler & 0xffff) + scan[i]) % 65521; adler = (((adler >> 16) + (adler & 0xffff)) << 16) | (adler & 0xffff); }
    uint32_t s1 = 1, s2 = 0; for (size_t i = 0; i < rawSize; ++i) { s1 = (s1 + scan[i]) % 65521; s2 = (s2 + s1) % 65521; } adler = (s2 << 16) | s1;
    *zp++ = (unsigned char)(adler >> 24); *zp++ = (unsigned char)(adler >> 16); *zp++ = (unsigned char)(adler >> 8); *zp++ = (unsigned char)adler;
    size_t dataLen = (size_t)(zp - z); WriteBE32(file, (uint32_t)dataLen); fwrite("IDAT", 1, 4, file); fwrite(z, 1, dataLen, file);
    unsigned char *crcData = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, dataLen + 4); memcpy(crcData, "IDAT", 4); memcpy(crcData + 4, z, dataLen); WriteBE32(file, PngCrc(crcData, dataLen + 4)); HeapFree(GetProcessHeap(), 0, crcData);
    WriteBE32(file, 0); fwrite("IEND", 1, 4, file); WriteBE32(file, PngCrc((unsigned char *)"IEND", 4)); fclose(file);
    HeapFree(GetProcessHeap(), 0, z); HeapFree(GetProcessHeap(), 0, pixels); HeapFree(GetProcessHeap(), 0, scan); return TRUE;
}

static void ResizeCanvas(HWND hwnd) {
    RECT r; GetClientRect(hwnd, &r);
    int w = r.right - CANVAS_X - CANVAS_MARGIN, h = r.bottom - CANVAS_Y - 24;
    if (w < 200 || h < 100) return;
    HBITMAP next = CreateBitmapCanvas(w, h);
    if (g_canvas) {
        HDC src = CreateCompatibleDC(NULL), dst = CreateCompatibleDC(NULL);
        SelectObject(src, g_canvas); SelectObject(dst, next);
        BitBlt(dst, 0, 0, g_canvasW < w ? g_canvasW : w, g_canvasH < h ? g_canvasH : h, src, 0, 0, SRCCOPY);
        DeleteDC(src); DeleteDC(dst); DeleteObject(g_canvas);
    }
    g_canvas = next; g_canvasW = w; g_canvasH = h;
}

static void HitTestToolbar(POINT p) {
    if (p.y >= 120 && p.y < 170) {
        if (p.x >= 80 && p.x < 124) g_tool = TOOL_PENCIL;
        else if (p.x >= 132 && p.x < 176) g_tool = TOOL_BRUSH;
        else if (p.x >= 184 && p.x < 228) g_tool = TOOL_FILL;
        else if (p.x >= 236 && p.x < 280) g_tool = TOOL_ERASER;
        else if (p.x >= 288 && p.x < 332) g_tool = TOOL_TEXT;
        else if (p.x >= 340 && p.x < 384) g_tool = TOOL_LINE;
        else if (p.x >= 392 && p.x < 436) g_tool = TOOL_RECT;
        else if (p.x >= 444 && p.x < 488) g_tool = TOOL_ELLIPSE;
        else if (p.x >= 496 && p.x < 550) g_tool = TOOL_TRIANGLE;
        else if (p.x >= 624 && p.x < 880) g_color = (COLORREF[]){ RGB(35,55,90), RGB(20,30,45), RGB(235,70,75), RGB(245,155,20), RGB(30,190,100), RGB(45,105,230), RGB(160,80,230), RGB(255,255,255) }[(p.x - 624) / 32];
        else if (p.x >= 979 && p.x < 1088) { g_brush = (p.x - 979) / 4; if (g_brush < 1) g_brush = 1; if (g_brush > 28) g_brush = 28; }
        else if (p.x >= 1110 && p.x < 1230) g_fill = !g_fill;
    } else if (p.y >= 170 && p.y < 220) {
        if (p.x >= 80 && p.x < 152) g_tool = TOOL_SELECT;
        else if (p.x >= 160 && p.x < 232) g_tool = TOOL_PICKER;
        else if (p.x >= 240 && p.x < 312) g_zoom = g_zoom < 2.0f ? g_zoom + 0.25f : g_zoom;
        else if (p.x >= 320 && p.x < 392) g_zoom = g_zoom > 0.5f ? g_zoom - 0.25f : g_zoom;
        else if (p.x >= 400 && p.x < 472) { SaveUndoState(); TransformCanvas(0); }
        else if (p.x >= 480 && p.x < 552) { SaveUndoState(); TransformCanvas(1); }
        else if (p.x >= 560 && p.x < 632) { SaveUndoState(); TransformCanvas(2); }
        else if (p.x >= 640 && p.x < 712) { SaveUndoState(); ResizeOrSkewCanvas(FALSE); }
        else if (p.x >= 720 && p.x < 792) { SaveUndoState(); ResizeOrSkewCanvas(TRUE); }
    } else if (p.y < 170) return;
    if (p.y >= 120 && p.y < 170 && p.x >= 1240 && p.x < 1314 && g_undoCount) {
        g_redo[g_redoCount++] = CopyBitmap(g_canvas, g_canvasW, g_canvasH);
        DeleteObject(g_canvas); g_canvas = g_undo[--g_undoCount];
    } else if (p.y >= 120 && p.y < 170 && p.x >= 1320 && p.x < 1394 && g_redoCount) {
        g_undo[g_undoCount++] = CopyBitmap(g_canvas, g_canvasW, g_canvasH);
        DeleteObject(g_canvas); g_canvas = g_redo[--g_redoCount];
    } else if (p.y >= 120 && p.y < 170 && p.x >= 1400 && p.x < 1474) {
        SaveUndoState(); HDC dc = CanvasDC(); RECT r = { 0,0,g_canvasW,g_canvasH }; HBRUSH b = CreateSolidBrush(RGB(255,255,255)); FillRect(dc,&r,b); DeleteObject(b); DeleteDC(dc);
    } else if (p.y >= 120 && p.y < 170 && p.x >= 1480 && p.x < 1584) {
        OPENFILENAMEW ofn = { sizeof(ofn) }; wchar_t path[MAX_PATH] = L"painting.png";
        ofn.hwndOwner = g_window; ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0"; ofn.lpstrFile = path; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_OVERWRITEPROMPT;
        if (GetSaveFileNameW(&ofn)) SavePNG(path);
    }
    InvalidateRect(g_window, NULL, FALSE);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: ResizeCanvas(hwnd); return 0;
    case WM_SIZE: ResizeCanvas(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (p.y < CANVAS_Y) { HitTestToolbar(p); return 0; }
        if (!g_canvas || p.x < CANVAS_X || p.y < CANVAS_Y) return 0;
        p.x = (int)((p.x - CANVAS_X) / g_zoom); p.y = (int)((p.y - CANVAS_Y) / g_zoom);
        if (p.x >= g_canvasW || p.y >= g_canvasH) return 0;
        g_start = g_last = p; g_drawing = TRUE; SaveUndoState();
        if (g_preview) DeleteObject(g_preview); g_preview = CopyBitmap(g_canvas, g_canvasW, g_canvasH);
        if (g_tool == TOOL_TEXT) { g_drawing = FALSE; FinishText(p); }
        else if (g_tool == TOOL_FILL) { g_drawing = FALSE; FillCanvasArea(p); }
        else if (g_tool == TOOL_PICKER) {
            HDC dc = CanvasDC(); g_color = GetPixel(dc, p.x, p.y); DeleteDC(dc); g_drawing = FALSE;
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_drawing && g_canvas) {
            POINT p = { (int)((GET_X_LPARAM(lp) - CANVAS_X) / g_zoom), (int)((GET_Y_LPARAM(lp) - CANVAS_Y) / g_zoom) };
            if (p.x < 0 || p.y < 0 || p.x >= g_canvasW || p.y >= g_canvasH) return 0;
            if (g_tool == TOOL_PENCIL || g_tool == TOOL_BRUSH || g_tool == TOOL_ERASER) PaintToCanvas(p);
            else if (g_tool == TOOL_SELECT) {
                RestorePreview(); g_selection.left = g_start.x < p.x ? g_start.x : p.x; g_selection.right = g_start.x > p.x ? g_start.x : p.x;
                g_selection.top = g_start.y < p.y ? g_start.y : p.y; g_selection.bottom = g_start.y > p.y ? g_start.y : p.y; g_selectionActive = TRUE;
            } else { RestorePreview(); HDC dc = CanvasDC(); DrawShape(dc, g_start, p, TRUE); DeleteDC(dc); }
            g_last = p; InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_drawing) { if (g_tool != TOOL_PENCIL && g_tool != TOOL_BRUSH && g_tool != TOOL_ERASER && g_tool != TOOL_SELECT) { RestorePreview(); PaintToCanvas(g_last); } g_drawing = FALSE; }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_DELETE && g_selectionActive) { SaveUndoState(); DeleteSelection(); InvalidateRect(hwnd, NULL, FALSE); return 0; }
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (wp == 'Z' && g_undoCount) {
                g_redo[g_redoCount++] = CopyBitmap(g_canvas, g_canvasW, g_canvasH);
                DeleteObject(g_canvas); g_canvas = g_undo[--g_undoCount]; InvalidateRect(hwnd, NULL, FALSE); return 0;
            }
            if (wp == 'Y' && g_redoCount) {
                g_undo[g_undoCount++] = CopyBitmap(g_canvas, g_canvasW, g_canvasH);
                DeleteObject(g_canvas); g_canvas = g_redo[--g_redoCount]; InvalidateRect(hwnd, NULL, FALSE); return 0;
            }
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT r; GetClientRect(hwnd, &r); DrawUI(dc, r);
        if (g_canvas) { HDC src = CreateCompatibleDC(NULL); SelectObject(src, g_canvas); SetStretchBltMode(dc, HALFTONE); StretchBlt(dc, CANVAS_X, CANVAS_Y, (int)(g_canvasW * g_zoom), (int)(g_canvasH * g_zoom), src, 0, 0, g_canvasW, g_canvasH, SRCCOPY); DeleteDC(src); }
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_DESTROY:
        if (g_canvas) DeleteObject(g_canvas); ClearHistory(g_undo, &g_undoCount); ClearHistory(g_redo, &g_redoCount);
        if (g_preview) DeleteObject(g_preview); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE unused, PWSTR command, int show) {
    (void)unused; (void)command;
    WNDCLASSW wc = { 0 };
    wc.hInstance = instance; wc.lpfnWndProc = WindowProc; wc.lpszClassName = L"CGPaintStudioC";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    g_window = CreateWindowExW(0, wc.lpszClassName, APP_NAME, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1600, 900, NULL, NULL, instance, NULL);
    ShowWindow(g_window, show);
    MSG msg; while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
