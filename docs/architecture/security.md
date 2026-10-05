# Security design

Status: design (2026-10-05). This is the target design: the document comes
first and the implementation follows it, so some of what it describes is not
built yet.

This document records where zedBSD and Keiland draw their privilege
boundaries and why. It begins with account administration. Later sections
will cover the other privileged paths.

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
