# PS2 hardware capture program (ps2sdk). Build inside the ps2dev toolchain container:
#   docker run --rm -v "<this folder>:/src" -w /src ps2dev/ps2dev:<tag> make
# Other programs: make EE_BIN=vu0_capture.elf EE_OBJS=vu0_capture.o (or struct_capture);
# strip with mips64r5900el-ps2-elf-strip before copying to the console.
EE_BIN = fpu_capture.elf
EE_OBJS = fpu_capture.o
EE_LIBS = -ldebug -lmc -lpatches
EE_CFLAGS += -O1

all: $(EE_BIN)

# struct_capture embeds the IOP probe: make EE_BIN=struct_capture.elf EE_OBJS="struct_capture.o spu2probe_irx.o"
spu2probe/spu2probe.irx:
	$(MAKE) -C spu2probe
spu2probe_irx.c: spu2probe/spu2probe.irx
	bin2c $< $@ spu2probe_irx

clean:
	rm -f $(EE_BIN) $(EE_OBJS) spu2probe_irx.c
	$(MAKE) -C spu2probe clean

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
