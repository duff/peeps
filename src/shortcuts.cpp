#include "shortcuts.h"

namespace omacontacts {

Command routeKey(const KeyEvent& event) {
  if (event.alt) return Command::Ignore;

  if (event.surface == Surface::Editor) {
    if (event.key == Key::Escape) return Command::Cancel;
    if (event.ctrl && (event.key == Key::Return || event.key == Key::S)) return Command::Save;
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

  if (event.detailOpen && event.key == Key::Escape) return Command::Back;
  if (event.detailOpen && (event.key == Key::Up || event.key == Key::Down || event.key == Key::Home || event.key == Key::End)) {
    return Command::Ignore;
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
    case Key::Delete:
      return Command::DeleteContact;
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
      {QStringLiteral("Up  Down"), QStringLiteral("Move through matches")},
      {QStringLiteral("/"), QStringLiteral("Focus search")},
      {QStringLiteral("Enter"), QStringLiteral("Open the selected contact")},
      {QStringLiteral("Esc"), QStringLiteral("Back to the list, or clear the search")},
      {QStringLiteral("Delete"), QStringLiteral("Delete the selected contact")},
      {QStringLiteral("?  or  F1"), QStringLiteral("Show keybindings")},
      {QStringLiteral("Ctrl+Enter"), QStringLiteral("Edit the selected contact, or save while editing")},
      {QStringLiteral("Ctrl+N"), QStringLiteral("New contact")},
      {QStringLiteral("Ctrl+E"), QStringLiteral("Edit the selected contact")},
      {QStringLiteral("Ctrl+,"), QStringLiteral("Google account")},
      {QStringLiteral("Ctrl+S"), QStringLiteral("Save the open contact")},
      {QStringLiteral("Ctrl+Q"), QStringLiteral("Quit")},
  };
}

}  // namespace omacontacts
