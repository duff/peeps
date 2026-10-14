#pragma once

#include <QList>
#include <QString>

namespace contacts {

enum class Key { Up, Down, Home, End, Escape, Return, Delete, Backspace, Slash, Question, N, E, S, C, Plus, Equal, Minus, Zero, Other };

enum class Surface { Browser, Editor, ConfirmDelete, Help, Account };

enum class Command {
  Ignore,
  MoveUp,
  MoveDown,
  MoveHome,
  MoveEnd,
  FocusSearch,
  ClearSearch,
  TypeToSearch,
  Back,
  OpenContact,
  ActivateField,
  CopyField,
  SendEmail,
  NewContact,
  EditContact,
  Help,
  TextLarger,
  TextSmaller,
  TextReset,
  Save,
  Cancel
};

struct KeyEvent {
  Key key = Key::Other;
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
  // The key types a visible character, such as a letter or a digit.
  bool printable = false;
  bool textInput = false;
  bool multiline = false;
  bool searchEmpty = true;
  bool detailOpen = false;
  Surface surface = Surface::Browser;
};

Command routeKey(const KeyEvent& event);

struct BindingHelp {
  QString group;
  QString keys;
  QString action;
};

QList<BindingHelp> bindingHelp();

}  // namespace contacts
