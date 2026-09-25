#pragma once

#include <QList>
#include <QString>

namespace contacts {

enum class Key {
  Up,
  Down,
  Home,
  End,
  PageUp,
  PageDown,
  Escape,
  Return,
  Delete,
  Backspace,
  Slash,
  Question,
  F1,
  J,
  K,
  N,
  E,
  M,
  Y,
  P,
  R,
  S,
  Q,
  C,
  Comma,
  Other
};

enum class Surface { Browser, Editor, ConfirmDelete, Help, Account };

enum class Command {
  Ignore,
  MoveUp,
  MoveDown,
  MoveHome,
  MoveEnd,
  PageUp,
  PageDown,
  FocusSearch,
  ClearSearch,
  Back,
  OpenContact,
  ActivateField,
  CopyField,
  CopyContact,
  SendEmail,
  NewContact,
  EditContact,
  DeleteContact,
  PrimaryAction,
  CopyEmail,
  CopyPhone,
  Sync,
  Help,
  Save,
  Cancel,
  Quit,
  Account
};

struct KeyEvent {
  Key key = Key::Other;
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
  bool textInput = false;
  bool multiline = false;
  bool searchEmpty = true;
  bool detailOpen = false;
  Surface surface = Surface::Browser;
};

Command routeKey(const KeyEvent& event);

struct BindingHelp {
  QString keys;
  QString action;
};

QList<BindingHelp> bindingHelp();

}  // namespace contacts
