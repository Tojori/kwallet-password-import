# kwallet-password-import

**English** · [Deutsch](#deutsch)

[![CI](https://github.com/Tojori/kwallet-password-import/actions/workflows/ci.yml/badge.svg)](https://github.com/Tojori/kwallet-password-import/actions/workflows/ci.yml)

Imports passwords from **Apple Passwords / iCloud Keychain, browsers and password managers** into your **KDE Wallet**: a native KDE wizard plus a command line tool. One-way and repeatable: running it again updates changed entries and leaves unchanged ones alone.

## Supported exports (detected automatically)

| Source | File | How to export |
|---|---|---|
| Apple Passwords / iCloud Keychain | CSV | Mac: *Passwords › File › Export All Passwords to File…* (iPhone/iPad cannot export) |
| Firefox | CSV | `about:logins` › menu *…* › *Export Passwords…* |
| Chrome, Chromium, Edge, Brave, Google Password Manager | CSV | *Password Manager › Settings › Export passwords* |
| Bitwarden / Vaultwarden | CSV or JSON (unencrypted) | *Tools › Export vault* |
| KeePassXC | CSV | *Database › Export › CSV File…* |
| 1Password | CSV | *File › Export › CSV* (best effort; columns matched by name, please check the preview) |
| Proton Pass | CSV | *Settings › Export › CSV* |
| **FIDO Credential Exchange Format (CXF 1.0)** | JSON | any manager exporting the FIDO standard; `basic-auth`, `totp` and `note` credentials are imported |

Passkeys are not imported (they are counted and reported); use [kpasskey](https://github.com/Tojori/kpasskey) for passkeys.

## Graphical wizard

Start **Password Import for KDE Wallet** from the application menu, or right-click an export file in **Dolphin** › *Import passwords into KDE Wallet…*.

1. choose the file (format is recognised, with export instructions),
2. choose wallet and folder (default per source, e.g. "iCloud Passwords"), optionally remove entries deleted at the source and delete the export file afterwards,
3. preview: every entry with *new / changed / unchanged / will be removed*, never showing passwords,
4. import.

The UI is available in English and German.

## Command line

```sh
kwallet-password-import --dry-run --list export.csv     # preview, writes nothing
kwallet-password-import --delete-file export.csv        # import, then overwrite + delete the export
```

| Option | Effect |
|---|---|
| `--format ID` | `auto` (default), `apple`, `firefox`, `chromium`, `bitwarden-csv`, `bitwarden-json`, `keepassxc`, `1password`, `protonpass`, `cxf` |
| `--folder NAME`, `--wallet NAME` | target (defaults: per-source folder, local wallet) |
| `--dry-run`, `-l`/`--list` | preview; list entry names (never passwords) |
| `--prune` (+ `--yes`) | remove entries imported earlier **from the same source** that no longer exist there; entries you created yourself are never touched |
| `--delete-file` | overwrite and delete the export after a successful import |

Entries are KWallet map entries named `<host> (<username>)` with `Title`, `URL`, `Username`, `Password`, `Notes`, `OTPAuth` (TOTP as `otpauth://` URI) and `Source`; view them in **KWalletManager**.

## Security notes

- Exports contain **all passwords in plain text**; delete them right after the import. On copy-on-write file systems (btrfs, ZFS) and SSDs overwriting cannot guarantee the data is gone; full-disk encryption is the real protection.
- Passwords are never printed or shown; no network access.
- Encrypted exports (e.g. encrypted Bitwarden JSON) are rejected with a hint.
- There is no live sync with iCloud: Apple offers no Linux API.

## Build

Dependencies (Arch/CachyOS): `cmake qt6-base kwallet kcoreaddons ki18n kwidgetsaddons`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j && ctest --test-dir build
sudo cmake --install build
```

Upgrading from `kwallet-icloud-import` 0.1: entries imported by it are recognised (`--prune` treats them as Apple entries).

License: LGPL-2.1-or-later.

---

## Deutsch

Übernimmt Passwörter aus **Apple Passwörter / iCloud-Schlüsselbund, Browsern und Passwortmanagern** in dein **KDE Wallet**, als KDE-Assistent und als Kommandozeilenwerkzeug. Der Import ist einseitig und wiederholbar: Bei erneutem Ausführen werden Änderungen übernommen, Unverändertes bleibt unberührt.

**Unterstützt** (automatisch erkannt): Apple Passwörter (CSV vom Mac), Firefox, Chrome/Chromium/Edge/Brave/Google-Passwortmanager, Bitwarden/Vaultwarden (CSV oder unverschlüsseltes JSON), KeePassXC, 1Password (nach bestem Wissen, bitte die Vorschau prüfen), Proton Pass sowie das FIDO-Standardformat **CXF 1.0**. Passkeys werden nicht übernommen, dafür gibt es [kpasskey](https://github.com/Tojori/kpasskey).

**Grafisch:** *Passwort-Import für KDE Wallet* im Startmenü oder in **Dolphin** per Rechtsklick auf die Exportdatei › *Passwörter ins KDE Wallet importieren …*. Der Assistent hat vier Schritte: Datei wählen, Ziel wählen, Vorschau (ohne Passwörter), Import. Optional entfernt er in der Quelle gelöschte Einträge und löscht die Exportdatei.

**Kommandozeile:** `kwallet-password-import --dry-run --list export.csv` zeigt eine Vorschau, `kwallet-password-import --delete-file export.csv` importiert und löscht die Datei danach.

**Sicherheit:** Exporte enthalten **alle Passwörter im Klartext**, also sofort nach dem Import löschen. Auf btrfs/ZFS und SSDs garantiert Überschreiben nicht, dass die Daten physisch verschwinden; der eigentliche Schutz ist Festplattenverschlüsselung. Passwörter werden nie angezeigt, und es gibt keinen Netzwerkzugriff. Eine laufende Synchronisation mit iCloud gibt es nicht, denn Apple bietet unter Linux keine Schnittstelle.
