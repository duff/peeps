.PHONY: all test clean install

PREFIX ?= $(HOME)/.local
CONFIGURE = cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$(PREFIX)"
BUILD = cmake --build build --parallel $(shell nproc)

all:
	$(CONFIGURE)
	$(BUILD) --target peeps

test:
	$(CONFIGURE)
	$(BUILD) --target peeps-tests
	ctest --test-dir build --output-on-failure

# DESTDIR stages a package. Without it, the launcher points at the installed
# binary so it works even when ~/.local/bin is not on the launcher's PATH.
APPS = $(PREFIX)/share/applications

install: all
	DESTDIR="$(DESTDIR)" cmake --install build
	sed -i 's|^Exec=.*|Exec=$(PREFIX)/bin/peeps|' "$(DESTDIR)$(APPS)/org.omelia.Peeps.desktop"
	@# Builds before the rename to Peeps installed Contacts.
	-[ -n "$(DESTDIR)" ] || ! grep -qs '^Exec=$(PREFIX)/bin/contacts$$' "$(APPS)/org.omelia.Contacts.desktop" || \
		rm -f "$(APPS)/org.omelia.Contacts.desktop" "$(PREFIX)/share/icons/hicolor/scalable/apps/org.omelia.Contacts.svg" \
			"$(PREFIX)/bin/contacts"
	-[ -n "$(DESTDIR)" ] || update-desktop-database "$(APPS)" >/dev/null 2>&1 || true

clean:
	rm -rf build
