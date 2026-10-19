.PHONY: all test clean install screenshots release

PREFIX ?= $(HOME)/.local

# A local.mk, which git ignores, can set PEEPS_GOOGLE_CLIENT_ID and
# PEEPS_GOOGLE_CLIENT_SECRET so your own builds carry your Google client.
-include local.mk
export PEEPS_GOOGLE_CLIENT_ID PEEPS_GOOGLE_CLIENT_SECRET

CONFIGURE = cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$(PREFIX)"
BUILD = cmake --build build --parallel $(shell nproc)

all:
	$(CONFIGURE)
	$(BUILD) --target peeps

test:
	$(CONFIGURE)
	$(BUILD) --target peeps-tests
	ctest --test-dir build --output-on-failure

# The README's pictures, rendered offscreen from fictional contacts.
screenshots:
	$(CONFIGURE)
	$(BUILD) --target peeps-screenshots
	build/peeps-screenshots docs/screenshots tools/screenshot-photo.jpg
	if command -v oxipng >/dev/null; then oxipng -q -o 4 --strip safe docs/screenshots/*.png; fi

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

# The tarball and SHA256SUMS a GitHub release carries for the peeps-bin
# package: a usr/ tree, built from a clean tree with the Google client from
# local.mk. Upload them with gh release create v$(VERSION).
VERSION = $(shell sed -n 's/.*kVersion\[\] = "\(.*\)";/\1/p' src/version.h)
TARBALL = peeps-$(VERSION)-$(shell uname -m)-linux.tar.gz

release: test
	@[ -n "$$PEEPS_GOOGLE_CLIENT_ID" ] || { echo "make release needs PEEPS_GOOGLE_CLIENT_ID in local.mk" >&2; exit 1; }
	@[ -z "$$(git status --porcelain)" ] || { echo "make release needs a clean working tree" >&2; exit 1; }
	rm -rf release
	cmake -S . -B release/build -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF
	cmake --build release/build --parallel $(shell nproc) --target peeps
	DESTDIR="$(CURDIR)/release/stage" cmake --install release/build --strip
	install -Dm644 LICENSE release/stage/usr/share/licenses/peeps/LICENSE
	tar -C release/stage --owner=0 --group=0 -czf release/$(TARBALL) usr
	cd release && sha256sum $(TARBALL) > SHA256SUMS

clean:
	rm -rf build release
