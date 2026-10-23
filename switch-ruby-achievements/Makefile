#---------------------------------------------------------------------------------
# Ruby Achievements - build completo
#   make          compila sysmodule, overlay e app NRO
#   make dist     monta dist/ pronto para copiar na raiz do SD
#   make test     testes de PC (g++ do MSYS2; serviços do Switch simulados em tests/shim)
#---------------------------------------------------------------------------------
TID	:=	0100000000007A11
DIST	:=	dist

.PHONY: all patches sysmodule overlay app dist clean test

test:
	@bash tests/run_tests.sh

all: sysmodule overlay app

# Patches locais do Borealis e do libtesla (submódulos no commit do upstream).
# Aplica só se ainda não estiverem aplicados; um clone novo sem eles compila,
# mas o app fecha no console (bloco de comandos do deko3d).
# --ignore-whitespace: o git do Windows (autocrlf) deixa as libs em CRLF e o do MSYS2 não converte.
define apply_patch
	@if git -C $(1) apply --ignore-whitespace --reverse --check $(CURDIR)/patches/$(2) 2>/dev/null; then :; \
	else git -C $(1) apply --ignore-whitespace $(CURDIR)/patches/$(2) && echo "patch aplicado: $(2)"; fi
endef

patches:
	$(call apply_patch,app_nro/lib/borealis,borealis.patch)
	$(call apply_patch,overlay/lib/libtesla,libtesla.patch)

sysmodule:
	@$(MAKE) --no-print-directory -C sysmodule

overlay: patches
	@$(MAKE) --no-print-directory -C overlay

app: patches
	@$(MAKE) --no-print-directory -C app_nro

dist: all
	@rm -rf $(DIST)
	@cp -r sd_card_structure $(DIST)
	@find $(DIST) -name .gitkeep -delete
	@cp sysmodule/ruby-achievements-sysmodule.nsp $(DIST)/atmosphere/contents/$(TID)/exefs.nsp
	@cp overlay/ruby-achievements-overlay.ovl $(DIST)/switch/.overlays/ruby-achievements-overlay.ovl
	@cp app_nro/ruby_achievements.nro $(DIST)/switch/ruby_achievements/ruby_achievements.nro
	@echo "SD pronto em $(DIST)/"

clean:
	@$(MAKE) --no-print-directory -C sysmodule clean
	@$(MAKE) --no-print-directory -C overlay clean
	@$(MAKE) --no-print-directory -C app_nro clean
	@rm -rf $(DIST)
