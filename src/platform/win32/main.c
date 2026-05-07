#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "nesemu.h"

#define WINDOW_CLASS_NAME L"NESEMUWindow"
#define WINDOW_TITLE      L"NESEMU"

typedef struct AppState {
    NesEmu nes;
    WCHAR rom_path[MAX_PATH];
    WCHAR status[512];
} AppState;

static AppState g_app;

static void app_set_status(const WCHAR *message)
{
    if (message == NULL) {
        g_app.status[0] = L'\0';
        return;
    }
    wcsncpy(g_app.status, message, (sizeof(g_app.status) / sizeof(g_app.status[0])) - 1u);
    g_app.status[(sizeof(g_app.status) / sizeof(g_app.status[0])) - 1u] = L'\0';
}

static const WCHAR *mirroring_name(NesMirroring mirroring)
{
    switch (mirroring) {
    case NES_MIRROR_HORIZONTAL:
        return L"horizontal";
    case NES_MIRROR_VERTICAL:
        return L"vertical";
    case NES_MIRROR_FOUR_SCREEN:
        return L"four-screen";
    default:
        return L"unknown";
    }
}

static void format_loaded_status(const WCHAR *path)
{
    const WCHAR *name = path;
    const WCHAR *slash1 = wcsrchr(path, L'\\');
    const WCHAR *slash2 = wcsrchr(path, L'/');
    const WCHAR *slash = slash1;

    if (slash2 != NULL && (slash == NULL || slash2 > slash)) {
        slash = slash2;
    }
    if (slash != NULL) {
        name = slash + 1;
    }
    swprintf(g_app.status,
             sizeof(g_app.status) / sizeof(g_app.status[0]),
             L"Loaded: %ls\nMapper: %u  PRG: %u x 16KB  CHR: %u x 8KB  Mirror: %ls\nReset vector: $%04X",
             name,
             (unsigned int)g_app.nes.rom.mapper_id,
             (unsigned int)g_app.nes.rom.prg_banks,
             (unsigned int)g_app.nes.rom.chr_banks,
             mirroring_name(g_app.nes.rom.mirroring),
             (unsigned int)g_app.nes.reset_vector);
}

static int read_entire_file_w(const WCHAR *path, uint8_t **out_data, size_t *out_size)
{
    HANDLE file;
    LARGE_INTEGER file_size;
    DWORD bytes_read;
    uint8_t *data;

    if (path == NULL || out_data == NULL || out_size == NULL) {
        return 0;
    }
    *out_data = NULL;
    *out_size = 0;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 || file_size.QuadPart > 0x7FFFFFFF) {
        CloseHandle(file);
        return 0;
    }

    data = (uint8_t *)malloc((size_t)file_size.QuadPart);
    if (data == NULL) {
        CloseHandle(file);
        return 0;
    }

    bytes_read = 0;
    if (!ReadFile(file, data, (DWORD)file_size.QuadPart, &bytes_read, NULL) ||
        bytes_read != (DWORD)file_size.QuadPart) {
        free(data);
        CloseHandle(file);
        return 0;
    }

    CloseHandle(file);
    *out_data = data;
    *out_size = (size_t)file_size.QuadPart;
    return 1;
}

static void load_rom(HWND hwnd, const WCHAR *path)
{
    uint8_t *data;
    size_t size;
    NesResult result;
    WCHAR message[512];

    if (!read_entire_file_w(path, &data, &size)) {
        app_set_status(L"ROM load failed: file could not be read.");
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    result = nes_load_rom_image(&g_app.nes, data, size);
    free(data);

    if (result != NES_RESULT_OK) {
        swprintf(message,
                 sizeof(message) / sizeof(message[0]),
                 L"ROM load failed: %S.",
                 nes_result_string(result));
        app_set_status(message);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    wcsncpy(g_app.rom_path, path, (sizeof(g_app.rom_path) / sizeof(g_app.rom_path[0])) - 1u);
    g_app.rom_path[(sizeof(g_app.rom_path) / sizeof(g_app.rom_path[0])) - 1u] = L'\0';
    format_loaded_status(path);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void handle_drop(HWND hwnd, HDROP drop)
{
    WCHAR path[MAX_PATH];

    if (DragQueryFileW(drop, 0, path, sizeof(path) / sizeof(path[0])) > 0) {
        load_rom(hwnd, path);
    }
    DragFinish(drop);
}

static void set_key_state(HWND hwnd, WPARAM key, int pressed, LPARAM lparam)
{
    int first_press = (lparam & (1L << 30)) == 0;

    switch (key) {
    case 'W':
        nes_set_button(&g_app.nes, NES_BUTTON_UP, pressed);
        break;
    case 'A':
        nes_set_button(&g_app.nes, NES_BUTTON_LEFT, pressed);
        break;
    case 'S':
        nes_set_button(&g_app.nes, NES_BUTTON_DOWN, pressed);
        break;
    case 'D':
        nes_set_button(&g_app.nes, NES_BUTTON_RIGHT, pressed);
        break;
    case 'Z':
        nes_set_button(&g_app.nes, NES_BUTTON_A, pressed);
        break;
    case 'X':
        nes_set_button(&g_app.nes, NES_BUTTON_B, pressed);
        break;
    case 'C':
        nes_set_button(&g_app.nes, NES_BUTTON_START, pressed);
        break;
    case 'V':
        nes_set_button(&g_app.nes, NES_BUTTON_SELECT, pressed);
        break;
    case 'B':
        if (pressed && first_press) {
            nes_reset(&g_app.nes);
            if (g_app.nes.rom_loaded) {
                format_loaded_status(g_app.rom_path);
            } else {
                app_set_status(L"Reset.");
            }
        }
        break;
    default:
        return;
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

static void append_button_text(WCHAR *buffer, size_t count, const WCHAR *name, int pressed)
{
    if (pressed) {
        wcsncat(buffer, name, count - wcslen(buffer) - 1u);
        wcsncat(buffer, L" ", count - wcslen(buffer) - 1u);
    }
}

static void paint_window(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc;
    RECT rect;
    HFONT font;
    HFONT old_font;
    HBRUSH brush;
    WCHAR text[1024];
    WCHAR buttons[128];

    dc = BeginPaint(hwnd, &ps);
    GetClientRect(hwnd, &rect);

    brush = CreateSolidBrush(RGB(20, 22, 24));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(232, 236, 239));
    font = CreateFontW(19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                       FF_MODERN, L"Consolas");
    old_font = (HFONT)SelectObject(dc, font);

    buttons[0] = L'\0';
    append_button_text(buttons, sizeof(buttons) / sizeof(buttons[0]), L"UP", nes_get_button(&g_app.nes, NES_BUTTON_UP));
    append_button_text(buttons, sizeof(buttons) / sizeof(buttons[0]), L"DOWN", nes_get_button(&g_app.nes, NES_BUTTON_DOWN));
    append_button_text(buttons, sizeof(buttons) / sizeof(buttons[0]), L"LEFT", nes_get_button(&g_app.nes, NES_BUTTON_LEFT));
    append_button_text(buttons, sizeof(buttons) / sizeof(buttons[0]), L"RIGHT", nes_get_button(&g_app.nes, NES_BUTTON_RIGHT));
    append_button_text(buttons, sizeof(buttons) / sizeof(buttons[0]), L"A", nes_get_button(&g_app.nes, NES_BUTTON_A));
    append_button_text(buttons, sizeof(buttons) / sizeof(buttons[0]), L"B", nes_get_button(&g_app.nes, NES_BUTTON_B));
    append_button_text(buttons, sizeof(buttons) / sizeof(buttons[0]), L"START", nes_get_button(&g_app.nes, NES_BUTTON_START));
    append_button_text(buttons, sizeof(buttons) / sizeof(buttons[0]), L"SELECT", nes_get_button(&g_app.nes, NES_BUTTON_SELECT));
    if (buttons[0] == L'\0') {
        wcscpy(buttons, L"(none)");
    }

    swprintf(text,
             sizeof(text) / sizeof(text[0]),
             L"NESEMU\n\n%ls\n\nDrop a .nes ROM file onto this window.\nKeys: WASD move, Z A, X B, C START, V SELECT, B reset.\nPressed: %ls",
             g_app.status[0] != L'\0' ? g_app.status : L"No ROM loaded.",
             buttons);

    rect.left += 24;
    rect.top += 24;
    rect.right -= 24;
    rect.bottom -= 24;
    DrawTextW(dc, text, -1, &rect, DT_LEFT | DT_TOP | DT_WORDBREAK);

    SelectObject(dc, old_font);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        app_set_status(L"No ROM loaded.");
        return 0;
    case WM_DROPFILES:
        handle_drop(hwnd, (HDROP)wparam);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        set_key_state(hwnd, wparam, 1, lparam);
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        set_key_state(hwnd, wparam, 0, lparam);
        return 0;
    case WM_PAINT:
        paint_window(hwnd);
        return 0;
    case WM_DESTROY:
        nes_shutdown(&g_app.nes);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance, PWSTR command_line, int show_command)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG message;

    (void)previous_instance;
    (void)command_line;

    nes_init(&g_app.nes);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    hwnd = CreateWindowExW(0,
                           WINDOW_CLASS_NAME,
                           WINDOW_TITLE,
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT,
                           CW_USEDEFAULT,
                           800,
                           480,
                           NULL,
                           NULL,
                           instance,
                           NULL);
    if (hwnd == NULL) {
        return 1;
    }

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
