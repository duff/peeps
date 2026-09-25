# Contacts

A keyboard-first address book for Omarchy. It keeps your Google contacts on the desktop, paints itself from the current Omarchy theme, and answers search from a local cache so looking someone up does not wait on the network.

With no arguments, `contacts` opens the window. A command stays in the terminal, which is how a person or an agent lists, searches, and changes the same contacts.

## Is this for you?

It fits if you want these things:

- Google is where the contacts live, and the window is just a fast way to use them. The same book shows up on an iPhone, a Mac, or an Android phone that uses Google Contacts, so a change here or there stays in sync.
- Search, copy, and mail should work from the keyboard, including while the search field is focused.
- The window should follow `colors.toml` when you change Omarchy themes, instead of shipping its own look.
- Mail should open in whatever owns `mailto:` on the machine. On a stock Omarchy desktop that is the HEY app.
- An agent should be able to inspect and edit the book from the shell, with JSON on a pipe, without a private API.

It is one Google account, not a CRM. Lookups and the contact you are reading come from the cache. Creating, editing, deleting, and sync go to Google and then refresh that cache. A delete from the command line requires `--yes`.

## Install

Qt 6 (widgets and network), libsecret, and a C++20 compiler.

```bash
make
make test
make install
```

`make install` puts `contacts` on `~/.local/bin` and adds a launcher. The tests use Qt Test and the offscreen platform.

To open it the way other Omarchy apps open, bind it in `~/.config/hypr/bindings.lua`. The `focus` pattern matches the window class, so a second press raises the window you already have:

```lua
o.bind("SUPER + SHIFT + ALT + C", "Contacts", { launch = "contacts", focus = "^contacts$" })
```

## The window

Type to filter. Up and Down move through the matches, including while the search field still has focus. Enter opens the contact. Enter on a field copies it. Ctrl+Enter sends mail to the selected address through `xdg-email`, which follows the desktop mail handler.

Esc or Backspace returns to the list, or clears the search if that is where you are. `?` opens the shortcut list. Ctrl+N starts a contact, Ctrl+E edits the one you are on, and Ctrl+S saves it. New, Sync, Keybindings, and Account are in the Menu.

| Keys | Action |
| --- | --- |
| Type | Filter from the search field |
| Up / Down | Move through matches |
| `/` | Focus search |
| Enter | Open the contact, or copy the selected field |
| Super+C | Copy the selected field. Omarchy delivers this as Ctrl+C |
| Ctrl+Enter | Email the selected address |
| Esc or Backspace | Back to the list, or clear the search |
| `?` | Shortcut help |
| Ctrl+N / Ctrl+E | New contact, edit the selected contact |
| Ctrl+S | Save the open contact |

Copying a field names it. A birthday says "Copied birthday". A work address says "Copied work email".

## Command line

`contacts help` lists every command, and `contacts <command> --help` shows that command's flags. A terminal prints text. A pipe prints JSON. `--json` and `--text` force either one.

List, search, and show read the cache only. Add, update, delete, and sync talk to Google, then write the cache again. Omitted fields on an update stay as they are. Repeatable flags such as `--email` replace that whole list. `--data` accepts the same JSON object that `contacts show --json` prints, which is the way to set an address, a date, or anything a flag does not cover.

```bash
contacts search lovelace
contacts show ada@example.com --json
contacts add --given Ada --family Lovelace --email home=ada@example.com
contacts update people/c1 --notes "Met in 1843"
contacts delete people/c1 --yes
contacts sync
```

An id is a Google resource name (`people/c123`), a unique name, or a unique email. Exit codes are 0 for success, 1 when the command fails, 2 for usage, 3 when nobody is signed in, and 4 when nothing matches. `contacts commands --json` prints the catalog.

```bash
contacts list --json | jq '.contacts[] | {name, email}'
contacts search ada --jq '.contacts[] | {id, name, score}'
```

## Google

Contacts signs in with a Desktop OAuth client and the People API scope `https://www.googleapis.com/auth/contacts`. The sign-in redirect is `http://127.0.0.1` on a free port. A Desktop client accepts that loopback address, so you do not register a port or a public URL.

Use a published app, not a testing one. A consent screen left in Testing only allows listed test users, and Google expires those refresh tokens after seven days. That is a poor fit for an address book you open every day.

1. In [Google Cloud](https://console.cloud.google.com/), create a project and enable the People API.
2. Open the OAuth consent screen (Google Auth Platform, Audience and Branding). Choose External unless this is only for a Workspace organization, in which case Internal is enough.
3. Set the app name and a support email. Add the scope `https://www.googleapis.com/auth/contacts`.
4. Publish the app so its status is In production.
5. Under Credentials, create an OAuth client ID of type **Desktop app**. Copy the client id. The client secret is optional. Contacts uses PKCE.
6. Launch Contacts, open Menu → Account, paste the client id, and choose Connect. Sign in in the browser. The page that comes back says you can close the tab.

The Contacts scope is sensitive. Until Google verifies the app, the consent screen warns that Google has not verified it. For your own account, continue past that warning. Verification, which wants a homepage, a privacy policy, and a review, is how you remove the warning for everyone else. Publishing is what leaves Testing. Verification is a separate step.

The refresh token stays in the login keyring under `org.omelia.contacts`. Disconnect from Menu → Account clears it.

## Where the data lives

| Path | What |
| --- | --- |
| `~/.local/share/contacts/contacts.json` | The cache search reads |
| `~/.config/contacts/config.json` | The OAuth client id |
| `~/.cache/contacts/photos` | Cached photos |
| Login keyring | The Google refresh token |

`contacts sync` downloads changes since the last sync. `contacts sync --full` downloads the whole book.

## Theme

On startup, and whenever the theme files change, Contacts reads `~/.local/state/omarchy/current/theme/colors.toml`. If that directory is missing, it tries the legacy `~/.config/omarchy/current` path. A broken or missing palette falls back to Tokyo Night as a whole. The window uses square controls, 12px text, and quiet borders from that palette.
