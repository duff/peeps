.PHONY: all test clean install

all:
	mkdir -p build
	cd build && qmake6 ../contacts.pro && $(MAKE)

test:
	mkdir -p build/tests
	cd build/tests && qmake6 ../../tests/tests.pro && $(MAKE)
	QT_QPA_PLATFORM=offscreen ./build/tests/contacts-tests

install: all
	cd build && $(MAKE) install
	sed -i 's|^Exec=.*|Exec='"$$HOME"'/.local/bin/contacts|' "$$HOME/.local/share/applications/contacts.desktop"
	-update-desktop-database "$$HOME/.local/share/applications" >/dev/null 2>&1 || true

clean:
	rm -rf build
