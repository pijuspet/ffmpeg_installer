SHELL         := /bin/bash

# ── Configurable variables ───────────────────────────────────────────────────
PATCH         ?= custom_ffmpeg.diff
CLONE_DIR     ?= ffmpeg
BRANCH        ?= release/8.0
JOBS          ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
PREFIX        ?= /usr/local

REPO_URL      := https://github.com/ffmpeg/ffmpeg.git

deps:
	@echo "[INFO]  Checking dependencies..."
	@command -v git        >/dev/null 2>&1 || { echo "[ERROR] git not found";        exit 1; }
	@command -v make       >/dev/null 2>&1 || { echo "[ERROR] make not found";       exit 1; }
	@command -v gcc        >/dev/null 2>&1 || { echo "[ERROR] gcc not found";        exit 1; }
	@command -v patch      >/dev/null 2>&1 || { echo "[ERROR] patch not found";      exit 1; }
	@command -v pkg-config >/dev/null 2>&1 || { echo "[ERROR] pkg-config not found"; exit 1; }
	@echo "[OK]    All dependencies found."

clone: deps
	@echo "[INFO]  Cloning FFmpeg from $(REPO_URL)..."
	@if [ -d "$(CLONE_DIR)/.git" ]; then \
		echo "[WARN]  $(CLONE_DIR) exists — resetting."; \
		cd $(CLONE_DIR) && git fetch --all --tags --prune && git checkout $(BRANCH) && git reset --hard origin/$(BRANCH) && git clean -fdx; \
	else \
		git clone -b $(BRANCH) $(REPO_URL) $(CLONE_DIR); \
	fi
	@echo "[OK]    Clone complete."

patch: clone
	@cd $(CLONE_DIR) && for p in ../$(PATCH); do \
		name=$$(basename $$p); \
		echo "[INFO]  Applying patch: $$name..."; \
		if patch -p1 --forward --force < $$p; then \
			echo "[OK]    $$name applied cleanly."; \
		elif patch -p1 --reverse --batch --dry-run < $$p >/dev/null 2>&1; then \
			echo "[WARN]  $$name already applied — skipping."; \
		else \
			REJECTS=$$(find . -name '*.rej' ! -path './doc/*'); \
			if [ -n "$$REJECTS" ]; then \
				echo "[ERROR] Critical hunks rejected in $$name:"; \
				echo "$$REJECTS"; \
				exit 1; \
			fi; \
			echo "[WARN]  $$name applied with non-critical skips."; \
		fi; \
		rm -f $$(find . -name '*.rej' -o -name '*.orig') 2>/dev/null; \
	done

configure: patch
	@echo "[INFO]  Running ./configure..."
	@cd $(CLONE_DIR) && ./configure \
		$(if $(PREFIX),--prefix=$(PREFIX)) \
		$(CONFIGURE_OPTS)
	@echo "[OK]    Configure complete."

build: configure
	@echo "[INFO]  Building FFmpeg with $(JOBS) jobs..."
	@$(MAKE) -C $(CLONE_DIR) -j$(JOBS)
	@echo "[OK]    Build complete."

install:
	@echo "[INFO]  Installing to $(PREFIX)..."
	@$(MAKE) -C $(CLONE_DIR) install
	@echo "[OK]    Installed to $(PREFIX)."

verify:
	@$(CLONE_DIR)/ffmpeg -version 2>/dev/null | head -1

clean:
	@echo "[INFO]  Cleaning build artifacts..."
	@if [ -f "$(CLONE_DIR)/Makefile" ]; then \
		$(MAKE) -C $(CLONE_DIR) clean 2>/dev/null || true; \
	fi
	@echo "[OK]    Clean complete."

distclean:
	@echo "[INFO]  Removing $(CLONE_DIR)/..."
	@rm -rf $(CLONE_DIR)
	@echo "[OK]    Distclean complete."