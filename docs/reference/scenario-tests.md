# Scenario tests

Status: design (2026-10-05). This is the target design: the document comes
first and the implementation follows it.

Scenario tests describe, in plain language, what a person does with the system
and what they should see. They are kept in the source tree under
[tests/](../../tests/README.md) and are treated like source code: reviewed,
versioned and kept up to date. Each scenario can be run by an agent on a real
machine or in QEMU (the Agent Acceptance Test, AAT), and by a person when a
step needs hands or judgement.

## Layout

```text
tests/
  README.md                 how the tree is organised and how to run it
  scenarios/
    os/<area>/<name>.md     operating-system functions: boot, login, network,
                            storage, power, input, sound, security keys ...
    desktop/<area>/<name>.md the desktop: windows, bar, App Home, keyboard,
                            input method, notifications, appearance ...
    apps/<app>/<name>.md    each application: files, settings, phone, ...
  suites/
    <name>.suite            named sets of scenarios
```

The identifier of a scenario is its path without the extension, with `/`
written as `.`: `apps/files/mount-confirm.md` is `apps.files.mount-confirm`.

## A scenario

A scenario is a Markdown file with a header block and four sections.

```markdown
---
id: apps.files.mount-confirm
title: Mounting a USB stick asks for confirmation
status: active            # draft | active | retired
areas: [files, volumed]
paths: [userland/desktop/files/, userland/base/volumed/]
machine: either           # qemu | hardware | either
human: none               # none | look | hands
since: ws132-p009
---

## Setup
What must be true before the first step (the image, a plugged device, a
logged-in user, files that exist).

## Steps
1. Open Files from App Home.
   Expect: the Files window opens on Today.
2. Double-click the USB stick under Devices.
   Expect: a card asks "Mount "STICK"?" with the size and the file system,
   and offers Cancel and Mount.
3. ...

## Pass
The conditions that decide the verdict, each observable in a screenshot or a
log line.

## Notes
Known limits, links to the bug or the work item, what a person must look at.
```

- **Steps** are written for a person. Each step has one action and, when it
  matters, an `Expect:` line. They avoid pixel coordinates: they name what to
  click ("the Mount button", "the second row") so that the scenario survives
  layout changes and different screen sizes.
- **status**: `draft` is a scenario written before the feature exists (test
  first); `active` is expected to pass; `retired` is kept for history.
- **areas** and **paths** connect a scenario to the parts of the system it
  exercises. They are what regression selection uses.
- **machine** says where it can run. **human** says whether a person is
  needed: `look` for a judgement on a screenshot (does it look right?),
  `hands` for physical actions (press the power button, close the lid, plug a
  device, touch a security key). An agent runs every step it can and leaves
  the rest marked for a person.

## Suites

A suite file lists scenarios, one per line, by identifier or by pattern, with
`#` comments. Patterns match identifiers (`apps.files.*`) or areas
(`area:volumed`).

```text
# smoke: a few minutes, run on every image
os.boot.login
desktop.bar.clock
apps.files.open-today
```

Suites in use:

| Suite | Purpose |
| --- | --- |
| `smoke` | A few minutes. Every test image, before anything else. |
| `<area>` and `<app>` | Everything for one area or one application, for work on it. |
| `full` | Every active scenario. A full regression run, from time to time and before a release. |
| `hardware` | The scenarios that need the real machine. |

## Choosing what to run

- **New work**: the scenarios for a new feature are written first, as `draft`,
  and the feature is done when they pass and become `active`.
- **A change**: the scenarios whose `paths` cover the changed files, plus
  `smoke`. A helper lists them from a git range.
- **A bug fix**: a scenario that reproduces the bug is added or extended, so the
  fix stays fixed.
- **Before a release, or periodically**: `full`.

## Running

The agent reads the scenario, drives the machine with the AAT tool
(`plan/tools/aat/aat`: click, type, key, screenshot, wait for a log line) and
records for each step what it did, what it saw and a screenshot. The verdict is
`pass`, `fail` (with the step and the evidence) or `needs-person` (a `look` or
`hands` step). A run of a suite produces a summary with every verdict and the
screenshots.
