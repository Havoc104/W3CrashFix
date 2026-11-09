// Guard stub shared by the DLL (fix 2) and src/test_stub.c.
// One stub block is ID_STUB_SIZE bytes (executable + writable): code at +0x00, then counters at
// +ID_ZERO (id == 0), +ID_OTHER (other out-of-range ids), +ID_LAST (dword, last out-of-range nonzero id),
// +ID_MAXSEEN (dword, largest out-of-range id seen, sampled by id_stub_note_max).
// Behaviour: the guarded register (edx, or r8d) passes unchanged when it is -1 or within 1..max;
// anything else is counted and replaced by -1, then the stub jumps to orig.
// Only eax, the guarded register and flags are touched: every other argument reaches orig unchanged.
#pragma once
#include <windows.h>
#include <string.h>

enum { ID_STUB_SIZE = 0x60, ID_ZERO = 0x48, ID_OTHER = 0x50, ID_LAST = 0x58, ID_MAXSEEN = 0x5C };
enum { ID_REG_EDX = 0, ID_REG_R8D = 1 };

static void put32(unsigned char *p, int v) { memcpy(p, &v, 4); }

static void build_id_stub(unsigned char *t, void *orig, unsigned max, int reg) {
    memset(t, 0, ID_STUB_SIZE);
    if (reg == ID_REG_EDX) {
        unsigned char s[0x36] = {
            0x85, 0xD2,                         // 00 test edx, edx
            0x74, 0x1F,                         // 02 je   zero (0x23)
            0x83, 0xFA, 0xFF,                   // 04 cmp  edx, -1
            0x74, 0x27,                         // 07 je   pass (0x30)
            0x8D, 0x42, 0xFF,                   // 09 lea  eax, [rdx-1]
            0x3D, 0, 0, 0, 0,                   // 0C cmp  eax, max
            0x72, 0x1D,                         // 11 jb   pass (0x30)
            0xF0, 0x48, 0xFF, 0x05, 0, 0, 0, 0, // 13 lock inc qword [rip+d] -> OTHER
            0x89, 0x15, 0, 0, 0, 0,             // 1B mov  [rip+d], edx      -> LAST
            0xEB, 0x08,                         // 21 jmp  setneg (0x2B)
            0xF0, 0x48, 0xFF, 0x05, 0, 0, 0, 0, // 23 zero: lock inc qword [rip+d] -> ZERO
            0xBA, 0xFF, 0xFF, 0xFF, 0xFF,       // 2B setneg: mov edx, -1
            0xFF, 0x25, 0, 0, 0, 0,             // 30 pass: jmp [rip+0] -> orig (at 0x36)
        };
        put32(s + 0x0D, (int)max);
        put32(s + 0x17, ID_OTHER - 0x1B);
        put32(s + 0x1D, ID_LAST - 0x21);
        put32(s + 0x27, ID_ZERO - 0x2B);
        memcpy(t, s, sizeof s);
        memcpy(t + 0x36, &orig, 8);
    } else {
        unsigned char s[0x3B] = {
            0x45, 0x85, 0xC0,                   // 00 test r8d, r8d
            0x74, 0x22,                         // 03 je   zero (0x27)
            0x41, 0x83, 0xF8, 0xFF,             // 05 cmp  r8d, -1
            0x74, 0x2A,                         // 09 je   pass (0x35)
            0x41, 0x8D, 0x40, 0xFF,             // 0B lea  eax, [r8-1]
            0x3D, 0, 0, 0, 0,                   // 0F cmp  eax, max
            0x72, 0x1F,                         // 14 jb   pass (0x35)
            0xF0, 0x48, 0xFF, 0x05, 0, 0, 0, 0, // 16 lock inc qword [rip+d] -> OTHER
            0x44, 0x89, 0x05, 0, 0, 0, 0,       // 1E mov  [rip+d], r8d      -> LAST
            0xEB, 0x08,                         // 25 jmp  setneg (0x2F)
            0xF0, 0x48, 0xFF, 0x05, 0, 0, 0, 0, // 27 zero: lock inc qword [rip+d] -> ZERO
            0x41, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, // 2F setneg: mov r8d, -1
            0xFF, 0x25, 0, 0, 0, 0,             // 35 pass: jmp [rip+0] -> orig (at 0x3B)
        };
        put32(s + 0x10, (int)max);
        put32(s + 0x1A, ID_OTHER - 0x1E);
        put32(s + 0x21, ID_LAST - 0x25);
        put32(s + 0x2B, ID_ZERO - 0x2F);
        memcpy(t, s, sizeof s);
        memcpy(t + 0x3B, &orig, 8);
    }
}

// track the largest out-of-range id from outside the hot path (called from the 30 s summary)
static void id_stub_note_max(unsigned char *t) {
    unsigned last = *(volatile unsigned *)(t + ID_LAST);
    unsigned *mx = (unsigned *)(t + ID_MAXSEEN);
    if (last > *mx) *mx = last;
}
