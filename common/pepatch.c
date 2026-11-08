/* Rewrites a game's DDRAW.DLL import to FPSDD.DLL (same length) and back.
 * Shared by rmpatch.exe and retrometer.exe. */
#include "pepatch.h"

static int ieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

static DWORD rva2off(IMAGE_NT_HEADERS *nt, DWORD rva, DWORD size)
{
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    int i;
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        DWORD vs = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
        if (rva >= s->VirtualAddress && rva < s->VirtualAddress + vs) {
            DWORD off = rva - s->VirtualAddress;
            if (off >= s->SizeOfRawData) return 0;
            off += s->PointerToRawData;
            return off < size ? off : 0;
        }
    }
    return 0;
}

static unsigned char *load(const char *path, DWORD *size)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    unsigned char *buf;
    DWORD got;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize(h, NULL);
    buf = (unsigned char *)GlobalAlloc(GMEM_FIXED, *size + 16);
    if (buf && (!ReadFile(h, buf, *size, &got, NULL) || got != *size)) { GlobalFree(buf); buf = NULL; }
    CloseHandle(h);
    return buf;
}

static IMAGE_IMPORT_DESCRIPTOR *imports(unsigned char *buf, DWORD size, IMAGE_NT_HEADERS **pnt)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    IMAGE_NT_HEADERS *nt;
    DWORD imp, off;
    if (size < 0x40 || dos->e_magic != IMAGE_DOS_SIGNATURE || (DWORD)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > size)
        return NULL;
    nt = (IMAGE_NT_HEADERS *)(buf + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return NULL;
    imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    off = imp ? rva2off(nt, imp, size) : 0;
    if (!off) return NULL;
    *pnt = nt;
    return (IMAGE_IMPORT_DESCRIPTOR *)(buf + off);
}

int pe_ddraw_state(const char *path)
{
    DWORD size;
    unsigned char *buf = load(path, &size);
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *d;
    int st = PE_NOT_PE;
    if (!buf) return PE_NOT_PE;
    d = imports(buf, size, &nt);
    if (d) {
        st = PE_NO_DDRAW;
        for (; (unsigned char *)(d + 1) <= buf + size && d->Name; d++) {
            DWORD noff = rva2off(nt, d->Name, size);
            if (!noff) continue;
            if (ieq((char *)buf + noff, "DDRAW.DLL")) st = PE_DDRAW;
            else if (ieq((char *)buf + noff, "FPSDD.DLL")) st = PE_FPSDD;
        }
    }
    GlobalFree(buf);
    return st;
}

int pe_ddraw_patch(const char *path, int to_fps, char *err)
{
    DWORD size, got;
    unsigned char *buf = load(path, &size);
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *d;
    char bak[MAX_PATH + 16];
    HANDLE h;
    int found = 0;

    if (!buf) { lstrcpyA(err, "Could not read the file."); return 0; }
    d = imports(buf, size, &nt);
    if (!d) { lstrcpyA(err, "Not a 32-bit Windows program with an import table."); GlobalFree(buf); return 0; }

    for (; (unsigned char *)(d + 1) <= buf + size && d->Name; d++) {
        DWORD noff = rva2off(nt, d->Name, size);
        char *name;
        if (!noff) continue;
        name = (char *)buf + noff;
        if (!ieq(name, to_fps ? "DDRAW.DLL" : "FPSDD.DLL")) continue;
        /* Pre-bound import: copy the name table back over the bound addresses. */
        if (d->TimeDateStamp != 0) {
            DWORD oft = d->OriginalFirstThunk ? rva2off(nt, d->OriginalFirstThunk, size) : 0;
            DWORD ft = rva2off(nt, d->FirstThunk, size);
            DWORD *src, *dst;
            if (!oft || !ft) {
                lstrcpyA(err, "This exe has pre-bound imports with no name table, so it can't be patched safely.");
                GlobalFree(buf);
                return 0;
            }
            src = (DWORD *)(buf + oft); dst = (DWORD *)(buf + ft);
            while ((unsigned char *)(src + 1) <= buf + size && (unsigned char *)(dst + 1) <= buf + size && *src) *dst++ = *src++;
            d->TimeDateStamp = 0;
            d->ForwarderChain = 0;
        }
        lstrcpyA(name, to_fps ? "FPSDD.DLL" : "DDRAW.DLL");
        found = 1;
    }
    if (!found) {
        lstrcpyA(err, to_fps ? "This program does not import DDRAW.DLL directly. It may load DirectDraw at run time, "
                               "or use another API (try the OpenGL, Glide or Direct3D 8 overlay)."
                             : "This program is not patched.");
        GlobalFree(buf);
        return 0;
    }
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size = 0;
    nt->OptionalHeader.CheckSum = 0;

    lstrcpyA(bak, path);
    lstrcatA(bak, ".rmbak");
    if (to_fps && GetFileAttributesA(bak) == 0xFFFFFFFF && !CopyFileA(path, bak, TRUE)) {
        lstrcpyA(err, "Could not write the backup copy. Is the folder read-only?");
        GlobalFree(buf);
        return 0;
    }
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE || !WriteFile(h, buf, size, &got, NULL) || got != size) {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        lstrcpyA(err, "Could not write the exe. Is the game running, or the file read-only?");
        GlobalFree(buf);
        return 0;
    }
    CloseHandle(h);
    GlobalFree(buf);
    return 1;
}

unsigned pe_import_mask(const char *path, const char *const *names, int n)
{
    DWORD size;
    unsigned char *buf = load(path, &size);
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *d;
    unsigned mask = 0;
    int i;
    if (!buf) return 0;
    d = imports(buf, size, &nt);
    if (d)
        for (; (unsigned char *)(d + 1) <= buf + size && d->Name; d++) {
            DWORD noff = rva2off(nt, d->Name, size);
            if (!noff) continue;
            for (i = 0; i < n; i++) if (ieq((char *)buf + noff, names[i])) mask |= 1u << i;
        }
    GlobalFree(buf);
    return mask;
}
