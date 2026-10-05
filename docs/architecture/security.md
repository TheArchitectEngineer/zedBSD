# Security design

Status: design (2026-10-05). This is the target design: the document comes
first and the implementation follows it, so some of what it describes is not
built yet.

This document records where zedBSD and Keiland draw their privilege
boundaries and why. It covers account administration and the authentication
of the graphical login. Later sections will cover the other privileged paths.

## Principles

- **The desktop holds no privilege.** The compositor, Settings and the other
  Keiland programs run as the logged-in user. An action that needs root goes
  through a small privileged program whose whole job is that action, and that
  program checks the request itself. It never trusts the caller.
- **A privileged program is small and has one job.** It reads a fixed request
  format, checks the caller and the request, does the work and exits. There
  is no general command channel and no shell.
- **The administrator proves presence.** An administrative change needs the
  administrator's own password at the time of the change, the same as `sudo`.
  A logged-in session alone is not enough, because an unlocked, unattended
  session must not be able to create accounts.
- **Administrators are the wheel group.** A user is an administrator when
  `wheel` is the user's primary group or names the user. root is not managed
  from the desktop.
- **Every privileged request is logged** to syslog's `auth` facility with the
  caller, the operation and the result. Passwords and hashes are never logged.
- **System files are replaced atomically.** A reader sees the old file or the
  new one, never a part (see [Shared account core](#shared-account-core)).

## Account administration

### What it covers

An administrator can do the following from Settings > Users:

- add a user, with a name, a display name, a password and an administrator
  switch;
- remove a user, keeping the home directory by default;
- reset another user's password;
- add a user to or remove a user from the `wheel` and `network` groups.

Every user can see the list of users. Every user can change their own
password; that path is separate (`passwd -s`, below).

### The path

```text
Settings (user)  ->  compositor (user)  ->  libkeiland-backend (user)
                                              |
                                              | runs as a child, request on stdin
                                              v
                                       account-admin (set-user-ID root)
                                              |
                                              v
                              /etc/passwd, /etc/shadow, /etc/group, /home
```

- Settings asks for the operation through a `kl_system_account_*` request on
  the compositor's system extension. Settings itself has no privilege and
  never runs the tool.
- The compositor's operating-system backend starts `account-admin` as a
  child, writes one request to its standard input, reads one answer from its
  standard output and waits for it to exit. This is the same pattern as the
  user's own password change through `passwd -s`.
- `account-admin` is the only privileged part. It is a set-user-ID root
  program in the base system (source `userland/base/account-admin/`),
  installed as `/usr/libexec/account-admin`, mode 4555, owned by root. It is
  not on any user's `PATH`, because no one runs it by hand.

The tool was chosen over a new request on `sessiond`, the root session
daemon. `sessiond` could identify the caller from its socket and skip the
password, but each new request widens the interface of a long-running root
daemon. A short-lived tool that checks the password and exits keeps the
privileged surface small, can be audited on its own, and follows the same
rule as `sudo`: the administrator types their password for each change.

### Request format

The request is text on standard input, one field per line, ending at end of
file:

```text
<caller's password>
<operation>
<arguments, one per line>
```

| Operation | Arguments |
| --- | --- |
| `add` | name, display name, new password, `admin` or `user` |
| `remove` | name, `keep-home` or `remove-home` |
| `reset-password` | name, new password |
| `group-add` | name, group (`wheel` or `network`) |
| `group-remove` | name, group (`wheel` or `network`) |

Nothing comes from the command line or the environment. The tool ignores
`argv` beyond its name and clears its environment before it starts.

The answer is one line on standard output, `ok` or `error <reason>`, and the
exit status is 0 or 1. The reasons are fixed words, such as
`not-administrator`, `bad-password`, `no-such-user`, `name-taken`,
`bad-name`, `weak-password`, `last-administrator`, `self`, `root`, `busy` and
`home-exists`,
so that Settings can show a message in the user's language.

### Checks, in order

1. **The caller.** The caller is the real user ID. The tool looks it up in
   `/etc/passwd`. A real user ID of 0 is refused, because root administers
   with the base tools, not this path.
2. **Administrator.** The caller must be in `wheel`. Otherwise the answer is
   `not-administrator`, and the password is not checked, so the tool cannot
   be used to test passwords of non-administrators.
3. **Password.** The caller's password must match the hash in
   `/etc/shadow`. A wrong password is answered after a fixed delay of 2
   seconds, and the failure is logged.
4. **The target.**
   - root and system accounts (user ID below 1000) are refused (`root`).
   - An administrator cannot remove themselves or take themselves out of
     `wheel` (`self`).
   - The last administrator among the people's accounts (user ID 1000 and
     up) cannot be removed or taken out of `wheel` (`last-administrator`).
     root does not count: it is always in `wheel` but is locked in a release
     build, so the machine always keeps an administrator who can log in.
5. **The values.**
   - A new name is a lower-case letter followed by up to 31 lower-case
     letters, digits, `-` or `_`, and must not be taken (`bad-name`,
     `name-taken`).
   - A display name may not contain `:` or a newline.
   - A new password follows the shared password rules (at least 8
     printable characters).
   - The group is `wheel` or `network`; no other group can be changed
     here.

Only after every check passes does the tool change anything.

### What each operation changes

- **add:** takes the lowest number from 1000 up that is free both as a user
  ID and as a group ID, and gives the user a private group of the same name
  and number. Writes the `/etc/passwd`, `/etc/group` and
  `/etc/shadow` lines (SHA-512 crypt), and adds the user to `wheel` when
  asked. Creates `/home/<name>` with mode 0700, owned by the new user, and
  copies the skeleton files from `/etc/skel` into it, when there are any:
  only the regular files directly in it, owned by the new user, without
  group and other permissions. If `/home/<name>` already exists (for
  example kept from an earlier removal), the request is refused
  (`home-exists`) and the directory is left alone, so an old home's files
  never pass silently to a new account.
- **remove:** removes the user's lines from the three files and from every
  group's member list. The home directory is kept unless `remove-home` was
  given. It is removed only after Settings has asked the administrator to
  confirm, and the tool never follows a symbolic link while removing it.
  A user who has a running process (logged in, or a program left running)
  cannot be removed (`busy`). The tool looks for any process with that
  user's ID in the kernel's process list, and for a login record naming
  them.
- **reset-password:** replaces the user's hash. The user is not told the old
  one, and no one can read it.
- **group-add / group-remove:** edits the member list of `wheel` or
  `network` in `/etc/group`.

### Robustness

- All three files are changed under the shared lock, in the order `group`,
  `passwd`, `shadow` for an addition and the reverse for a removal. Each is
  replaced atomically. A crash between files leaves at most an unused group
  or an account without a password entry, which cannot log in.
- Signals that would stop the tool midway are held during the change.
- The tool reads a bounded request (4 KiB). Longer input is refused before
  any check.
- Password buffers are cleared before the tool exits.

### Other operating systems

On Linux and FreeBSD the Keiland ports show the list of users, but the
administrative operations answer "not supported here". Those systems have
their own account tools (AccountsService, `pw`), and wrapping them is a
separate design.

## Login authentication

### What it covers

The graphical login screen and the lock screen accept three kinds of
credential, called styles:

- **password**: the account's password, as everywhere else;
- **pin**: a six-digit PIN the user sets in Settings > Users;
- **fido2**: a FIDO2 security key the user registers in Settings > Users,
  touched and unlocked with the key's own PIN, over USB or NFC.

The PIN and the security key are conveniences for the person at the machine.
They are never accepted by `login` on the console, `su`, `sudo`, `passwd` or
SSH, which take the password only.

### The parts

```text
greeter / lock screen (no privilege)
        |  one request per line on its socket pair
        v
sessiond (root, resident)  -- chooses the style, counts failures, delays
        |  request on stdin, answer on stdout
        v
/sbin/passkey (root, short-lived)  -- checks one request, or changes /etc/passkey
        |  the device descriptors only
        v
device helper (_passkey, chroot /var/empty)  -- talks CTAP to the security key
```

- **sessiond** never checks a credential itself. For each attempt it starts
  `/sbin/passkey`, writes the request to its standard input and reads the
  answer from its standard output. It keeps the failure counts and applies
  the delays. The account it names is the one the greeter chose for a login,
  and always the session's own user for an unlock or a change.
- **`/sbin/passkey`** is a base program, mode 0500, owned by root and not
  set-user-ID. It refuses to run unless its real user ID is 0. It reads one
  bounded request (4 KiB), does that one thing and exits. Nothing comes from
  its command line or its environment.
- **The device helper** is a child of passkey for the security key style. It
  runs as the `_passkey` account inside an empty root directory and holds
  only the descriptors of the key's device nodes, which passkey opened. It
  sends the key its request and returns the key's answer as bytes. It does
  not read `/etc/passkey`, does not choose the challenge and does not decide
  whether the answer is good.
- **The check** is done by passkey as root on the bytes the helper returns,
  against the public key stored for the credential the answer names.

### The request

The request is text, one field per line, ending at end of file:

```text
<operation>
<account name>
<style>             (auth only)
<secret>            (auth: the password, the PIN, or the key's PIN;
                     the other operations: the user's current password)
<arguments>
```

| Operation | Arguments |
| --- | --- |
| `auth` | none |
| `styles` | none (and no secret): the styles the account has enrolled |
| `enrolled` | none (and no secret): the account's PIN and keys, without secrets |
| `enroll-pin` | the new PIN |
| `remove-pin` | none |
| `enroll-fido2` | a label, the key's PIN |
| `remove-fido2` | the credential's ID |

The answer is zero or more `status touch` lines (the user should touch the
key), then `ok` (with the credential's ID after `enroll-fido2`) or
`fail <reason>`. The reasons are fixed words: `bad-secret`, `no-such-user`,
`not-enrolled`, `locked-account`, `no-key`, `timeout`, `device`, `replay`,
`bad-request`, `busy` and `internal`. The exit status is 0 for `ok`, 1 for
`fail` and 2 for an internal error. sessiond kills a passkey that runs longer
than 5 seconds for a password or a PIN, or 35 seconds for a security key.

### Failure counts and delays

sessiond counts the failures in a row for each account and style in memory.
They are never written to a file, so that an attempt leaves no trace an
attacker could time or watch. After a failure the answer waits: 2 seconds,
doubling after every three failures in a row, up to 16 seconds, for every
style of the account together.

After five wrong PINs in a row, the PIN is turned off for that account: the
login screen no longer offers it and a PIN attempt fails at once. A
successful password or security key login turns it on again. The counts are
lost when sessiond restarts; to slow down an attacker who could make it
restart, sessiond accepts no PIN during its first 60 seconds. A security key
counts its own wrong PINs and locks itself after eight.

### /etc/passkey

The enrolled credentials live in `/etc/passkey`, owned by root with mode
0600, beside `/etc/passwd` and `/etc/shadow`, which do not change. It is
replaced atomically under the shared account lock, like `/etc/shadow`.

```text
# zedBSD passkey 1
<name>:pin:<SHA-512 crypt hash>
<name>:fido2:<credential ID>:<COSE public key>:<signature count>:<relying party>:<label>:<date>
```

The credential ID and the key are base64url. An account has at most one PIN
and five security keys; a label has at most 32 characters and no `:`. A line
of a kind passkey does not know is kept as it is when the file is rewritten.
Removing an account removes its lines.

### The PIN

A PIN is exactly six decimal digits, hashed like a password (SHA-512 crypt).
Setting, changing or removing it requires the account's current password. An
account whose password is locked cannot have a PIN. Six digits give a
million combinations; the protection is the file's permissions and the
limit of five attempts, not the hash.

### The security key

The relying party is `zedbsd.login`, a name that cannot collide with a web
site's. Registration asks the key for a new non-resident ES256 credential
(COSE algorithm -7) with user verification, and stores its ID and public
key; the key's attestation is not checked, so the make of the key is not
part of the trust. A key without a PIN of its own cannot be registered.

To log in, passkey makes a random 32-byte challenge and the client data hash
`SHA-256("zedbsd.login" NUL name NUL challenge)`, and the helper asks every
security key present for an assertion over the account's registered
credentials, using the first key the user touches. passkey then:

1. finds the public key by the credential ID in the answer, among the
   account's own registrations;
2. checks that the authenticator data's relying party hash is the hash of
   `zedbsd.login`, and that its flags say the user was present and verified;
3. checks the signature over the authenticator data and its own client data
   hash;
4. checks that the signature count is 0 or larger than the one stored, and
   stores the new one (a count that goes back is answered `replay`: the key
   may have been copied).

## Shared account core

`passwd`, `su`, `sudo` and `account-admin` share one implementation of the
account rules (`userland/base/common/account.c`):

- **Passwords:** at least 8 printable characters when a user chooses one,
  and different from the one they replace. Hashes are SHA-512 crypt with a
  high round count and 16 random characters of salt from `getentropy`.
- **File replacement:** `/etc/shadow` and the other files are never written
  in place. Under an exclusive lock file, the whole file is read, the change
  is applied, the result is written to a new file in `/etc` with the
  original's mode, synced, and renamed over the original. Then `/etc` is
  synced.
- **wheel:** a user is in `wheel` when it is the primary group or the group
  names the user.
- **Environment:** a command run as another user keeps only the terminal and
  locale variables of the caller and sets `HOME`, `SHELL`, `USER`, `LOGNAME`
  and a fixed `PATH`. `LD_*`, `IFS`, `ENV` and the rest never pass through.
