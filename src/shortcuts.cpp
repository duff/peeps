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
  if (event.ctrl && event.key == Key::R) return Command::Sync;
  if (event.ctrl && event.key == Key::Comma) return Command::Account;
  if (event.ctrl && event.key == Key::Return) return Command::PrimaryAction;

  if (event.textInput) {
    switch (event.key) {
      case Key::Up:
        return Command::MoveUp;
      case Key::Down:
        return Command::MoveDown;
      case Key::PageUp:
        return Command::PageUp;
      case Key::PageDown:
        return Command::PageDown;
      case Key::Escape:
        if (!event.searchEmpty) return Command::ClearSearch;
        if (event.narrowDetail) return Command::Back;
        return Command::Ignore;
      case Key::Return:
        return Command::PrimaryAction;
      default:
        return Command::Ignore;
    }
  }

  switch (event.key) {
    case Key::Up:
    case Key::K:
      return Command::MoveUp;
    case Key::Down:
    case Key::J:
      return Command::MoveDown;
    case Key::Home:
      return Command::MoveHome;
    case Key::End:
      return Command::MoveEnd;
    case Key::PageUp:
      return Command::PageUp;
    case Key::PageDown:
      return Command::PageDown;
    case Key::Slash:
      return Command::FocusSearch;
    case Key::Escape:
      if (!event.searchEmpty) return Command::ClearSearch;
      if (event.narrowDetail) return Command::Back;
      return Command::Ignore;
    case Key::N:
      return Command::NewContact;
    case Key::E:
      return Command::EditContact;
    case Key::Delete:
      return Command::DeleteContact;
    case Key::M:
    case Key::Return:
      return Command::PrimaryAction;
    case Key::Y:
      return Command::CopyEmail;
    case Key::P:
      return Command::CopyPhone;
    case Key::R:
      return Command::Sync;
    case Key::Question:
      return Command::Help;
    case Key::Comma:
      return Command::Account;
    default:
      return Command::Ignore;
  }
}

QList<BindingHelp> bindingHelp() {
  return {
      {QStringLiteral("Type"), QStringLiteral("Filter contacts from the search field")},
      {QStringLiteral("Up  Down"), QStringLiteral("Move through matches")},
      {QStringLiteral("PgUp  PgDn"), QStringLiteral("Jump through matches")},
      {QStringLiteral("Enter"), QStringLiteral("Email the contact, or edit when there is no address")},
      {QStringLiteral("Esc"), QStringLiteral("Clear the search")},
      {QStringLiteral("/"), QStringLiteral("Focus search")},
      {QStringLiteral("j  k"), QStringLiteral("Move through matches when search is not focused")},
      {QStringLiteral("n"), QStringLiteral("New contact")},
      {QStringLiteral("e"), QStringLiteral("Edit the selected contact")},
      {QStringLiteral("Delete"), QStringLiteral("Delete the selected contact")},
      {QStringLiteral("m"), QStringLiteral("Email the selected contact")},
      {QStringLiteral("y"), QStringLiteral("Copy the email address")},
      {QStringLiteral("p"), QStringLiteral("Copy the phone number")},
      {QStringLiteral("r"), QStringLiteral("Sync with Google")},
      {QStringLiteral("?  or  F1"), QStringLiteral("Show this help")},
      {QStringLiteral("Ctrl+Enter"), QStringLiteral("Email the selected contact")},
      {QStringLiteral("Ctrl+N"), QStringLiteral("New contact")},
      {QStringLiteral("Ctrl+E"), QStringLiteral("Edit the selected contact")},
      {QStringLiteral("Ctrl+R"), QStringLiteral("Sync with Google")},
      {QStringLiteral("Ctrl+,"), QStringLiteral("Google account")},
      {QStringLiteral("Ctrl+S"), QStringLiteral("Save the open contact")},
      {QStringLiteral("Ctrl+Enter"), QStringLiteral("Save the open contact")},
      {QStringLiteral("Ctrl+Q"), QStringLiteral("Quit")},
  };
}

}  // namespace omacontacts
