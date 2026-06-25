/*
 * gl_shim.c
 *
 * Proxy opengl32.dll that intercepts glGetString / glGetIntegerv to report
 * OpenGL 4.6, forwarding everything else to the real System32 opengl32.dll.
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
/* wglGetProcAddress — forward to real DLL                             */
/* ------------------------------------------------------------------ */

PROC WINAPI wglGetProcAddress(LPCSTR name)
{
    return real_wglGetProcAddress(name);
}
