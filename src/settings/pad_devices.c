/* ---------------------------------------------------------------------------
 * Connected controllers, by name, for the controller dropdown
 * ------------------------------------------------------------------------- */
#include "settings.h"
#include <mmsystem.h>
#include <setupapi.h>
#include <hidsdi.h>

/* XInput devices show up as HID devices whose path contains "&IG_". */
static int is_xinput_path(const char *path) {
    char low[1024];
    lstrcpynA(low, path, sizeof low);
    CharLowerA(low);
    for (char *p = low; *p; p++)
        if (p[0] == '&' && p[1] == 'i' && p[2] == 'g' && p[3] == '_') return 1;
    return 0;
}

/* XInput controllers in slot order (which is the HID enumeration order of
 * the "IG_" devices). */
static int list_xinput(pad_entry *out, int max) {
    int n = 0;
    GUID g;
    HidD_GetHidGuid(&g);
    HDEVINFO set = SetupDiGetClassDevsA(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return 0;
    SP_DEVICE_INTERFACE_DATA ifd;
    ifd.cbSize = sizeof ifd;
    for (DWORD i = 0; n < max && n < 4 && SetupDiEnumDeviceInterfaces(set, NULL, &g, i, &ifd); i++) {
        union { SP_DEVICE_INTERFACE_DETAIL_DATA_A d; char raw[1024]; } u;
        u.d.cbSize = sizeof u.d;
        if (!SetupDiGetDeviceInterfaceDetailA(set, &ifd, &u.d, sizeof u, NULL, NULL)) continue;
        if (!is_xinput_path(u.d.DevicePath)) continue;
        WCHAR w[96] = L"";
        HANDLE h = CreateFileA(u.d.DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) { HidD_GetProductString(h, w, sizeof w); CloseHandle(h); }
        out[n].dev = PAD_XINPUT(n);
        if (!w[0] || !WideCharToMultiByte(CP_UTF8, 0, w, -1, out[n].name, sizeof out[n].name, NULL, NULL))
            wsprintfA(out[n].name, "Controller %d", n + 1);
        n++;
    }
    SetupDiDestroyDeviceInfoList(set);
    return n;
}

static int list_joysticks(pad_entry *out, int max) {
    int n = 0;
    UINT cnt = pad_joy_count();
    for (UINT j = 0; j < cnt && n < max; j++) {
        pad_joyinfoex ji;
        if (!pad_joy_read(j, &ji)) continue;
        JOYCAPSA jc;
        ZeroMemory(&jc, sizeof jc);
        out[n].dev = PAD_JOY((int)j);
        if (joyGetDevCapsA(j, &jc, sizeof jc) == 0 && jc.szPname[0])
            wsprintfA(out[n].name, "Joystick %u: %s", j + 1, jc.szPname);
        else
            wsprintfA(out[n].name, "Joystick %u", j + 1);
        n++;
    }
    return n;
}

/* XInput controllers, or the generic joysticks when there are none. */
int pad_list_devices(pad_entry *out, int max) {
    int n = list_xinput(out, max);
    return n ? n : list_joysticks(out, max);
}
