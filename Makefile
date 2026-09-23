# Build locally, then install a user-owned version ahead of the distro package.
BUILD_DIR ?= build
PREFIX ?= $(HOME)/.local
LRELEASE ?= $(abspath $(BUILD_DIR)/private-linguist/root/usr/lib/qt6/bin/lrelease)

.PHONY: all install check-linux

all: check-linux
	CANTATA_BUILD_DIR="$(abspath $(BUILD_DIR))" CANTATA_INSTALL_PREFIX="$(abspath $(PREFIX))" ./mybuild.sh
	@if test -x "$(LRELEASE)"; then \
		mkdir -p "$(abspath $(BUILD_DIR))/translations"; \
		for ts in translations/cantata_*.ts; do \
			name=$${ts##*/}; \
			"$(LRELEASE)" -silent "$$ts" -qm "$(abspath $(BUILD_DIR))/translations/$${name%.ts}.qm" || exit $$?; \
		done; \
	else \
		echo "Qt6 lrelease not found: set LRELEASE=/path/to/lrelease to build UI translations" >&2; \
	fi

check-linux:
	@test "$(shell uname -s)" = Linux || { echo "This Makefile targets Linux; use ./mybuild.sh on macOS" >&2; exit 1; }

install: all
	@test -f "$(abspath $(BUILD_DIR))/translations/cantata_zh_CN.qm" || { echo "Chinese UI translation is missing; provide Qt6 lrelease" >&2; exit 1; }
	cmake --install "$(abspath $(BUILD_DIR))" --prefix "$(abspath $(PREFIX))"
	@install -d "$(abspath $(PREFIX))/share/Cantata/translations"
	@for qm in "$(abspath $(BUILD_DIR))"/translations/cantata_*.qm; do \
		install -m 644 "$$qm" "$(abspath $(PREFIX))/share/Cantata/translations/" || exit $$?; \
	done
	@printf 'Installed: %s/bin/cantata\n' "$(abspath $(PREFIX))"
	@printf 'For an already-open zsh terminal, run: rehash\n'
