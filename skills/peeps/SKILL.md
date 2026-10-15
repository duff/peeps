---
name: peeps
description: >
  Look up, add, and change the user's contacts with the `peeps` command, which
  reads and writes their Google Contacts. Use for anything about people in their
  address book: finding someone's email, phone, address, birthday, or company;
  adding a person from an email signature, a business card, or an invite;
  updating or tidying contacts; exporting vCards. Triggers: contact, contacts,
  address book, phone number, email address, birthday, "add this person",
  "who do I know at", vCard, Peeps.
---

# Peeps

`peeps` is the command line of Peeps, an address book for the user's Google
Contacts. The same contacts appear on their phone, so a change here is a change
everywhere. Start with `peeps help` or `peeps help COMMAND` if anything below is
unclear.

## Reading is local, writing is Google

- `list`, `search`, `show`, and `export` read a copy on this computer. They are
  instant and change nothing. Use them freely.
- `add`, `update`, and `delete` change the user's Google account and their
  phone. `sync` downloads Google's latest into the local copy.
- If the answer depends on a very recent change, such as one just made on the
  phone, run `peeps sync` first.

A pipe prints JSON and a terminal prints text, so pass `--json` when you will
read the result.

```bash
peeps search maya --json                # {"query","total","count","contacts":[{"id","name","email","phone","organization","score"}]}
peeps show people/c123 --json           # every field of one contact
peeps list --json | jq '.contacts[]'    # everyone: id, name, email, phone, organization
peeps export people/c123 > maya.vcf     # a vCard
```

An ID is a resource name (`people/c123`), a unique full name, or a unique
email. Part of a name, such as a first name, is not an ID, and neither is a full
name two people share: the command exits 1 and lists the contacts it could
mean, with their ids. Exit 4 means nothing matched at all. When unsure, search
first and use the `id` from the result.

Exit codes: 0 success, 1 failed, 2 usage, 3 not signed in, 4 no contact matched.
On 3, tell the user to open Peeps and connect from Menu → Google account.

## Privacy

What you read from `peeps` goes into this conversation. Read only what the task
needs: `search` for a person, and `jq` to keep only the fields you need from
`list`. Do not dump the whole address book to answer a question about a few
people.

## Before changing anything

A request to add, update, or delete is the proposal, not the agreement. Even
"add this email to Maya" needs a yes before anything changes.

1. Look up the contact and its current values.
2. In that same reply, say exactly what will change: who, which fields, old
   and new values. Include every choice you would otherwise make yourself,
   such as a label, or which of several matching contacts.
3. Stop. Apply the change only after the user agrees to that statement in a
   later message, such as "yes" or "do it". If their reply still leaves a
   choice you did not state, state it and wait again.
4. Delete only when the user asked to delete. `peeps delete ID --yes` is
   permanent.
5. Before adding someone, `peeps search` for them. If they may already be
   there, ask whether to update that contact instead.

## Lists: replace or append

`--email`, `--phone`, `--url`, `--address`, and the other repeatable flags
**replace the whole list**. To keep what a contact has and add to it, use
`--add-email`, `--add-phone`, `--add-url`, or `--add-address`. They skip a value
the contact already has.

```bash
peeps add --given Maya --family Okafor --company "Northwind Studio" \
  --email work=maya@northwind.example --phone mobile="+1 416-555-0142"
peeps update people/c123 --add-email home=maya@example.com
peeps update people/c123 --title "Design lead" --notes "Met at the Omarchy meetup."
peeps update people/c123 --email work=new@northwind.example   # replaces every email
```

Labels are words like `home`, `work`, `mobile`, or any custom word. Never
leave a label blank on your own: when the user did not say one, propose one in
your statement, or ask. An address
is `LABEL|STREET|CITY|REGION|POSTAL|COUNTRY`. A birthday is `YYYY-MM-DD`, or
`MM-DD` without a year.

For anything the flags do not cover, or for several fields at once, edit the
JSON from `show` and send it back with `--data`. It replaces the fields it
includes:

```bash
peeps show people/c123 --json > maya.json
# edit maya.json
peeps update people/c123 --data maya.json
```

Fields left out of an update stay as they are. `peeps help update` lists every
flag.

## Tidying many contacts

For a change across many contacts, such as phone formats, missing country
codes, or likely duplicates, first list every proposed change and let the user
review it. Then apply them one `update` at a time and report any that failed.
Peeps has no merge command: for duplicates, update the contact to keep and
delete the other only once the user agrees.
