#include "shortcuts.h"

namespace omacontacts {

Command routeKey(const KeyEvent& event) {
  if (event.alt) return Command::Ignore;

  if (event.surface == Surface::Editor) {
    if (event.key == Key::Escape) return Command::Cancel;
    if (event.ctrl && (event.key == Key::S || event.key == Key::Return)) return Command::Save;
    if (event.key == Key::Return && !event.multiline) return Command::Save;
    return Command::Ignore;
  }
  if (event.surface == Surface::ConfirmDelete) {
    if (event.key == Key::Escape) return Command::Cancel;
    return Command::Ignore;
  }
  if (event.surface == Surface::Help) {
    if (event.key == Key::Escape || event.key == Key::Question || event.key == Key::F1) return Command::Cancel;
    return Command::Ignore;
  }
  if (event.surface == Surface::Account) {
    if (event.key == Key::Escape) return Command::Cancel;
    return Command::Ignore;
  }

  if (event.key == Key::F1) return Command::Help;
  if (event.ctrl && event.key == Key::Q) return Command::Quit;
  if (event.ctrl && event.key == Key::N) return Command::NewContact;
  if (event.ctrl && event.key == Key::E) return Command::EditContact;
  if (event.ctrl && event.key == Key::Comma) return Command::Account;
  if (event.ctrl && event.key == Key::Return) return Command::EditContact;
  if (event.ctrl && event.shift && event.key == Key::C && event.detailOpen && !event.textInput) return Command::CopyContact;
  if (event.ctrl && !event.shift && event.key == Key::C && event.detailOpen && !event.textInput) return Command::CopyField;
  if (event.ctrl && event.key == Key::M && event.detailOpen && !event.textInput) return Command::SendEmail;

  if (event.detailOpen && (event.key == Key::Escape || (event.key == Key::Backspace && !event.textInput))) return Command::Back;
  if (event.detailOpen) {
    switch (event.key) {
      case Key::Up:
        return Command::MoveUp;
      case Key::Down:
        return Command::MoveDown;
      case Key::Home:
        return Command::MoveHome;
      case Key::End:
        return Command::MoveEnd;
      case Key::Return:
        return Command::ActivateField;
      case Key::Delete:
        return Command::Ignore;
      default:
        break;
    }
  }

  if (event.textInput) {
    switch (event.key) {
      case Key::Up:
        return Command::MoveUp;
      case Key::Down:
        return Command::MoveDown;
      case Key::Escape:
        if (!event.searchEmpty) return Command::ClearSearch;
        return Command::Ignore;
      case Key::Return:
        return Command::OpenContact;
      default:
        return Command::Ignore;
    }
  }

  switch (event.key) {
    case Key::Up:
      return Command::MoveUp;
    case Key::Down:
      return Command::MoveDown;
    case Key::Home:
      return Command::MoveHome;
    case Key::End:
      return Command::MoveEnd;
    case Key::Slash:
      return Command::FocusSearch;
    case Key::Escape:
      if (!event.searchEmpty) return Command::ClearSearch;
      return Command::Ignore;
    case Key::Return:
      return Command::OpenContact;
    case Key::Question:
      return Command::Help;
    default:
      return Command::Ignore;
  }
}

QList<BindingHelp> bindingHelp() {
  return {
      {QStringLiteral("Type"), QStringLiteral("Filter contacts from the search field")},
      {QStringLiteral("Up  Down"), QStringLiteral("Move through contacts or fields")},
      {QStringLiteral("/"), QStringLiteral("Focus search")},
      {QStringLiteral("Enter"), QStringLiteral("Open the selected contact, or copy the selected field")},
      {QStringLiteral("Ctrl+C"), QStringLiteral("Copy the selected field")},
      {QStringLiteral("Ctrl+Shift+C"), QStringLiteral("Copy the whole contact")},
      {QStringLiteral("Ctrl+M"), QStringLiteral("Email the selected address")},
      {QStringLiteral("Esc  or  Backspace"), QStringLiteral("Back to the list, or clear the search")},
      {QStringLiteral("?  or  F1"), QStringLiteral("Show keybindings")},
      {QStringLiteral("Ctrl+Enter"), QStringLiteral("Edit the selected contact, or save from the notes field")},
      {QStringLiteral("Ctrl+N"), QStringLiteral("New contact")},
      {QStringLiteral("Ctrl+E"), QStringLiteral("Edit the selected contact")},
      {QStringLiteral("Ctrl+,"), QStringLiteral("Google account")},
      {QStringLiteral("Enter"), QStringLiteral("Save the open contact from a single-line field")},
      {QStringLiteral("Ctrl+S"), QStringLiteral("Save the open contact")},
      {QStringLiteral("Ctrl+Q"), QStringLiteral("Quit")},
  };
}

}  // namespace omacontacts
