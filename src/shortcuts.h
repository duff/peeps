#pragma once

#include <QList>
#include <QString>

namespace omacontacts {

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
  bool searchEmpty = true;
  bool narrowDetail = false;
  Surface surface = Surface::Browser;
};

Command routeKey(const KeyEvent& event);

struct BindingHelp {
  QString keys;
  QString action;
};

QList<BindingHelp> bindingHelp();

}  // namespace omacontacts
