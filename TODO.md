# To do

Ideas from the October 2026 review, roughly in order of value. None of these are started.

## Finding and reading

- [ ] **Contact header.** Open a contact on a large name, "title · company", and the photo, instead of a "Full name" row. Group the fields under the same headings the editor uses.
- [ ] **Email from the list.** Ctrl+Enter on a search result emails that person's first address, so search, then Ctrl+Enter, sends mail without opening the contact.
- [ ] **PgUp and PgDn.** These were removed on purpose (commit 46447d0). With a couple of thousand contacts they may be worth having again in the list.
- [x] **Sync time.** Once the last sync is over an hour old, the footer says so ("Synced 3 hours ago"), so a stale cache is obvious.

## Getting contacts in and out

- [ ] **Import vCards.** `peeps import file.vcf` and a drop target in the window, so a card that arrives over LocalSend lands in Google in one step.
- [ ] **Launcher search.** An Omarchy shell plugin so Super+Space finds a contact and copies or emails it without opening the window.
- [ ] **Birthdays.** An upcoming-birthdays view, or `peeps birthdays` feeding `omarchy reminder`.

## Google

- [ ] **Labels.** Filter by Google contact labels (contactGroups).
- [ ] **Duplicates.** Find likely duplicates and merge them.
- [x] **Expired sign-in.** When Google revokes the refresh token, the footer says so and offers Reconnect.

## Before a public release

- [x] **License.** MIT, like Omarchy.
- [x] **Screenshots.** `make screenshots` draws the README's pictures from fictional contacts.
- [x] **App id.** The window class and desktop file are now `org.omelia.Peeps`.
- [x] **Google client.** Peeps ships its own client, built in with `PEEPS_GOOGLE_CLIENT_ID`. The account screen no longer asks for one.
- [ ] **Verification.** Branding is verified. Data access for the contacts scope was submitted on October 14, 2026 with the demo video (https://youtu.be/WobFZxedkRk). Waiting on Google.
- [x] **Google branding.** The consent screen's app name is Peeps, with https://omelia.org/peeps and its privacy policy.
- [x] **Label boxes.** The email, phone, and address label boxes show a down-arrow, so they read as choices.
- [ ] **AUR package.** Wait until Google verifies Peeps, then publish `peeps` to the AUR with the Peeps client built in. When it is up, lead the README's Install with the `yay` line, say a package needs no Google client of your own, and move the client steps under building from source.
- [x] **Public repo.** github.com/duff/peeps is public.
- [x] **README.** It says why Peeps exists and which builds carry a Google client, and no longer has the Hyprland binding example.
- [x] **Clipped text.** Dialogs size themselves for their wrapped text, so a second line no longer gets cut off on Wayland, and Keybindings shows one line per binding.

## To consider

- [ ] **Launcher overlay.** An Omarchy shell plugin, like the emoji picker on Super+Ctrl+E: a key opens a themed panel that loads `peeps list --json` and filters as you type. Enter copies the email or phone, Ctrl+Enter starts an email, Shift+Enter opens the contact in Peeps, which needs a small `peeps open <id>`. It would install with `omarchy plugin add <git url>`, likely from its own repo. Putting contacts in Super+Space itself would need Omarchy to accept menu providers from plugins.
- [ ] **Agents example.** Show in the README what an agent can do through the `peeps` command. Possibilities:
  - Add a person from an email signature or a photo of a business card.
  - Answer "who works at Northwind?" or "whose birthday is this month?" from `peeps list --json`.
  - Tidy the book: phone formats, missing country codes, and likely duplicates, shown for review before anything changes.
  - Export a group as vCards to send on.
  - Ship an agent skill that teaches the commands, the way Omarchy ships skills for its own tools.
