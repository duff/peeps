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
  void roundTripsPerson();
  void fullAndIncrementalSync();
  void retriesOnceAfterUnauthorized();
  void createReadsResource();
  void mailUsesDesktopHandler();
  void browserPrefersOmarchy();
  void storeRoundTrip();
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
