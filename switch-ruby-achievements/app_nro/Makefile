#---------------------------------------------------------------------------------
# Ruby Achievements - app NRO (gera ruby_achievements.nro)
# O Borealis (fork xfangfang) só oferece build via CMake; este Makefile chama o
# CMake com o toolchain do devkitPro para manter `make` como interface comum.
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Defina DEVKITPRO no ambiente. export DEVKITPRO=<caminho>/devkitpro")
endif

BUILD	:=	build
JOBS	?=	$(shell nproc 2>/dev/null || echo 4)

.PHONY: all clean configure

all: configure
	@$(MAKE) --no-print-directory -C $(BUILD) ruby_achievements.nro -j$(JOBS)
	@cp $(BUILD)/ruby_achievements.nro ruby_achievements.nro
	@echo "built ... ruby_achievements.nro"

configure:
	@test -f lib/borealis/library/CMakeLists.txt || { echo "Falta lib/borealis - rode: git submodule update --init --recursive"; exit 1; }
	@cmake -S . -B $(BUILD) -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_DEPENDS_USE_COMPILER=OFF

clean:
	@rm -rf $(BUILD) ruby_achievements.nro
