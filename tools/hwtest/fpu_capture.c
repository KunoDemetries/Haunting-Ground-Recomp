/* PS2 hardware capture of EE FPU and VU0 macro arithmetic results, for checking the
 * recompilation's float model bit for bit (docs/SOURCES.md, tools/fpu_vector_check.cpp).
 * Runs on a real PS2 (homebrew launcher), writes mc0:/HGFPU/RESULTS.BIN and shows
 * progress on screen. Built with ps2sdk (tools/hwtest/Makefile). Our own program.
 *
 * Record per case (12 little-endian words): a, b, c, mul(a,b), mul(b,a), div(a,b),
 * madd(acc=c; a*b), vu0 vmul.x(a,b), d, add(a,d), sub(a,d), sqrt(|a|), where d has an
 * exponent within 0..31 of a's (operand alignment cases). Inputs come from fixed
 * classes (below) and a xorshift generator, so the file is self-describing.
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
#define WORDS 12
static unsigned int out[CASES * WORDS] __attribute__((aligned(64)));

static unsigned int state = 0x2545F491u;
static unsigned int next(void) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

/* Inputs chosen to expose an inexact multiplier: exact products (few mantissa bits),
 * all-ones mantissas, single-bit and random patterns, both operand orders. */
static unsigned int pick(unsigned int kind) {
    unsigned int sign = next() & 0x80000000u;
    unsigned int exp = (100u + (next() % 56u)) << 23;   /* moderate exponents: no overflow */
    unsigned int r = next() & 0x7FFFFFu;
    switch (kind & 7u) {
    case 0: return sign | exp | r;                                  /* random mantissa */
    case 1: return sign | exp;                                      /* power of two */
    case 2: return sign | exp | 0x7FFFFFu;                          /* all-ones mantissa */
    case 3: return sign | exp | (r & 0x7F0000u);                    /* high bits only */
    case 4: return sign | exp | (r & 0x00FFFFu);                    /* low bits only */
    case 5: return sign | exp | (1u << (next() % 23u));             /* one fraction bit */
    case 6: return sign | exp | (0x7FFFFFu ^ (1u << (next() % 23u)));/* all but one */
    default: return sign | exp | (r | 0x7F8000u);                   /* high bits set */
    }
}

static unsigned int mul_s(unsigned int a, unsigned int b) {
    unsigned int r;
    __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n mul.s $f2, $f0, $f1\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2");
    return r;
}
static unsigned int div_s(unsigned int a, unsigned int b) {
    unsigned int r;
    __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n div.s $f2, $f0, $f1\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2");
    return r;
}
static unsigned int madd_s(unsigned int c, unsigned int a, unsigned int b) {
    unsigned int r;
    /* ACC = c + 0 (adda.s), then fd = ACC + a*b (madd.s). */
    __asm__ volatile("mtc1 %1, $f3\n mtc1 $zero, $f4\n adda.s $f3, $f4\n"
                     "mtc1 %2, $f0\n mtc1 %3, $f1\n madd.s $f2, $f0, $f1\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(c), "r"(a), "r"(b) : "$f0", "$f1", "$f2", "$f3", "$f4");
    return r;
}
static unsigned int add_s(unsigned int a, unsigned int b) {
    unsigned int r;
    __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n add.s $f2, $f0, $f1\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2");
    return r;
}
static unsigned int sub_s(unsigned int a, unsigned int b) {
    unsigned int r;
    __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n sub.s $f2, $f0, $f1\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2");
    return r;
}
static unsigned int sqrt_s(unsigned int a) {
    unsigned int r;
    __asm__ volatile("mtc1 %1, $f0\n sqrt.s $f2, $f0\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(a) : "$f0", "$f2");
    return r;
}
static unsigned int vu0_mul(unsigned int a, unsigned int b) {
    unsigned int r;
    /* VU0 macro: vf1.x = a, vf2.x = b, vf3 = vf1 * vf2.x (field x). */
    __asm__ volatile("qmtc2 %1, $vf1\n qmtc2 %2, $vf2\n vmulx.x $vf3, $vf1, $vf2x\n qmfc2 %0, $vf3"
                     : "=r"(r) : "r"(a), "r"(b));
    return r;
}

int main(void) {
    int i, fd, written;
    SifInitRpc(0);
    init_scr();
    scr_printf("HG FPU capture: %d cases\n", CASES);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);
    for (i = 0; i < CASES; ++i) {
        unsigned int a = pick(next()), b = pick(next()), c = pick(0);
        unsigned int *o = &out[i * WORDS];
        o[0] = a; o[1] = b; o[2] = c;
        o[3] = mul_s(a, b); o[4] = mul_s(b, a);
        o[5] = div_s(a, b); o[6] = madd_s(c, a, b);
        o[7] = vu0_mul(a, b);
        {   /* d: a's exponent minus 0..31, own sign and mantissa class */
            unsigned int ea = (a >> 23) & 255u, shift = next() % 32u;
            unsigned int d = (pick(next()) & 0x807FFFFFu) | ((ea > shift ? ea - shift : 1u) << 23);
            o[8] = d; o[9] = add_s(a, d); o[10] = sub_s(a, d); o[11] = sqrt_s(a & 0x7FFFFFFFu);
        }
    }
    scr_printf("computed; writing mc0:/HGFPU/RESULTS.BIN ...\n");
    /* libmc (MCMAN/MCSERV from the console ROM): every call completes at mcSync. */
    if (mcInit(MC_TYPE_MC) < 0) { scr_printf("ERROR: mcInit failed\n"); SleepThread(); }
    {
        int type = 0, freek = 0, format = 0, result = 0;
        mcGetInfo(0, 0, &type, &freek, &format); mcSync(0, NULL, &result);
        scr_printf("memory card slot 1: type %d, %d KB free, formatted %d\n", type, freek, format);
        mcMkDir(0, 0, "HGFPU"); mcSync(0, NULL, &result);   /* fails harmlessly if it exists */
        mcOpen(0, 0, "HGFPU/RESULTS.BIN", O_WRONLY | O_CREAT); mcSync(0, NULL, &fd);
        if (fd < 0) { scr_printf("ERROR: cannot open file on memory card slot 1 (%d)\n", fd); SleepThread(); }
        mcWrite(fd, out, sizeof out); mcSync(0, NULL, &written);
        mcClose(fd); mcSync(0, NULL, &result);
    }
    scr_printf(written == (int)sizeof out ? "DONE: %d bytes written. You can turn the console off.\n"
                                          : "ERROR: wrote %d bytes\n", written);
    SleepThread();
    return 0;
}
