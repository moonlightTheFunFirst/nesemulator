#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "nesemu.h"

#define WINDOW_CLASS_NAME L"NESEMUWindow"
#define WINDOW_TITLE      L"NESEMU"
#define FRAME_TIMER_ID    1u
#define AUDIO_BUFFERS     4u
#define AUDIO_SAMPLES     (NESEMU_AUDIO_RATE / 60u)

typedef struct AppState {
    NesEmu nes;
    WCHAR rom_path[MAX_PATH];
    WCHAR status[512];
    BITMAPINFO frame_bmi;
    HWAVEOUT wave_out;
    WAVEHDR wave_headers[AUDIO_BUFFERS];
    int16_t wave_buffers[AUDIO_BUFFERS][AUDIO_SAMPLES];
    int audio_open;
    HDC paint_dc;
    HBITMAP paint_bitmap;
    HBITMAP old_paint_bitmap;
    int paint_width;
    int paint_height;
    int reset_key_down;
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

static void app_init_bitmap_info(void)
{
    memset(&g_app.frame_bmi, 0, sizeof(g_app.frame_bmi));
    g_app.frame_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_app.frame_bmi.bmiHeader.biWidth = NESEMU_SCREEN_WIDTH;
    g_app.frame_bmi.bmiHeader.biHeight = -(LONG)NESEMU_SCREEN_HEIGHT;
    g_app.frame_bmi.bmiHeader.biPlanes = 1;
    g_app.frame_bmi.bmiHeader.biBitCount = 32;
    g_app.frame_bmi.bmiHeader.biCompression = BI_RGB;
}

static void app_release_backbuffer(void)
{
    if (g_app.paint_dc != NULL) {
        if (g_app.old_paint_bitmap != NULL) {
            SelectObject(g_app.paint_dc, g_app.old_paint_bitmap);
        }
        if (g_app.paint_bitmap != NULL) {
            DeleteObject(g_app.paint_bitmap);
        }
        DeleteDC(g_app.paint_dc);
    }
    g_app.paint_dc = NULL;
    g_app.paint_bitmap = NULL;
    g_app.old_paint_bitmap = NULL;
    g_app.paint_width = 0;
    g_app.paint_height = 0;
}

static HDC app_get_backbuffer(HDC window_dc, int width, int height)
{
    HBITMAP bitmap;

    if (width <= 0 || height <= 0) {
        return NULL;
    }
    if (g_app.paint_dc != NULL && g_app.paint_width == width && g_app.paint_height == height) {
        return g_app.paint_dc;
    }

    app_release_backbuffer();
    g_app.paint_dc = CreateCompatibleDC(window_dc);
    if (g_app.paint_dc == NULL) {
        return NULL;
    }
    bitmap = CreateCompatibleBitmap(window_dc, width, height);
    if (bitmap == NULL) {
        app_release_backbuffer();
        return NULL;
    }
    g_app.paint_bitmap = bitmap;
    g_app.old_paint_bitmap = (HBITMAP)SelectObject(g_app.paint_dc, g_app.paint_bitmap);
    g_app.paint_width = width;
    g_app.paint_height = height;
    return g_app.paint_dc;
}

static void audio_close(void)
{
    unsigned int i;

    if (!g_app.audio_open) {
        return;
    }
    waveOutReset(g_app.wave_out);
    for (i = 0; i < AUDIO_BUFFERS; ++i) {
        if ((g_app.wave_headers[i].dwFlags & WHDR_PREPARED) != 0) {
            waveOutUnprepareHeader(g_app.wave_out, &g_app.wave_headers[i], sizeof(WAVEHDR));
        }
    }
    waveOutClose(g_app.wave_out);
    memset(g_app.wave_headers, 0, sizeof(g_app.wave_headers));
    g_app.wave_out = NULL;
    g_app.audio_open = 0;
}

static void audio_fill_and_submit(unsigned int index)
{
    WAVEHDR *header = &g_app.wave_headers[index];

    nes_render_audio(&g_app.nes, g_app.wave_buffers[index], AUDIO_SAMPLES, NESEMU_AUDIO_RATE);
    header->dwFlags &= (DWORD)~WHDR_DONE;
    waveOutWrite(g_app.wave_out, header, sizeof(WAVEHDR));
}

static void audio_pump(void)
{
    unsigned int i;

    if (!g_app.audio_open || !g_app.nes.rom_loaded) {
        return;
    }
    for (i = 0; i < AUDIO_BUFFERS; ++i) {
        if ((g_app.wave_headers[i].dwFlags & WHDR_DONE) != 0) {
            audio_fill_and_submit(i);
        }
    }
}

static void audio_start(void)
{
    WAVEFORMATEX format;
    MMRESULT result;
    unsigned int i;

    audio_close();

    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = NESEMU_AUDIO_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = (WORD)(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    result = waveOutOpen(&g_app.wave_out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        g_app.audio_open = 0;
        return;
    }

    g_app.audio_open = 1;
    for (i = 0; i < AUDIO_BUFFERS; ++i) {
        memset(&g_app.wave_headers[i], 0, sizeof(WAVEHDR));
        g_app.wave_headers[i].lpData = (LPSTR)g_app.wave_buffers[i];
        g_app.wave_headers[i].dwBufferLength = sizeof(g_app.wave_buffers[i]);
        waveOutPrepareHeader(g_app.wave_out, &g_app.wave_headers[i], sizeof(WAVEHDR));
        audio_fill_and_submit(i);
    }
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
    audio_start();
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void handle_drop(HWND hwnd, HDROP drop)
{
    WCHAR path[MAX_PATH];

    if (DragQueryFileW(drop, 0, path, sizeof(path) / sizeof(path[0])) > 0) {
        load_rom(hwnd, path);
        SetFocus(hwnd);
    }
    DragFinish(drop);
}

static int app_key_down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static void app_sync_keyboard(HWND hwnd)
{
    int active = GetForegroundWindow() == hwnd;
    int reset_pressed;

    if (!active) {
        nes_set_button(&g_app.nes, NES_BUTTON_UP, 0);
        nes_set_button(&g_app.nes, NES_BUTTON_DOWN, 0);
        nes_set_button(&g_app.nes, NES_BUTTON_LEFT, 0);
        nes_set_button(&g_app.nes, NES_BUTTON_RIGHT, 0);
        nes_set_button(&g_app.nes, NES_BUTTON_A, 0);
        nes_set_button(&g_app.nes, NES_BUTTON_B, 0);
        nes_set_button(&g_app.nes, NES_BUTTON_START, 0);
        nes_set_button(&g_app.nes, NES_BUTTON_SELECT, 0);
        g_app.reset_key_down = 0;
        return;
    }

    nes_set_button(&g_app.nes, NES_BUTTON_UP, app_key_down('W') || app_key_down(VK_UP));
    nes_set_button(&g_app.nes, NES_BUTTON_DOWN, app_key_down('S') || app_key_down(VK_DOWN));
    nes_set_button(&g_app.nes, NES_BUTTON_LEFT, app_key_down('A') || app_key_down(VK_LEFT));
    nes_set_button(&g_app.nes, NES_BUTTON_RIGHT, app_key_down('D') || app_key_down(VK_RIGHT));
    nes_set_button(&g_app.nes, NES_BUTTON_A, app_key_down('Z') || app_key_down(VK_SPACE));
    nes_set_button(&g_app.nes, NES_BUTTON_B, app_key_down('X') || app_key_down(VK_SHIFT));
    nes_set_button(&g_app.nes, NES_BUTTON_START, app_key_down('C') || app_key_down(VK_RETURN));
    nes_set_button(&g_app.nes, NES_BUTTON_SELECT, app_key_down('V') || app_key_down(VK_BACK));

    reset_pressed = app_key_down('B');
    if (reset_pressed && !g_app.reset_key_down) {
        nes_reset(&g_app.nes);
        if (g_app.nes.rom_loaded) {
            format_loaded_status(g_app.rom_path);
        } else {
            app_set_status(L"Reset.");
        }
    }
    g_app.reset_key_down = reset_pressed;
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
    SetFocus(hwnd);
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
    HDC window_dc;
    HDC dc;
    RECT rect;
    RECT frame_rect;
    HFONT font;
    HFONT old_font;
    HBRUSH brush;
    WCHAR text[1024];
    WCHAR buttons[128];
    const uint32_t *framebuffer;
    int client_w;
    int client_h;
    int scale;
    int draw_w;
    int draw_h;

    window_dc = BeginPaint(hwnd, &ps);
    GetClientRect(hwnd, &rect);
    client_w = rect.right - rect.left;
    client_h = rect.bottom - rect.top;
    dc = app_get_backbuffer(window_dc, client_w, client_h);
    if (dc == NULL) {
        dc = window_dc;
    }

    brush = CreateSolidBrush(RGB(20, 22, 24));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);

    framebuffer = nes_get_framebuffer(&g_app.nes);
    if (g_app.nes.rom_loaded && framebuffer != NULL) {
        scale = client_w / (int)NESEMU_SCREEN_WIDTH;
        if (client_h / (int)NESEMU_SCREEN_HEIGHT < scale) {
            scale = client_h / (int)NESEMU_SCREEN_HEIGHT;
        }
        if (scale < 1) {
            scale = 1;
        }
        draw_w = (int)NESEMU_SCREEN_WIDTH * scale;
        draw_h = (int)NESEMU_SCREEN_HEIGHT * scale;
        frame_rect.left = (client_w - draw_w) / 2;
        frame_rect.top = (client_h - draw_h) / 2;
        frame_rect.right = frame_rect.left + draw_w;
        frame_rect.bottom = frame_rect.top + draw_h;
        StretchDIBits(dc,
                      frame_rect.left,
                      frame_rect.top,
                      draw_w,
                      draw_h,
                      0,
                      0,
                      NESEMU_SCREEN_WIDTH,
                      NESEMU_SCREEN_HEIGHT,
                      framebuffer,
                      &g_app.frame_bmi,
                      DIB_RGB_COLORS,
                      SRCCOPY);
    } else {
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
                 L"NESEMU\n\n%ls\n\nDrop a .nes ROM file onto this window.\nKeys: WASD/Arrows move, Z/Space A, X/Shift B, C/Enter START, V/Backspace SELECT, B reset.\nPressed: %ls",
                 g_app.status[0] != L'\0' ? g_app.status : L"No ROM loaded.",
                 buttons);

        rect.left += 24;
        rect.top += 24;
        rect.right -= 24;
        rect.bottom -= 24;
        DrawTextW(dc, text, -1, &rect, DT_LEFT | DT_TOP | DT_WORDBREAK);

        SelectObject(dc, old_font);
        DeleteObject(font);
    }
    if (dc != window_dc) {
        BitBlt(window_dc, 0, 0, client_w, client_h, dc, 0, 0, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        app_init_bitmap_info();
        app_set_status(L"No ROM loaded.");
        timeBeginPeriod(1);
        SetTimer(hwnd, FRAME_TIMER_ID, 16, NULL);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DROPFILES:
        handle_drop(hwnd, (HDROP)wparam);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
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
    case WM_TIMER:
        if (wparam == FRAME_TIMER_ID && g_app.nes.rom_loaded) {
            app_sync_keyboard(hwnd);
            nes_run_frame(&g_app.nes);
            audio_pump();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, FRAME_TIMER_ID);
        timeEndPeriod(1);
        audio_close();
        app_release_backbuffer();
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
    int argc;
    LPWSTR *argv;

    (void)previous_instance;
    (void)command_line;

    nes_init(&g_app.nes);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
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

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != NULL) {
        if (argc >= 2) {
            load_rom(hwnd, argv[1]);
        }
        LocalFree(argv);
    }

    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
