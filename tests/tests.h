#pragma once

#include <QObject>

class ThemeTests : public QObject {
  Q_OBJECT
 private slots:
  void parsesOnlyFullHex();
  void mixesToward();
  void onAccentFollowsContrast();
  void semanticPaletteUsesDeclaredRoles();
  void derivesMissingRoles();
  void ansiAliases();
  void rejectsIncompleteDocuments();
  void lightModeDarkensStatusInk();
  void infersLightAppearance();
  void brokenStateDoesNotReviveLegacy();
  void legacyOnlyWhenStateIsAbsent();
  void missingHomeFallsBackWhole();
  void stylesheetUsesTheGivenPalette();
};

class SearchTests : public QObject {
  Q_OBJECT
 private slots:
  void displayNameAndInitials();
  void postalAddressBreaksAfterTheStreet();
  void plainTextSkipsThePhoto();
  void draftValidation();
  void emptyQueryIsAlphabetical();
  void namePrefixOutranksEmail();
  void tokensAreAnded();
  void upsertAndRemove();
  void tenThousandContactsStayFast();
  void moveWrapsAndClamps();
};

class GoogleTests : public QObject {
  Q_OBJECT
 private slots:
  void pkceMatchesRfc7636();
  void authUrl();
  void parsesBrowserRedirect();
  void keepsRefreshToken();
  void refreshPostsToken();
  void parsesPrimaryFields();
  void prefersRealPhotoOverDefault();
  void sizesPhotoUrl();
  void oldCacheRequestsFullSync();
  void roundTripsPerson();
  void labelTypesRoundTrip();
  void updateRetriesWithFreshEtag();
  void updatesContactPhoto();
  void fullAndIncrementalSync();
  void retriesOnceAfterUnauthorized();
  void createReadsResource();
  void mailUsesDesktopHandler();
  void browserPrefersOmarchy();
  void storeRoundTrip();
};

class LayoutTests : public QObject {
  Q_OBJECT
 private slots:
  void enterReplacesTheListWithThatContact();
  void editOpensOnTheSelectedField();
  void companyWithoutAPersonalName();
  void notesWrapInsteadOfEllipsis();
  void blankPhoneTypeStaysBlank();
};

class GuardTests : public QObject {
  Q_OBJECT
 private slots:
  void requestsStayOnHttp1();
  void secondLaunchRaisesTheFirst();
};

class ShortcutTests : public QObject {
  Q_OBJECT
 private slots:
  void typingSuspendsSingleKeys();
  void escapeClearsBeforeBack();
  void listKeys();
  void editorKeepsKeys();
  void accountKeepsQuestionMarks();
  void deleteConfirmIgnoresEnter();
  void helpListsEmail();
};
