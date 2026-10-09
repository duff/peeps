.PHONY: all test clean install

PREFIX ?= $(HOME)/.local
CONFIGURE = cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$(PREFIX)"
BUILD = cmake --build build --parallel $(shell nproc)

all:
	$(CONFIGURE)
	$(BUILD) --target contacts

test:
	$(CONFIGURE)
	$(BUILD) --target contacts-tests
	ctest --test-dir build --output-on-failure

# DESTDIR stages a package. Without it, the launcher points at the installed
# binary so it works even when ~/.local/bin is not on the launcher's PATH.
APPS = $(PREFIX)/share/applications

install: all
	DESTDIR="$(DESTDIR)" cmake --install build
	sed -i 's|^Exec=.*|Exec=$(PREFIX)/bin/contacts|' "$(DESTDIR)$(APPS)/org.omelia.Contacts.desktop"
	@# Builds before the org.omelia.Contacts app id installed a plain contacts.desktop.
	-[ -n "$(DESTDIR)" ] || ! grep -qs '^Exec=$(PREFIX)/bin/contacts$$' "$(APPS)/contacts.desktop" || \
		rm -f "$(APPS)/contacts.desktop" "$(PREFIX)/share/icons/hicolor/scalable/apps/contacts.svg"
	-[ -n "$(DESTDIR)" ] || update-desktop-database "$(APPS)" >/dev/null 2>&1 || true

clean:
	rm -rf build
