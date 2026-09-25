# Omacontacts

A Qt 6 contacts app for Omarchy. It reads the desktop theme, finds people as you type, and creates, edits, and deletes Google Contacts. Email opens in the desktop's default mail handler.

## Build

```bash
make
make test
make install
```

`make install` puts the binary in `~/.local/bin` and adds a launcher entry. Qt 6 (widgets, network), libsecret, and a C++20 compiler are required. Tests use Qt Test and the offscreen platform plugin.

## Google

1. In Google Cloud, create a project and enable the People API.
2. Configure the OAuth consent screen. While it is in testing, add the Google account you will use.
3. Create an OAuth client of type **Desktop**.
4. Launch Omacontacts, press `Ctrl+,`, paste the client id, and choose Connect.

The refresh token stays in the login keyring. Contacts are cached in `~/.local/share/omacontacts/contacts.json` so search stays local.

## Shortcuts

| Keys | Action |
| --- | --- |
| Type | Filter from the search field |
| Up / Down | Move through matches, including while the search field is focused |
| `/` | Focus search |
| Enter | Open the selected contact, or copy the selected field |
| Ctrl+C | Copy the selected field |
| Ctrl+Shift+C | Copy the whole contact, without the photo |
| Ctrl+M | Email the selected address |
| Esc or Backspace | Back to the list, or clear the search |
| F1 or `?` | Shortcut help |
| Ctrl+N / Ctrl+E | New contact, edit the selected contact |
| Ctrl+, | Google account |
| Ctrl+S / Ctrl+Enter | Save the open contact |
| Ctrl+Q | Quit |

New, Sync, Keybindings, and Account are in the Menu. Letters typed in the search field only filter the list.

## Theme

On startup, and whenever the theme files change, Omacontacts reads `~/.local/state/omarchy/current/theme/colors.toml`. If that directory is missing, it tries the legacy `~/.config/omarchy/current` path. A broken or missing palette falls back to Tokyo Night as a whole. The window uses square controls, 12px text, and quiet borders from that palette.
