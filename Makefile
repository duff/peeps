.PHONY: all test clean install

PREFIX ?= $(HOME)/.local

all:
	mkdir -p build
	cd build && qmake6 PREFIX=$(PREFIX) ../contacts.pro && $(MAKE)

test:
	mkdir -p build/tests
	cd build/tests && qmake6 ../../tests/tests.pro && $(MAKE)
	QT_QPA_PLATFORM=offscreen ./build/tests/contacts-tests

# DESTDIR stages a package. Without it, the launcher points at the installed
# binary so it works even when ~/.local/bin is not on the launcher's PATH.
APPS = $(PREFIX)/share/applications

install: all
	cd build && $(MAKE) install INSTALL_ROOT=$(DESTDIR)
	sed -i 's|^Exec=.*|Exec=$(PREFIX)/bin/contacts|' "$(DESTDIR)$(APPS)/org.omelia.Contacts.desktop"
	@# Builds before the org.omelia.Contacts app id installed a plain contacts.desktop.
	-[ -n "$(DESTDIR)" ] || ! grep -qs '^Exec=$(PREFIX)/bin/contacts$$' "$(APPS)/contacts.desktop" || \
		rm -f "$(APPS)/contacts.desktop" "$(PREFIX)/share/icons/hicolor/scalable/apps/contacts.svg"
	-[ -n "$(DESTDIR)" ] || update-desktop-database "$(APPS)" >/dev/null 2>&1 || true

clean:
	rm -rf build
