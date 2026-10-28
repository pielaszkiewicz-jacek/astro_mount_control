# Astro Mount Controller — wygodny Makefile deweloperski.
# Właściwa kompilacja odbywa się przez CMake (zob. CMakeLists.txt).

SHELL := /bin/bash

# Katalog lokalny kontrolera w katalogu domowym użytkownika.
# Można go nadpisać zmienną środowiskową ASTRO_MOUNT_HOME.
ASTRO_MOUNT_HOME ?= $(HOME)/astro-mount-controller
LOCALUSER_DIR := $(ASTRO_MOUNT_HOME)

.PHONY: all build localuser localuser-clean clean

all: build

# Buduje projekt przez CMake i umieszcza binarki w build/bin.
build:
	@mkdir -p build
	@cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$$(nproc)

# Tworzy katalog kontrolera w katalogu domowym użytkownika i kopiuje do niego
# wszystkie wymagane zasoby: konfigurację, binarki, bazę danych, certyfikaty,
# skrypty oraz frontend webowy (bez node_modules i .env).
localuser:
	@echo "== Lokalna instalacja kontrolera =="
	@echo "  Katalog docelowy: $(LOCALUSER_DIR)"
	@mkdir -p "$(LOCALUSER_DIR)"/bin \
	          "$(LOCALUSER_DIR)"/config/mount \
	          "$(LOCALUSER_DIR)"/config/dome \
	          "$(LOCALUSER_DIR)"/config/derotator \
	          "$(LOCALUSER_DIR)"/config/focuser \
	          "$(LOCALUSER_DIR)"/config/power \
	          "$(LOCALUSER_DIR)"/config/sequencer \
	          "$(LOCALUSER_DIR)"/config/pec \
	          "$(LOCALUSER_DIR)"/config/st4guider \
	          "$(LOCALUSER_DIR)"/config/weather \
	          "$(LOCALUSER_DIR)"/config/pidcal \
	          "$(LOCALUSER_DIR)"/config/backup \
	          "$(LOCALUSER_DIR)"/data \
	          "$(LOCALUSER_DIR)"/logs \
	          "$(LOCALUSER_DIR)"/run \
	          "$(LOCALUSER_DIR)"/certs \
	          "$(LOCALUSER_DIR)"/scripts
	@if [ -d build/bin ]; then \
	    cp -v build/bin/astro_* "$(LOCALUSER_DIR)"/bin/ 2>/dev/null || true; \
	else \
	    echo "  UWAGA: brak build/bin — uruchom najpierw 'make build'."; \
	fi
	@if [ -d config ]; then \
	    for sub in mount dome derotator focuser power sequencer pec st4guider weather pidcal; do \
	        if [ -d "config/$$sub" ]; then \
	            find "config/$$sub" -maxdepth 1 -type f -name '*.json' \
	                ! -name '*[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]*' \
	                -exec cp -v {} "$(LOCALUSER_DIR)"/config/$$sub/ \; ; \
	        fi; \
	    done; \
	fi
	@if [ -d data ]; then cp -v data/* "$(LOCALUSER_DIR)"/data/ 2>/dev/null || true; fi
	@if [ -f astronomy_objects.db ]; then cp -v astronomy_objects.db "$(LOCALUSER_DIR)"/data/ 2>/dev/null || true; fi
	@if [ -d certs ]; then cp -v certs/* "$(LOCALUSER_DIR)"/certs/ 2>/dev/null || true; fi
	@if [ -d scripts ]; then cp -av scripts/. "$(LOCALUSER_DIR)"/scripts/ 2>/dev/null || true; fi
	@if [ -d proto ]; then \
	    mkdir -p "$(LOCALUSER_DIR)"/proto; \
	    cp -v proto/*.proto "$(LOCALUSER_DIR)"/proto/ 2>/dev/null || true; \
	fi
	@if [ -d db/proto ]; then \
	    mkdir -p "$(LOCALUSER_DIR)"/db/proto; \
	    cp -v db/proto/*.proto "$(LOCALUSER_DIR)"/db/proto/ 2>/dev/null || true; \
	fi
	@if [ -d dome/proto ]; then \
	    mkdir -p "$(LOCALUSER_DIR)"/dome/proto; \
	    cp -v dome/proto/*.proto "$(LOCALUSER_DIR)"/dome/proto/ 2>/dev/null || true; \
	fi
	@if [ -d derotator/proto ]; then \
	    mkdir -p "$(LOCALUSER_DIR)"/derotator/proto; \
	    cp -v derotator/proto/*.proto "$(LOCALUSER_DIR)"/derotator/proto/ 2>/dev/null || true; \
	fi
	@if [ -d web ]; then \
	    echo "  Kopiuję katalog web/ (bez node_modules i .env)..."; \
	    mkdir -p "$(LOCALUSER_DIR)"/web; \
	    tar --exclude='web/proxy/node_modules' --exclude='web/proxy/.env' \
	        -C . -cf - web | tar -C "$(LOCALUSER_DIR)" -xf -; \
	fi
	@echo ""
	@echo "== Gotowe =="
	@echo "  Katalog:      $(LOCALUSER_DIR)"
	@echo "  Binarki:      $(LOCALUSER_DIR)/bin"
	@echo "  Konfiguracja: $(LOCALUSER_DIR)/config"
	@echo "  Baza danych:  $(LOCALUSER_DIR)/data"
	@echo "  Skrypty:      $(LOCALUSER_DIR)/scripts"
	@echo "  Proto (gRPC): $(LOCALUSER_DIR)/proto"
	@echo ""
	@echo "  Uruchomienie: $(LOCALUSER_DIR)/scripts/astro_env.sh start"
	@echo "  Skrypt domyślnie używa tego katalogu (ASTRO_MOUNT_HOME)."
	@echo "  Inny katalog: export ASTRO_MOUNT_HOME=/sciezka/do/katalogu"

# Usuwa katalog lokalny kontrolera.
localuser-clean:
	@echo "Usuwam katalog lokalny: $(LOCALUSER_DIR)"
	@rm -rf "$(LOCALUSER_DIR)"

clean:
	@rm -rf build
