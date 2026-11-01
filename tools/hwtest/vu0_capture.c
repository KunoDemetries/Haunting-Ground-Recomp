/* Second PS2 hardware capture: VU0 macro-mode arithmetic (add, sub, multiply-add, DIV,
 * SQRT, RSQRT through Q), the EE FPU RSQRT.S, and more MUL/DIV samples for the
 * multiplier model. Writes mc0:/HGFPU/VU0.BIN. Built with ps2sdk (tools/hwtest/Makefile).
 * Our own program.
 *
 * Record per case (16 little-endian words):
 *  0 a   1 b   2 c
 *  3 vu0 vadd.x(a,b)     4 vu0 vsub.x(a,b)      5 vu0 vmadd.x (ACC=c, + a*b)
 *  6 vu0 vmul.x(a,b)     7 vu0 Q after vdiv(a,b) 8 Q after vsqrt(|b|)
 *  9 Q after vrsqrt(a,|b|)  10 fpu rsqrt.s(a,|b|)  11 fpu mul(a,b)  12 fpu mul(b,a)
 * 13 fpu div(a,b)        14 vu0 vadd.x(c, a*b product register)  15 0
 */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libmc.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#define CASES 16384
#define WORDS 16
static unsigned int out[CASES * WORDS] __attribute__((aligned(64)));

static unsigned int state = 0x9E3779B9u;   /* a different stream from fpu_capture */
static unsigned int next(void) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}
static unsigned int pick(unsigned int kind) {
    unsigned int sign = next() & 0x80000000u;
    unsigned int exp = (100u + (next() % 56u)) << 23;
    unsigned int r = next() & 0x7FFFFFu;
    switch (kind & 7u) {
    case 0: return sign | exp | r;
    case 1: return sign | exp;
    case 2: return sign | exp | 0x7FFFFFu;
    case 3: return sign | exp | (r & 0x7F0000u);
    case 4: return sign | exp | (r & 0x00FFFFu);
    case 5: return sign | exp | (1u << (next() % 23u));
    case 6: return sign | exp | (0x7FFFFFu ^ (1u << (next() % 23u)));
    default: return sign | exp | (r | 0x7F8000u);
    }
}

static unsigned int fpu2(unsigned int op, unsigned int a, unsigned int b) {
    unsigned int r = 0;
    switch (op) {
    case 0: __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n mul.s $f2, $f0, $f1\n mfc1 %0, $f2"
                             : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2"); break;
    case 1: __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n div.s $f2, $f0, $f1\n mfc1 %0, $f2"
                             : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2"); break;
    default: __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n rsqrt.s $f2, $f0, $f1\n mfc1 %0, $f2"
                              : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2"); break;
    }
    return r;
}

/* VU0 macro mode: x fields only. vf1 = a, vf2 = b, vf4 = c. */
static void vu0(unsigned int a, unsigned int b, unsigned int c, unsigned int *o) {
    unsigned int add, sub, madd, mul, qdiv, qsqrt, qrsqrt, addprod;
    __asm__ volatile(
        "qmtc2 %8, $vf1\n qmtc2 %9, $vf2\n qmtc2 %10, $vf4\n"
        "vadd.x $vf3, $vf1, $vf2\n qmfc2 %0, $vf3\n"
        "vsub.x $vf3, $vf1, $vf2\n qmfc2 %1, $vf3\n"
        "vmulaw.x $ACC, $vf4, $vf0w\n"          /* ACC.x = c * 1.0 */
        "vmaddx.x $vf3, $vf1, $vf2x\n qmfc2 %2, $vf3\n"
        "vmulx.x $vf5, $vf1, $vf2x\n qmfc2 %3, $vf5\n"
        "vadd.x $vf6, $vf4, $vf5\n qmfc2 %7, $vf6\n"
        "vdiv $Q, $vf1x, $vf2x\n vwaitq\n cfc2 %4, $vi22\n"
        "vsqrt $Q, $vf2x\n vwaitq\n cfc2 %5, $vi22\n"
        "vrsqrt $Q, $vf1x, $vf2x\n vwaitq\n cfc2 %6, $vi22\n"
        : "=r"(add), "=r"(sub), "=r"(madd), "=r"(mul), "=r"(qdiv), "=r"(qsqrt), "=r"(qrsqrt), "=r"(addprod)
        : "r"(a), "r"(b), "r"(c));
    o[3] = add; o[4] = sub; o[5] = madd; o[6] = mul;
    o[7] = qdiv; o[8] = qsqrt; o[9] = qrsqrt; o[14] = addprod;
}

int main(void) {
    int i, fd = -1, written = 0;
    SifInitRpc(0);
    init_scr();
    scr_printf("HG VU0 capture: %d cases\n", CASES);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);
    for (i = 0; i < CASES; ++i) {
        unsigned int a = pick(next()), b = pick(next()), c = pick(next());
        unsigned int *o = &out[i * WORDS];
        o[0] = a; o[1] = b; o[2] = c;
        vu0(a, b & 0x7FFFFFFFu, c, o);   /* |b| so SQRT/RSQRT see a positive radicand */
        o[10] = fpu2(2, a, b & 0x7FFFFFFFu);
        o[11] = fpu2(0, a, b); o[12] = fpu2(0, b, a); o[13] = fpu2(1, a, b);
        o[15] = 0;
    }
    scr_printf("computed; writing mc0:/HGFPU/VU0.BIN ...\n");
    if (mcInit(MC_TYPE_MC) < 0) { scr_printf("ERROR: mcInit failed\n"); SleepThread(); }
    {
        int type = 0, freek = 0, format = 0, result = 0;
        mcGetInfo(0, 0, &type, &freek, &format); mcSync(0, NULL, &result);
        scr_printf("memory card slot 1: type %d, %d KB free, formatted %d\n", type, freek, format);
        mcMkDir(0, 0, "HGFPU"); mcSync(0, NULL, &result);
        mcOpen(0, 0, "HGFPU/VU0.BIN", O_WRONLY | O_CREAT); mcSync(0, NULL, &fd);
        if (fd < 0) { scr_printf("ERROR: cannot open file on memory card slot 1 (%d)\n", fd); SleepThread(); }
        mcWrite(fd, out, sizeof out); mcSync(0, NULL, &written);
        mcClose(fd); mcSync(0, NULL, &result);
    }
    scr_printf(written == (int)sizeof out ? "DONE: %d bytes written. You can turn the console off.\n"
                                          : "ERROR: wrote %d bytes\n", written);
    SleepThread();
    return 0;
}
