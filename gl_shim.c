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
        real_glGetStringi      = (PFN_glGetStringi)     GetProcAddress(real_gl, "glGetStringi");
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

const GLubyte *WINAPI glGetString(GLenum name)
{
    switch (name) {
        case GL_VERSION:
            return (const GLubyte *)"4.6.0 Compatibility Profile";
        case GL_SHADING_LANGUAGE_VERSION:
            return (const GLubyte *)"4.60";
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
            if (attribList[i] == WGL_CONTEXT_MINOR_VERSION_ARB && attribList[i+1] > 1)
                patched[i+1] = 1;
        }
    }
    patched[i] = 0; /* terminator */

    return real_wglCreateContextAttribsARB(hdc, hShareContext, patched);
}

/* ------------------------------------------------------------------ */
/* wglGetProcAddress — intercept to hook wglCreateContextAttribsARB   */
/* ------------------------------------------------------------------ */

PROC WINAPI wglGetProcAddress(LPCSTR name)
{
    PROC real = real_wglGetProcAddress(name);

    if (name && strcmp(name, "wglCreateContextAttribsARB") == 0 && real) {
        /* Stash the real pointer so our hook can call it */
        real_wglCreateContextAttribsARB = (PFN_wglCreateContextAttribsARB)real;
        return (PROC)hooked_wglCreateContextAttribsARB;
    }

    return real;
}
