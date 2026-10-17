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
  void ignoresValuesItDoesNotRead();
  void lightModeDarkensStatusInk();
  void infersLightAppearance();
  void brokenStateDoesNotReviveLegacy();
  void legacyOnlyWhenStateIsAbsent();
  void missingHomeFallsBackWhole();
  void stylesheetUsesTheGivenPalette();
  void followsOmarchyTextSize();
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
  void expiredSignInIsNamed();
  void refreshPostsToken();
  void parsesPrimaryFields();
  void prefersRealPhotoOverDefault();
  void sizesPhotoUrl();
  void savedPhotoIsVisibleImmediately();
  void downloadedPhotoIsVisibleImmediately();
  void uploadsPhotosAtFullQuality();
  void drawsPhotosFromASizedCopy();
  void oldCacheRequestsFullSync();
  void cacheKeepsSyncTime();
  void syncedAgoReads();
  void roundTripsPerson();
  void labelTypesRoundTrip();
  void updateRetriesWithFreshEtag();
  void updatesContactPhoto();
  void fullAndIncrementalSync();
  void retriesOnceAfterUnauthorized();
  void createReadsResource();
  void mailUsesDesktopHandler();
  void browserPrefersOmarchy();
  void sharesThroughOmarchy();
  void writesVCards();
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
  void copyNamesTheField();
  void editorTracksChanges();
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
  void textSizeKeys();
  void escapeClearsBeforeBack();
  void listKeys();
  void editorKeepsKeys();
  void accountKeepsQuestionMarks();
  void deleteConfirmIgnoresEnter();
  void helpListsEmail();
  void typingOutsideSearchSearches();
  void vimKeysMoveThroughFields();
};

class CliTests : public QObject {
  Q_OBJECT
 private slots:
  void invocationLeavesTheWindowAlone();
  void helpAndCommands();
  void usageAndVersion();
  void listAndSearchStayOnTheCache();
  void showResolvesIdNameAndEmail();
  void addUpdateAndDelete();
  void jsonDataRoundTrip();
  void syncRefreshesTheCache();
  void addFlagsAppend();
  void partialNamesListCandidates();
  void skillLinks();
  void skillRemoveSticks();
  void skillCoversEveryCommand();
  void liveCacheAndGoogle();
  void exportPrintsVCards();
};
