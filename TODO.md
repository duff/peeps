# To do

Ideas from the October 2026 review, roughly in order of value. None of these are started.

## Finding and reading

- [ ] **Contact header.** Open a contact on a large name, "title · company", and the photo, instead of a "Full name" row. Group the fields under the same headings the editor uses.
- [ ] **Email from the list.** Ctrl+Enter on a search result emails that person's first address, so search, then Ctrl+Enter, sends mail without opening the contact.
- [ ] **PgUp and PgDn.** These were removed on purpose (commit 46447d0). With a couple of thousand contacts they may be worth having again in the list.
- [ ] **Sync time.** Show "Synced 2 min ago" in the footer, so a stale cache is obvious.

## Getting contacts in and out

- [ ] **Import vCards.** `peeps import file.vcf` and a drop target in the window, so a card that arrives over LocalSend lands in Google in one step.
- [ ] **Launcher search.** An Omarchy shell plugin so Super+Space finds a contact and copies or emails it without opening the window.
- [ ] **Birthdays.** An upcoming-birthdays view, or `peeps birthdays` feeding `omarchy reminder`.

## Google

- [ ] **Labels.** Filter by Google contact labels (contactGroups).
- [ ] **Duplicates.** Find likely duplicates and merge them.
- [ ] **Expired sign-in.** When Google revokes the refresh token, offer a Reconnect button instead of the raw error.

## Before a public release

- [x] **License.** MIT, like Omarchy.
- [x] **Screenshots.** `make screenshots` draws the README's pictures from fictional contacts.
- [x] **App id.** The window class and desktop file are now `org.omelia.Peeps`.
- [x] **Google client.** Peeps ships its own client, built in with `PEEPS_GOOGLE_CLIENT_ID`. The account screen no longer asks for one.
- [ ] **Verification.** Branding is verified. Data access for the contacts scope was submitted on October 14, 2026 with the demo video (https://youtu.be/WobFZxedkRk). Waiting on Google.
- [x] **Google branding.** The consent screen's app name is Peeps, with https://omelia.org/peeps and its privacy policy.
- [ ] **Label boxes.** Show a down-arrow on the email, phone, and address label boxes so they read as choices.
- [x] **Public repo.** github.com/duff/peeps is public.
- [x] **README.** It says why Peeps exists and which builds carry a Google client, and no longer has the Hyprland binding example.
- [x] **Clipped text.** Dialogs size themselves for their wrapped text, so a second line no longer gets cut off on Wayland, and Keybindings shows one line per binding.
