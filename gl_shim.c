/*
 * gl_shim.c
 *
 * Proxy opengl32.dll that:
 *   1. Reports OpenGL 4.6 via glGetString / glGetIntegerv
 *   2. Intercepts wglCreateContextAttribsARB (via wglGetProcAddress) to clamp
 *      the requested context version to 4.1, so GLFW/SGCT can create a context
 *      even when GLFW is statically linked and a glfw3.dll shim won't reach it.
 * Everything else is forwarded to the real System32 opengl32.dll.
 *
 * Build: see build.yml (GitHub Actions, MinGW cross-compiler)
 * Install: copy opengl32.dll next to openspace.exe
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Include gl.h for types only — then undefine the dllimport-decorated
   prototypes for the functions we are re-implementing, otherwise MinGW
   complains about the redeclaration losing the dllimport attribute. */
#include <GL/gl.h>
#undef glGetString
#undef glGetStringi
#undef glGetIntegerv
#undef wglGetProcAddress

#include <string.h>

/* GL_SHADING_LANGUAGE_VERSION is GL 2.0 and not in MinGW's older gl.h */
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif

/* ------------------------------------------------------------------ */
/* Real function pointers                                               */
/* ------------------------------------------------------------------ */

static HMODULE real_gl = NULL;

typedef const GLubyte *(WINAPI *PFN_glGetString)(GLenum);
typedef const GLubyte *(WINAPI *PFN_glGetStringi)(GLenum, GLuint);
typedef void           (WINAPI *PFN_glGetIntegerv)(GLenum, GLint *);
typedef PROC           (WINAPI *PFN_wglGetProcAddress)(LPCSTR);

static PFN_glGetString      real_glGetString      = NULL;
static PFN_glGetStringi     real_glGetStringi     = NULL;
static PFN_glGetIntegerv    real_glGetIntegerv    = NULL;
static PFN_wglGetProcAddress real_wglGetProcAddress = NULL;

/* ------------------------------------------------------------------ */
/* DllMain — load real opengl32.dll from System32                      */
/* ------------------------------------------------------------------ */

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    (void)hInst; (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, sizeof(path));
        strcat_s(path, sizeof(path), "\\opengl32.dll");
        real_gl = LoadLibraryA(path);
        if (!real_gl) return FALSE;

        real_glGetString       = (PFN_glGetString)      GetProcAddress(real_gl, "glGetString");
        /* glGetStringi is GL 3.0+, not a static export - loaded later via wglGetProcAddress */
        real_glGetIntegerv     = (PFN_glGetIntegerv)    GetProcAddress(real_gl, "glGetIntegerv");
        real_wglGetProcAddress = (PFN_wglGetProcAddress)GetProcAddress(real_gl, "wglGetProcAddress");

        if (!real_glGetString || !real_glGetIntegerv || !real_wglGetProcAddress)
            return FALSE;
    }

    if (reason == DLL_PROCESS_DETACH && real_gl) {
        FreeLibrary(real_gl);
        real_gl = NULL;
    }

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Intercepted functions                                                */
/* ------------------------------------------------------------------ */

/* GL_MAJOR_VERSION / GL_MINOR_VERSION pname values */
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C

#ifndef GL_EXTENSIONS
#define GL_EXTENSIONS 0x1F03
#endif

const GLubyte *WINAPI glGetString(GLenum name)
{
    switch (name) {
        case GL_VERSION:
            return (const GLubyte *)"4.6.0 Compatibility Profile";
        case GL_SHADING_LANGUAGE_VERSION:
            return (const GLubyte *)"4.60";
        case GL_EXTENSIONS:
            /* Core profile (GL 3.0+) returns NULL for GL_EXTENSIONS; callers
               must use glGetStringi instead. Return empty string rather than
               NULL so GLFW does not treat it as a platform error. */
            return (const GLubyte *)"";
        default:
            return real_glGetString(name);
    }
}

/* glGetStringi is used for indexed extension queries — pass through unchanged */
const GLubyte *WINAPI glGetStringi(GLenum name, GLuint index)
{
    if (real_glGetStringi)
        return real_glGetStringi(name, index);
    return NULL;
}

void WINAPI glGetIntegerv(GLenum pname, GLint *data)
{
    real_glGetIntegerv(pname, data);
    /* Override version integers queried via glGetIntegerv */
    if (pname == GL_MAJOR_VERSION && data) { *data = 4; return; }
    if (pname == GL_MINOR_VERSION && data) { *data = 6; return; }
}

/* ------------------------------------------------------------------ */
/* wglCreateContextAttribsARB interception                             */
/*                                                                     */
/* GLFW (whether DLL or statically linked) creates GL contexts via     */
/* wglCreateContextAttribsARB, passing WGL_CONTEXT_MAJOR/MINOR_VERSION */
/* attributes. The driver rejects anything above 4.1 on this host.    */
/* We intercept by returning our wrapper from wglGetProcAddress        */
/* instead of the real function pointer.                               */
/* ------------------------------------------------------------------ */

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092

typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
static PFN_wglCreateContextAttribsARB real_wglCreateContextAttribsARB = NULL;

HGLRC WINAPI hooked_wglCreateContextAttribsARB(HDC hdc, HGLRC hShareContext,
                                                const int *attribList)
{
    /* Copy attrib list, clamping major to 4 and minor to 1.
       The list is key/value int pairs terminated by a 0. */
    int patched[64];
    int i = 0;

    if (attribList) {
        for (; attribList[i] != 0 && i < 62; i += 2) {
            patched[i]   = attribList[i];
            patched[i+1] = attribList[i+1];

            if (attribList[i] == WGL_CONTEXT_MAJOR_VERSION_ARB && attribList[i+1] > 4)
                patched[i+1] = 4;
            if (attribList[i] == WGL_CONTEXT_MINOR_VERSION_ARB && attribList[i+1] > 3)
                patched[i+1] = 3;
        }
    }
    patched[i] = 0; /* terminator */

    /* First try with our clamped version. If the driver still refuses,
       retry stepping the minor version down to 0. */
    HGLRC ctx = real_wglCreateContextAttribsARB(hdc, hShareContext, patched);
    if (ctx) return ctx;

    /* Retry loop: step minor down from 3 to 0 */
    for (int minor = 2; minor >= 0; minor--) {
        for (int j = 0; patched[j] != 0; j += 2) {
            if (patched[j] == WGL_CONTEXT_MINOR_VERSION_ARB)
                patched[j+1] = minor;
        }
        ctx = real_wglCreateContextAttribsARB(hdc, hShareContext, patched);
        if (ctx) return ctx;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* wglGetProcAddress — intercept to hook wglCreateContextAttribsARB   */
/* ------------------------------------------------------------------ */

PROC WINAPI wglGetProcAddress(LPCSTR name)
{
    PROC real = real_wglGetProcAddress(name);

    if (!name) return real;

    /* Intercept wglCreateContextAttribsARB to clamp requested GL version */
    if (strcmp(name, "wglCreateContextAttribsARB") == 0 && real) {
        real_wglCreateContextAttribsARB = (PFN_wglCreateContextAttribsARB)real;
        return (PROC)hooked_wglCreateContextAttribsARB;
    }

    /* GLFW loads glGetString and glGetIntegerv via wglGetProcAddress rather
       than using the DLL export — so we must intercept here too, otherwise
       GLFW reads the real version string ("4.3.0..."), parses it as 4.3,
       and fails its post-creation version check against the requested 4.6. */
    if (strcmp(name, "glGetString") == 0)    return (PROC)glGetString;
    if (strcmp(name, "glGetStringi") == 0) {
        /* Stash the real pointer now that we have a live context */
        if (!real_glGetStringi && real)
            real_glGetStringi = (PFN_glGetStringi)real;
        return (PROC)glGetStringi;
    }
    if (strcmp(name, "glGetIntegerv") == 0)  return (PROC)glGetIntegerv;

    return real;
}
