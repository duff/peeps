#include "shortcuts.h"

namespace contacts {

Command routeKey(const KeyEvent& event) {
  if (event.alt) return Command::Ignore;

  // Peeps' text size, on top of Omarchy's, in the list and in a contact.
  if (event.ctrl && event.surface == Surface::Browser) {
    if (event.key == Key::Plus || event.key == Key::Equal) return Command::TextLarger;
    if (event.key == Key::Minus) return Command::TextSmaller;
    if (event.key == Key::Zero) return Command::TextReset;
  }

  if (event.surface == Surface::Editor) {
    if (event.key == Key::Escape) return Command::Cancel;
    if (event.ctrl && (event.key == Key::S || event.key == Key::Return)) return Command::Save;
    if (event.key == Key::Return && !event.multiline) return Command::Save;
    return Command::Ignore;
  }
  if (event.surface == Surface::ConfirmDelete || event.surface == Surface::Account) {
    return event.key == Key::Escape ? Command::Cancel : Command::Ignore;
  }
  if (event.surface == Surface::Help) {
    return event.key == Key::Escape || event.key == Key::Question ? Command::Cancel : Command::Ignore;
  }

  if (event.ctrl && event.key == Key::N) return Command::NewContact;
  if (event.ctrl && event.key == Key::E) return Command::EditContact;
  if (event.ctrl && event.key == Key::Return && event.detailOpen && !event.textInput) return Command::SendEmail;
  if (event.ctrl && event.key == Key::Return) return Command::Ignore;
  if (event.ctrl && !event.shift && event.key == Key::C && event.detailOpen && !event.textInput) return Command::CopyField;

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
        return event.searchEmpty ? Command::Ignore : Command::ClearSearch;
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
      return event.searchEmpty ? Command::Ignore : Command::ClearSearch;
    case Key::Return:
      return Command::OpenContact;
    case Key::Question:
      return Command::Help;
    default:
      // Typing anywhere outside a field starts a new search.
      return event.printable && !event.ctrl ? Command::TypeToSearch : Command::Ignore;
  }
}

QList<BindingHelp> bindingHelp() {
  const QString find = QStringLiteral("Find");
  const QString contact = QStringLiteral("Contact");
  const QString edit = QStringLiteral("Edit");
  return {
      {find, QStringLiteral("Type"), QStringLiteral("Search names, email, phone, and notes")},
      {find, QStringLiteral("Up  Down"), QStringLiteral("Move through contacts or fields")},
      {find, QStringLiteral("Enter"), QStringLiteral("Open the selected contact, or copy the selected field")},
      {find, QStringLiteral("Esc  or  Backspace"), QStringLiteral("Back to the list, or clear the search")},
      {find, QStringLiteral("/"), QStringLiteral("Focus search")},
      {contact, QStringLiteral("Super+C"), QStringLiteral("Copy the selected field")},
      {contact, QStringLiteral("Ctrl+Enter"), QStringLiteral("Email the selected address")},
      {contact, QStringLiteral("Ctrl+E"), QStringLiteral("Edit the selected contact")},
      {contact, QStringLiteral("Ctrl+N"), QStringLiteral("New contact")},
      {edit, QStringLiteral("Ctrl+S"), QStringLiteral("Save the open contact")},
      {edit, QStringLiteral("Esc"), QStringLiteral("Cancel, and ask first when something changed")},
      {edit, QStringLiteral("Tab"), QStringLiteral("Next field, in the order they appear")},
      {QStringLiteral("Text"), QStringLiteral("Ctrl+=  Ctrl+-"), QStringLiteral("Larger or smaller text than Omarchy's")},
      {QStringLiteral("Text"), QStringLiteral("Ctrl+0"), QStringLiteral("Back to Omarchy's text size")},
      {QStringLiteral("Help"), QStringLiteral("?"), QStringLiteral("Show keybindings")},
  };
}

}  // namespace contacts
