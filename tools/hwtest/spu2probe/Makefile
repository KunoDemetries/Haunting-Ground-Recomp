# IOP side of struct_capture's SPU2 envelope probe (ps2sdk). Built by ../Makefile's
# struct target, or alone: docker run --rm -v "<this folder>:/src" -w /src ps2dev/ps2dev:<tag> make
IOP_BIN = spu2probe.irx
IOP_OBJS = spu2probe.o imports.o
IOP_OBJS_DIR = obj/

all: $(IOP_BIN)

clean:
	rm -rf obj $(IOP_BIN)

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
