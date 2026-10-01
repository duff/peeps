# Sources shared by the app and the tests.

SOURCES += \
    $$PWD/src/blockingnam.cpp \
    $$PWD/src/cli.cpp \
    $$PWD/src/contact.cpp \
    $$PWD/src/desktop.cpp \
    $$PWD/src/dialogs.cpp \
    $$PWD/src/google.cpp \
    $$PWD/src/instance.cpp \
    $$PWD/src/mainwindow.cpp \
    $$PWD/src/oauth.cpp \
    $$PWD/src/photos.cpp \
    $$PWD/src/search.cpp \
    $$PWD/src/secrets.cpp \
    $$PWD/src/shortcuts.cpp \
    $$PWD/src/store.cpp \
    $$PWD/src/theme.cpp \
    $$PWD/src/vcard.cpp

HEADERS += \
    $$PWD/src/blockingnam.h \
    $$PWD/src/cli.h \
    $$PWD/src/contact.h \
    $$PWD/src/desktop.h \
    $$PWD/src/dialogs.h \
    $$PWD/src/google.h \
    $$PWD/src/http.h \
    $$PWD/src/instance.h \
    $$PWD/src/mainwindow.h \
    $$PWD/src/net.h \
    $$PWD/src/oauth.h \
    $$PWD/src/photos.h \
    $$PWD/src/search.h \
    $$PWD/src/secrets.h \
    $$PWD/src/shortcuts.h \
    $$PWD/src/store.h \
    $$PWD/src/theme.h \
    $$PWD/src/vcard.h \
    $$PWD/src/version.h \
    $$PWD/src/worker.h

# A packaged build can ship a published Google OAuth client so people only
# sign in. Leave these unset and each person brings their own client id.
GOOGLE_CLIENT_ID = $$(CONTACTS_GOOGLE_CLIENT_ID)
GOOGLE_CLIENT_SECRET = $$(CONTACTS_GOOGLE_CLIENT_SECRET)
!isEmpty(GOOGLE_CLIENT_ID): DEFINES += CONTACTS_GOOGLE_CLIENT_ID=\\\"$$GOOGLE_CLIENT_ID\\\"
!isEmpty(GOOGLE_CLIENT_SECRET): DEFINES += CONTACTS_GOOGLE_CLIENT_SECRET=\\\"$$GOOGLE_CLIENT_SECRET\\\"
