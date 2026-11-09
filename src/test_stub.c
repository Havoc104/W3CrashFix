// Unit test for the fix-2 guard stubs (same builder as the DLL, from idstub.h), both register forms.
#include <windows.h>
#include <stdio.h>
#include "idstub.h"

static int s_edx, s_r8, s_r9, s_stack;
__attribute__((noinline)) void target(void *self, int edx, int r8, int r9, int stack5) {
    (void)self; s_edx = edx; s_r8 = r8; s_r9 = r9; s_stack = stack5;
}
typedef void (*fn)(void *, int, int, int, int);

static int run(int reg) {
    unsigned char *t = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    build_id_stub(t, (void *)target, 234276, reg);
    fn f = (fn)t;
    volatile LONG64 *zero = (volatile LONG64 *)(t + ID_ZERO), *other = (volatile LONG64 *)(t + ID_OTHER);
    volatile unsigned *last = (volatile unsigned *)(t + ID_LAST);
    struct { int in, want; long long z, o; unsigned last; } c[] = {
        {0, -1, 1, 0, 0},
        {0x40666AEE, -1, 1, 1, 0x40666AEE},
        {1, 1, 1, 1, 0x40666AEE}, {5, 5, 1, 1, 0x40666AEE}, {234276, 234276, 1, 1, 0x40666AEE},
        {234277, -1, 1, 2, 234277},
        {-1, -1, 1, 2, 234277},
        {0, -1, 2, 2, 234277},
        {-5, -1, 2, 3, 0xFFFFFFFB},
    };
    int ok = 1;
    printf("-- guarding %s\n", reg == ID_REG_EDX ? "edx" : "r8d");
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        // the guarded register gets the test id; the other one carries a marker that must arrive untouched
        if (reg == ID_REG_EDX) f(NULL, c[i].in, 0, 22, 33); else f(NULL, 0, c[i].in, 22, 33);
        int got = reg == ID_REG_EDX ? s_edx : s_r8, untouched = reg == ID_REG_EDX ? s_r8 : s_edx;
        int pass = got == c[i].want && untouched == 0 && s_r9 == 22 && s_stack == 33 &&
                   *zero == c[i].z && *other == c[i].o && *last == c[i].last;
        printf("id %-11d -> %-7d other-reg %d zero %lld out %lld last 0x%X  %s\n",
               c[i].in, got, untouched, *zero, *other, *last, pass ? "ok" : "FAIL");
        ok &= pass;
    }
    id_stub_note_max(t);
    ok &= *(unsigned *)(t + ID_MAXSEEN) == 0xFFFFFFFB;
    return ok;
}

int main(void) {
    int ok = run(ID_REG_EDX) & run(ID_REG_R8D);
    puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
