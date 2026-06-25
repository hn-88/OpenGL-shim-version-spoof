/*
 * glfw_shim.c
 *
 * Proxy glfw3.dll that intercepts glfwWindowHint to clamp the requested
 * OpenGL context version from 4.6 down to 4.1, allowing OpenSpace 0.22.x
 * (via SGCT) to run on drivers that only support OpenGL 4.1 (e.g. VMware
 * Fusion on macOS).
 *
 * Build: see build.yml
 * Install: copy glfw3.dll next to openspace.exe, rename real glfw3.dll to
 *          glfw3_real.dll in the same folder.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* GLFW_CONTEXT_VERSION_MAJOR / MINOR hint values */
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003

static HMODULE real_glfw = NULL;

typedef void (*PFN_glfwWindowHint)(int, int);
static PFN_glfwWindowHint real_glfwWindowHint = NULL;

/* ------------------------------------------------------------------ */
/* Forwarding table                                                     */
/* ------------------------------------------------------------------ */

#define FWD_COUNT 108
void *fwd_ptrs[FWD_COUNT];

static const char *fwd_names[FWD_COUNT] = {
    "glfwCreateCursor",
    "glfwCreateStandardCursor",
    "glfwCreateWindow",
    "glfwCreateWindowSurface",
    "glfwDefaultWindowHints",
    "glfwDestroyCursor",
    "glfwDestroyWindow",
    "glfwExtensionSupported",
    "glfwFocusWindow",
    "glfwGetCurrentContext",
    "glfwGetCursorPos",
    "glfwGetError",
    "glfwGetFramebufferSize",
    "glfwGetGamepadState",
    "glfwGetInputMode",
    "glfwGetInstanceProcAddress",
    "glfwGetJoystickUserPointer",
    "glfwGetKey",
    "glfwGetKeyScancode",
    "glfwGetMonitorContentScale",
    "glfwGetMonitorPhysicalSize",
    "glfwGetMonitorPos",
    "glfwGetMonitorUserPointer",
    "glfwGetMonitorWorkarea",
    "glfwGetMonitors",
    "glfwGetMouseButton",
    "glfwGetPhysicalDevicePresentationSupport",
    "glfwGetPlatform",
    "glfwGetPrimaryMonitor",
    "glfwGetProcAddress",
    "glfwGetTime",
    "glfwGetTimerFrequency",
    "glfwGetTimerValue",
    "glfwGetVersion",
    "glfwGetWindowAttrib",
    "glfwGetWindowContentScale",
    "glfwGetWindowFrameSize",
    "glfwGetWindowMonitor",
    "glfwGetWindowOpacity",
    "glfwGetWindowPos",
    "glfwGetWindowSize",
    "glfwGetWindowUserPointer",
    "glfwHideWindow",
    "glfwIconifyWindow",
    "glfwInit",
    "glfwInitAllocator",
    "glfwInitHint",
    "glfwInitVulkanLoader",
    "glfwJoystickIsGamepad",
    "glfwJoystickPresent",
    "glfwMakeContextCurrent",
    "glfwMaximizeWindow",
    "glfwPlatformSupported",
    "glfwPollEvents",
    "glfwPostEmptyEvent",
    "glfwRawMouseMotionSupported",
    "glfwRequestWindowAttention",
    "glfwRestoreWindow",
    "glfwSetCharCallback",
    "glfwSetCharModsCallback",
    "glfwSetClipboardString",
    "glfwSetCursor",
    "glfwSetCursorEnterCallback",
    "glfwSetCursorPos",
    "glfwSetCursorPosCallback",
    "glfwSetDropCallback",
    "glfwSetErrorCallback",
    "glfwSetFramebufferSizeCallback",
    "glfwSetGamma",
    "glfwSetGammaRamp",
    "glfwSetInputMode",
    "glfwSetJoystickCallback",
    "glfwSetJoystickUserPointer",
    "glfwSetKeyCallback",
    "glfwSetMonitorCallback",
    "glfwSetMonitorUserPointer",
    "glfwSetMouseButtonCallback",
    "glfwSetScrollCallback",
    "glfwSetTime",
    "glfwSetWindowAspectRatio",
    "glfwSetWindowAttrib",
    "glfwSetWindowCloseCallback",
    "glfwSetWindowContentScaleCallback",
    "glfwSetWindowFocusCallback",
    "glfwSetWindowIcon",
    "glfwSetWindowIconifyCallback",
    "glfwSetWindowMaximizeCallback",
    "glfwSetWindowMonitor",
    "glfwSetWindowOpacity",
    "glfwSetWindowPos",
    "glfwSetWindowPosCallback",
    "glfwSetWindowRefreshCallback",
    "glfwSetWindowShouldClose",
    "glfwSetWindowSize",
    "glfwSetWindowSizeCallback",
    "glfwSetWindowSizeLimits",
    "glfwSetWindowTitle",
    "glfwSetWindowUserPointer",
    "glfwShowWindow",
    "glfwSwapBuffers",
    "glfwSwapInterval",
    "glfwTerminate",
    "glfwUpdateGamepadMappings",
    "glfwVulkanSupported",
    "glfwWaitEvents",
    "glfwWaitEventsTimeout",
    "glfwWindowHintString",
    "glfwWindowShouldClose",
};

static void load_forwarded(HMODULE m) {
    for (int i = 0; i < FWD_COUNT; i++)
        fwd_ptrs[i] = (void*)GetProcAddress(m, fwd_names[i]);
}

/* ------------------------------------------------------------------ */
/* DllMain                                                              */
/* ------------------------------------------------------------------ */

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    (void)hInst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        /* Load real glfw3.dll from the same directory as our shim */
        char path[MAX_PATH];
        GetModuleFileNameA(hInst, path, sizeof(path));
        /* Chop filename, append glfw3_real.dll */
        /* Find last backslash, terminate after it */
        char *last = path;
        for (char *p = path; *p; p++) if (*p == '\\') last = p;
        *(last + 1) = '\0';
        strcat_s(path, sizeof(path), "glfw3_real.dll");
        real_glfw = LoadLibraryA(path);
        if (!real_glfw) return FALSE;

        real_glfwWindowHint = (PFN_glfwWindowHint)GetProcAddress(real_glfw, "glfwWindowHint");
        if (!real_glfwWindowHint) return FALSE;

        load_forwarded(real_glfw);
    }
    if (reason == DLL_PROCESS_DETACH && real_glfw) {
        FreeLibrary(real_glfw);
        real_glfw = NULL;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Intercepted: glfwWindowHint                                         */
/* ------------------------------------------------------------------ */

void glfwWindowHint(int hint, int value)
{
    if (hint == GLFW_CONTEXT_VERSION_MAJOR && value > 4) value = 4;
    if (hint == GLFW_CONTEXT_VERSION_MINOR && value > 1) value = 1;
    real_glfwWindowHint(hint, value);
}

/* ------------------------------------------------------------------ */
/* Naked forwarding thunks                                             */
/* ------------------------------------------------------------------ */

__attribute__((naked)) void glfwCreateCursor_fwd(void) {
    __asm__("jmp *fwd_ptrs+0(%rip)");
}

__attribute__((naked)) void glfwCreateStandardCursor_fwd(void) {
    __asm__("jmp *fwd_ptrs+8(%rip)");
}

__attribute__((naked)) void glfwCreateWindow_fwd(void) {
    __asm__("jmp *fwd_ptrs+16(%rip)");
}

__attribute__((naked)) void glfwCreateWindowSurface_fwd(void) {
    __asm__("jmp *fwd_ptrs+24(%rip)");
}

__attribute__((naked)) void glfwDefaultWindowHints_fwd(void) {
    __asm__("jmp *fwd_ptrs+32(%rip)");
}

__attribute__((naked)) void glfwDestroyCursor_fwd(void) {
    __asm__("jmp *fwd_ptrs+40(%rip)");
}

__attribute__((naked)) void glfwDestroyWindow_fwd(void) {
    __asm__("jmp *fwd_ptrs+48(%rip)");
}

__attribute__((naked)) void glfwExtensionSupported_fwd(void) {
    __asm__("jmp *fwd_ptrs+56(%rip)");
}

__attribute__((naked)) void glfwFocusWindow_fwd(void) {
    __asm__("jmp *fwd_ptrs+64(%rip)");
}

__attribute__((naked)) void glfwGetCurrentContext_fwd(void) {
    __asm__("jmp *fwd_ptrs+72(%rip)");
}

__attribute__((naked)) void glfwGetCursorPos_fwd(void) {
    __asm__("jmp *fwd_ptrs+80(%rip)");
}

__attribute__((naked)) void glfwGetError_fwd(void) {
    __asm__("jmp *fwd_ptrs+88(%rip)");
}

__attribute__((naked)) void glfwGetFramebufferSize_fwd(void) {
    __asm__("jmp *fwd_ptrs+96(%rip)");
}

__attribute__((naked)) void glfwGetGamepadState_fwd(void) {
    __asm__("jmp *fwd_ptrs+104(%rip)");
}

__attribute__((naked)) void glfwGetInputMode_fwd(void) {
    __asm__("jmp *fwd_ptrs+112(%rip)");
}

__attribute__((naked)) void glfwGetInstanceProcAddress_fwd(void) {
    __asm__("jmp *fwd_ptrs+120(%rip)");
}

__attribute__((naked)) void glfwGetJoystickUserPointer_fwd(void) {
    __asm__("jmp *fwd_ptrs+128(%rip)");
}

__attribute__((naked)) void glfwGetKey_fwd(void) {
    __asm__("jmp *fwd_ptrs+136(%rip)");
}

__attribute__((naked)) void glfwGetKeyScancode_fwd(void) {
    __asm__("jmp *fwd_ptrs+144(%rip)");
}

__attribute__((naked)) void glfwGetMonitorContentScale_fwd(void) {
    __asm__("jmp *fwd_ptrs+152(%rip)");
}

__attribute__((naked)) void glfwGetMonitorPhysicalSize_fwd(void) {
    __asm__("jmp *fwd_ptrs+160(%rip)");
}

__attribute__((naked)) void glfwGetMonitorPos_fwd(void) {
    __asm__("jmp *fwd_ptrs+168(%rip)");
}

__attribute__((naked)) void glfwGetMonitorUserPointer_fwd(void) {
    __asm__("jmp *fwd_ptrs+176(%rip)");
}

__attribute__((naked)) void glfwGetMonitorWorkarea_fwd(void) {
    __asm__("jmp *fwd_ptrs+184(%rip)");
}

__attribute__((naked)) void glfwGetMonitors_fwd(void) {
    __asm__("jmp *fwd_ptrs+192(%rip)");
}

__attribute__((naked)) void glfwGetMouseButton_fwd(void) {
    __asm__("jmp *fwd_ptrs+200(%rip)");
}

__attribute__((naked)) void glfwGetPhysicalDevicePresentationSupport_fwd(void) {
    __asm__("jmp *fwd_ptrs+208(%rip)");
}

__attribute__((naked)) void glfwGetPlatform_fwd(void) {
    __asm__("jmp *fwd_ptrs+216(%rip)");
}

__attribute__((naked)) void glfwGetPrimaryMonitor_fwd(void) {
    __asm__("jmp *fwd_ptrs+224(%rip)");
}

__attribute__((naked)) void glfwGetProcAddress_fwd(void) {
    __asm__("jmp *fwd_ptrs+232(%rip)");
}

__attribute__((naked)) void glfwGetTime_fwd(void) {
    __asm__("jmp *fwd_ptrs+240(%rip)");
}

__attribute__((naked)) void glfwGetTimerFrequency_fwd(void) {
    __asm__("jmp *fwd_ptrs+248(%rip)");
}

__attribute__((naked)) void glfwGetTimerValue_fwd(void) {
    __asm__("jmp *fwd_ptrs+256(%rip)");
}

__attribute__((naked)) void glfwGetVersion_fwd(void) {
    __asm__("jmp *fwd_ptrs+264(%rip)");
}

__attribute__((naked)) void glfwGetWindowAttrib_fwd(void) {
    __asm__("jmp *fwd_ptrs+272(%rip)");
}

__attribute__((naked)) void glfwGetWindowContentScale_fwd(void) {
    __asm__("jmp *fwd_ptrs+280(%rip)");
}

__attribute__((naked)) void glfwGetWindowFrameSize_fwd(void) {
    __asm__("jmp *fwd_ptrs+288(%rip)");
}

__attribute__((naked)) void glfwGetWindowMonitor_fwd(void) {
    __asm__("jmp *fwd_ptrs+296(%rip)");
}

__attribute__((naked)) void glfwGetWindowOpacity_fwd(void) {
    __asm__("jmp *fwd_ptrs+304(%rip)");
}

__attribute__((naked)) void glfwGetWindowPos_fwd(void) {
    __asm__("jmp *fwd_ptrs+312(%rip)");
}

__attribute__((naked)) void glfwGetWindowSize_fwd(void) {
    __asm__("jmp *fwd_ptrs+320(%rip)");
}

__attribute__((naked)) void glfwGetWindowUserPointer_fwd(void) {
    __asm__("jmp *fwd_ptrs+328(%rip)");
}

__attribute__((naked)) void glfwHideWindow_fwd(void) {
    __asm__("jmp *fwd_ptrs+336(%rip)");
}

__attribute__((naked)) void glfwIconifyWindow_fwd(void) {
    __asm__("jmp *fwd_ptrs+344(%rip)");
}

__attribute__((naked)) void glfwInit_fwd(void) {
    __asm__("jmp *fwd_ptrs+352(%rip)");
}

__attribute__((naked)) void glfwInitAllocator_fwd(void) {
    __asm__("jmp *fwd_ptrs+360(%rip)");
}

__attribute__((naked)) void glfwInitHint_fwd(void) {
    __asm__("jmp *fwd_ptrs+368(%rip)");
}

__attribute__((naked)) void glfwInitVulkanLoader_fwd(void) {
    __asm__("jmp *fwd_ptrs+376(%rip)");
}

__attribute__((naked)) void glfwJoystickIsGamepad_fwd(void) {
    __asm__("jmp *fwd_ptrs+384(%rip)");
}

__attribute__((naked)) void glfwJoystickPresent_fwd(void) {
    __asm__("jmp *fwd_ptrs+392(%rip)");
}

__attribute__((naked)) void glfwMakeContextCurrent_fwd(void) {
    __asm__("jmp *fwd_ptrs+400(%rip)");
}

__attribute__((naked)) void glfwMaximizeWindow_fwd(void) {
    __asm__("jmp *fwd_ptrs+408(%rip)");
}

__attribute__((naked)) void glfwPlatformSupported_fwd(void) {
    __asm__("jmp *fwd_ptrs+416(%rip)");
}

__attribute__((naked)) void glfwPollEvents_fwd(void) {
    __asm__("jmp *fwd_ptrs+424(%rip)");
}

__attribute__((naked)) void glfwPostEmptyEvent_fwd(void) {
    __asm__("jmp *fwd_ptrs+432(%rip)");
}

__attribute__((naked)) void glfwRawMouseMotionSupported_fwd(void) {
    __asm__("jmp *fwd_ptrs+440(%rip)");
}

__attribute__((naked)) void glfwRequestWindowAttention_fwd(void) {
    __asm__("jmp *fwd_ptrs+448(%rip)");
}

__attribute__((naked)) void glfwRestoreWindow_fwd(void) {
    __asm__("jmp *fwd_ptrs+456(%rip)");
}

__attribute__((naked)) void glfwSetCharCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+464(%rip)");
}

__attribute__((naked)) void glfwSetCharModsCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+472(%rip)");
}

__attribute__((naked)) void glfwSetClipboardString_fwd(void) {
    __asm__("jmp *fwd_ptrs+480(%rip)");
}

__attribute__((naked)) void glfwSetCursor_fwd(void) {
    __asm__("jmp *fwd_ptrs+488(%rip)");
}

__attribute__((naked)) void glfwSetCursorEnterCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+496(%rip)");
}

__attribute__((naked)) void glfwSetCursorPos_fwd(void) {
    __asm__("jmp *fwd_ptrs+504(%rip)");
}

__attribute__((naked)) void glfwSetCursorPosCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+512(%rip)");
}

__attribute__((naked)) void glfwSetDropCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+520(%rip)");
}

__attribute__((naked)) void glfwSetErrorCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+528(%rip)");
}

__attribute__((naked)) void glfwSetFramebufferSizeCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+536(%rip)");
}

__attribute__((naked)) void glfwSetGamma_fwd(void) {
    __asm__("jmp *fwd_ptrs+544(%rip)");
}

__attribute__((naked)) void glfwSetGammaRamp_fwd(void) {
    __asm__("jmp *fwd_ptrs+552(%rip)");
}

__attribute__((naked)) void glfwSetInputMode_fwd(void) {
    __asm__("jmp *fwd_ptrs+560(%rip)");
}

__attribute__((naked)) void glfwSetJoystickCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+568(%rip)");
}

__attribute__((naked)) void glfwSetJoystickUserPointer_fwd(void) {
    __asm__("jmp *fwd_ptrs+576(%rip)");
}

__attribute__((naked)) void glfwSetKeyCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+584(%rip)");
}

__attribute__((naked)) void glfwSetMonitorCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+592(%rip)");
}

__attribute__((naked)) void glfwSetMonitorUserPointer_fwd(void) {
    __asm__("jmp *fwd_ptrs+600(%rip)");
}

__attribute__((naked)) void glfwSetMouseButtonCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+608(%rip)");
}

__attribute__((naked)) void glfwSetScrollCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+616(%rip)");
}

__attribute__((naked)) void glfwSetTime_fwd(void) {
    __asm__("jmp *fwd_ptrs+624(%rip)");
}

__attribute__((naked)) void glfwSetWindowAspectRatio_fwd(void) {
    __asm__("jmp *fwd_ptrs+632(%rip)");
}

__attribute__((naked)) void glfwSetWindowAttrib_fwd(void) {
    __asm__("jmp *fwd_ptrs+640(%rip)");
}

__attribute__((naked)) void glfwSetWindowCloseCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+648(%rip)");
}

__attribute__((naked)) void glfwSetWindowContentScaleCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+656(%rip)");
}

__attribute__((naked)) void glfwSetWindowFocusCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+664(%rip)");
}

__attribute__((naked)) void glfwSetWindowIcon_fwd(void) {
    __asm__("jmp *fwd_ptrs+672(%rip)");
}

__attribute__((naked)) void glfwSetWindowIconifyCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+680(%rip)");
}

__attribute__((naked)) void glfwSetWindowMaximizeCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+688(%rip)");
}

__attribute__((naked)) void glfwSetWindowMonitor_fwd(void) {
    __asm__("jmp *fwd_ptrs+696(%rip)");
}

__attribute__((naked)) void glfwSetWindowOpacity_fwd(void) {
    __asm__("jmp *fwd_ptrs+704(%rip)");
}

__attribute__((naked)) void glfwSetWindowPos_fwd(void) {
    __asm__("jmp *fwd_ptrs+712(%rip)");
}

__attribute__((naked)) void glfwSetWindowPosCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+720(%rip)");
}

__attribute__((naked)) void glfwSetWindowRefreshCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+728(%rip)");
}

__attribute__((naked)) void glfwSetWindowShouldClose_fwd(void) {
    __asm__("jmp *fwd_ptrs+736(%rip)");
}

__attribute__((naked)) void glfwSetWindowSize_fwd(void) {
    __asm__("jmp *fwd_ptrs+744(%rip)");
}

__attribute__((naked)) void glfwSetWindowSizeCallback_fwd(void) {
    __asm__("jmp *fwd_ptrs+752(%rip)");
}

__attribute__((naked)) void glfwSetWindowSizeLimits_fwd(void) {
    __asm__("jmp *fwd_ptrs+760(%rip)");
}

__attribute__((naked)) void glfwSetWindowTitle_fwd(void) {
    __asm__("jmp *fwd_ptrs+768(%rip)");
}

__attribute__((naked)) void glfwSetWindowUserPointer_fwd(void) {
    __asm__("jmp *fwd_ptrs+776(%rip)");
}

__attribute__((naked)) void glfwShowWindow_fwd(void) {
    __asm__("jmp *fwd_ptrs+784(%rip)");
}

__attribute__((naked)) void glfwSwapBuffers_fwd(void) {
    __asm__("jmp *fwd_ptrs+792(%rip)");
}

__attribute__((naked)) void glfwSwapInterval_fwd(void) {
    __asm__("jmp *fwd_ptrs+800(%rip)");
}

__attribute__((naked)) void glfwTerminate_fwd(void) {
    __asm__("jmp *fwd_ptrs+808(%rip)");
}

__attribute__((naked)) void glfwUpdateGamepadMappings_fwd(void) {
    __asm__("jmp *fwd_ptrs+816(%rip)");
}

__attribute__((naked)) void glfwVulkanSupported_fwd(void) {
    __asm__("jmp *fwd_ptrs+824(%rip)");
}

__attribute__((naked)) void glfwWaitEvents_fwd(void) {
    __asm__("jmp *fwd_ptrs+832(%rip)");
}

__attribute__((naked)) void glfwWaitEventsTimeout_fwd(void) {
    __asm__("jmp *fwd_ptrs+840(%rip)");
}

__attribute__((naked)) void glfwWindowHintString_fwd(void) {
    __asm__("jmp *fwd_ptrs+848(%rip)");
}

__attribute__((naked)) void glfwWindowShouldClose_fwd(void) {
    __asm__("jmp *fwd_ptrs+856(%rip)");
}
