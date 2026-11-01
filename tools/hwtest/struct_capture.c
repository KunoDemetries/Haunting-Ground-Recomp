/* Third PS2 hardware capture: structured operands for fitting the EE FPU divider,
 * square root and multiplier (the random captures RESULTS.BIN / VU0.BIN showed the
 * rules but not their internals; docs/SOURCES.md). Fixed divisors with consecutive
 * dividends, consecutive radicands, and all-ones x sparse multiplier patterns.
 * The FPU and VU0 share one arithmetic (VU0.BIN), so only the FPU is used.
 * Writes mc0:/HGFPU/STRUCT.BIN. Built with ps2sdk (tools/hwtest/Makefile). Our own program.
 *
 * Record (4 little-endian words): op, a, b, result. op 1 div.s a/b, 2 sqrt.s a,
 * 3 mul.s a*b, 4 rsqrt.s a/sqrt(b), 0 unused. Operands are positive with exponent
 * 127 or 128, so no overflow, underflow or sign handling is involved.
 * Then the SPU2 envelope probe (spu2probe/spu2probe.c, our IOP module run with the ROM's
 * LIBSD): op 5 key-on / 6 key-off samples {op, ADSR1<<16|ADSR2, microseconds, ENVX};
 * op 7 {7, load result, module result, 0} if the module could not run.
 */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libmc.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sbv_patches.h>

extern unsigned char spu2probe_irx[];
extern unsigned int size_spu2probe_irx;
#define SPU_RECORDS (9 * (100 + 64))   /* must match spu2probe/spu2probe.c */
static unsigned int spu_buf[SPU_RECORDS * 4] __attribute__((aligned(64)));

#define RECORDS 65536
static unsigned int out[RECORDS * 4] __attribute__((aligned(64)));
static int used = 0;

#define ONE 0x3F800000u   /* exponent 127 */
#define TWO 0x40000000u   /* exponent 128 */
#define M(m) ((m) & 0x7FFFFFu)

static unsigned int div_s(unsigned int a, unsigned int b) {
    unsigned int r;
    __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n div.s $f2, $f0, $f1\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2");
    return r;
}
static unsigned int mul_s(unsigned int a, unsigned int b) {
    unsigned int r;
    __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n mul.s $f2, $f0, $f1\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2");
    return r;
}
static unsigned int sqrt_s(unsigned int a) {
    unsigned int r;
    __asm__ volatile("mtc1 %1, $f0\n sqrt.s $f2, $f0\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(a) : "$f0", "$f2");
    return r;
}
static unsigned int rsqrt_s(unsigned int a, unsigned int b) {
    unsigned int r;
    __asm__ volatile("mtc1 %1, $f0\n mtc1 %2, $f1\n rsqrt.s $f2, $f0, $f1\n mfc1 %0, $f2"
                     : "=r"(r) : "r"(a), "r"(b) : "$f0", "$f1", "$f2");
    return r;
}

static void put(unsigned int op, unsigned int a, unsigned int b) {
    unsigned int *o;
    if (used >= RECORDS) return;
    o = &out[used++ * 4];
    o[0] = op; o[1] = a; o[2] = b;
    o[3] = op == 1 ? div_s(a, b) : op == 2 ? sqrt_s(a) : op == 3 ? mul_s(a, b) : rsqrt_s(a, b);
}

static const unsigned int divisors[8] = {
    0xA00000u, 0x900000u, 0xC00000u, 0xB00000u, 0x880000u, 0xFFFFFFu, 0x800001u, 0xE00000u };

int main(void) {
    int fd = -1, written = 0;
    unsigned int i, j, k;
    SifInitRpc(0);
    init_scr();
    scr_printf("HG structured FPU capture\n");
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);

    /* DIV: fixed divisors, consecutive dividends (three dividend windows). */
    for (i = 0; i < 8; ++i) {
        unsigned int b = ONE | M(divisors[i]);
        for (k = 0; k < 1024; ++k) put(1, ONE | M(0x800000u + k), b);
        for (k = 0; k < 512; ++k) put(1, ONE | M(0xC66000u + k), b);
        for (k = 0; k < 1024; ++k) put(1, ONE | M(0xFFFC00u + k), b);
    }
    /* DIV: fixed dividends, consecutive divisors. */
    {
        static const unsigned int dividends[3] = { 0x800000u, 0xC6605Du, 0xFFFFFFu };
        for (i = 0; i < 3; ++i)
            for (k = 0; k < 1024; ++k) {
                put(1, ONE | M(dividends[i]), ONE | M(0x800000u + k));
                put(1, ONE | M(dividends[i]), ONE | M(0xA00000u + k));
            }
    }
    /* SQRT: consecutive radicands, even and odd unbiased exponent. */
    for (k = 0; k < 4096; ++k) { put(2, ONE | M(0x800000u + k), 0); put(2, TWO | M(0x800000u + k), 0); }
    for (k = 0; k < 2048; ++k) { put(2, ONE | M(0xC00000u + k), 0); put(2, TWO | M(0xC00000u + k), 0); }
    /* MUL: all-ones times sparse patterns (hidden bit plus up to two fraction bits), both orders. */
    put(3, ONE | 0x7FFFFFu, ONE); put(3, ONE, ONE | 0x7FFFFFu);
    for (j = 0; j < 23; ++j)
        for (k = j; k < 23; ++k) {
            unsigned int b = ONE | (1u << j) | (1u << k);
            put(3, ONE | 0x7FFFFFu, b); put(3, b, ONE | 0x7FFFFFu);
        }
    /* MUL: consecutive mantissas times all-ones, both orders. */
    for (k = 0; k < 4096; ++k) {
        put(3, ONE | M(0x800000u + k), ONE | 0x7FFFFFu);
        put(3, ONE | 0x7FFFFFu, ONE | M(0x800000u + k));
    }
    /* MUL: near-all-ones pairs. */
    for (j = 0; j < 64; ++j)
        for (k = 0; k < 64; ++k) put(3, ONE | M(0xFFFFFFu - j), ONE | M(0xFFFFFFu - k));
    /* MUL: single fraction bit times consecutive mantissas (partial-product alignment). */
    for (j = 0; j < 23; ++j)
        for (k = 0; k < 128; ++k) put(3, ONE | (1u << j), ONE | M(0xFFFF80u + k));
    /* RSQRT: 1/sqrt(b) for consecutive b, both exponent parities. */
    for (k = 0; k < 2048; ++k) { put(4, ONE, ONE | M(0x800000u + k)); put(4, ONE, TWO | M(0x800000u + k)); }

    /* SPU2 envelope probe (about 25 s; the voice is silent). */
    {
        char arg[16];
        int load, result = 0;
        scr_printf("%d float records; SPU2 envelope probe (about 25 s) ...\n", used);
        sbv_patch_enable_lmb();
        sbv_patch_disable_prefix_check();
        SifLoadModule("rom0:LIBSD", 0, NULL);
        memset(spu_buf, 0, sizeof spu_buf);
        FlushCache(0);
        sprintf(arg, "%x", (unsigned int)spu_buf & 0x1FFFFFFFu);
        load = SifExecModuleBuffer(spu2probe_irx, size_spu2probe_irx, strlen(arg) + 1, arg, &result);
        InvalidDCache(spu_buf, (char *)spu_buf + sizeof spu_buf - 1);
        if (load < 0 || spu_buf[0] != 5) {
            unsigned int *o = &out[used++ * 4];
            o[0] = 7; o[1] = (unsigned int)load; o[2] = (unsigned int)result; o[3] = 0;
            scr_printf("SPU2 probe did not run (load %d, result %d)\n", load, result);
        } else {
            memcpy(&out[used * 4], spu_buf, sizeof spu_buf);
            used += SPU_RECORDS;
        }
    }
    scr_printf("%d records computed; writing mc0:/HGFPU/STRUCT.BIN ...\n", used);
    if (mcInit(MC_TYPE_MC) < 0) { scr_printf("ERROR: mcInit failed\n"); SleepThread(); }
    {
        int type = 0, freek = 0, format = 0, result = 0;
        mcGetInfo(0, 0, &type, &freek, &format); mcSync(0, NULL, &result);
        scr_printf("memory card slot 1: type %d, %d KB free, formatted %d\n", type, freek, format);
        mcMkDir(0, 0, "HGFPU"); mcSync(0, NULL, &result);
        mcOpen(0, 0, "HGFPU/STRUCT.BIN", O_WRONLY | O_CREAT); mcSync(0, NULL, &fd);
        if (fd < 0) { scr_printf("ERROR: cannot open file on memory card slot 1 (%d)\n", fd); SleepThread(); }
        mcWrite(fd, out, sizeof out); mcSync(0, NULL, &written);
        mcClose(fd); mcSync(0, NULL, &result);
    }
    scr_printf(written == (int)sizeof out ? "DONE: %d bytes written. You can turn the console off.\n"
                                          : "ERROR: wrote %d bytes\n", written);
    SleepThread();
    return 0;
}
