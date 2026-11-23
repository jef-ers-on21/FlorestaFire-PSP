# ============================================================
# Floresta Fire - Makefile (PSPSDK)
# ============================================================

TARGET = FlorestaFire
OBJS = main.o

# Bibliotecas necessárias
LIBS = -lpspgu -lpspgum -lpspge -lpspdisplay -lpspctrl -lpsppower -lpsprtc -lm -lc

# Flags de otimização para PSP
CFLAGS = -O2 -G0 -Wall -D_PSP_FW_VERSION=150
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)

# Título que aparece no XMB / PPSSPP
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Floresta Fire

# Caminho do SDK
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

# Alvos extras
clean:
	rm -f *.o $(TARGET).elf $(TARGET).prx EBOOT.PBP PARAM.SFO
