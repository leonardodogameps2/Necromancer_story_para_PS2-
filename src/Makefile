EE_BIN = necro.elf
EE_OBJS = src/main.o
EE_INCS += -Isrc
EE_LIBS = -lgskit -ldmakit -lpad -lc -lkernel
all: $(EE_BIN)
clean:
	rm -f $(EE_BIN) $(EE_OBJS)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
