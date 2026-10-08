# kwallet-icloud-import

**English** · [Deutsch](#deutsch)

[![CI](https://github.com/Tojori/kwallet-icloud-import/actions/workflows/ci.yml/badge.svg)](https://github.com/Tojori/kwallet-icloud-import/actions/workflows/ci.yml)

Imports the passwords you exported from **Apple Passwords / iCloud Keychain** into your **KDE Wallet**: one-way and repeatable.

There is no live sync: Apple offers no Linux client or public API for iCloud Keychain. What works is exporting a CSV file on a Mac and importing it here. Run it again whenever you want to bring changes over; changed entries are updated, unchanged ones are left alone.

## Usage

1. On the **Mac**: open *Passwords* → *File* → *Export All Passwords to File…* and save the CSV. (iPhone/iPad cannot export files.)
2. Copy the CSV to the Linux machine securely (e.g. a USB stick; avoid cloud drives and messengers), then:

```sh
kwallet-icloud-import --dry-run --list Passwords.csv   # preview, writes nothing
kwallet-icloud-import --delete-csv Passwords.csv        # import, then overwrite + delete the CSV
```

Entries land in the wallet folder **"iCloud Passwords"** as map entries named `<host> (<username>)` with the fields `Title`, `URL`, `Username`, `Password`, `Notes`, `OTPAuth` (TOTP secret as `otpauth://` URI) and `Source`. Browse them with **KWalletManager**.

| Option | Effect |
|---|---|
| `--dry-run` | show what would change, write nothing |
| `-l`, `--list` | list entry names (never passwords) |
| `--folder NAME` | target folder (default `iCloud Passwords`) |
| `--wallet NAME` | target wallet (default: your local wallet) |
| `--prune` | delete entries that were imported earlier but are no longer in the export (asks first; `--yes` skips the question). Entries you created yourself are never touched. |
| `--delete-csv` | overwrite the CSV with zeros and delete it after a successful import |

## Security notes

- The CSV contains **all passwords in plain text**. Delete it right after the import (`--delete-csv`). On copy-on-write file systems (btrfs, ZFS) and SSDs, overwriting cannot guarantee that old blocks are gone; full-disk encryption is the real protection.
- Passwords are never printed. The tool does not use the network.
- Passkeys are not part of Apple's CSV export. For passkeys see [kpasskey](https://github.com/Tojori/kpasskey).

## Build

Dependencies (Arch/CachyOS): `cmake qt6-base kwallet`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j && ctest --test-dir build
sudo cmake --install build      # installs /usr/local/bin/kwallet-icloud-import
```

License: LGPL-2.1-or-later.

---

## Deutsch

Importiert die aus **Apple Passwörter / iCloud-Schlüsselbund** exportierten Passwörter in dein **KDE Wallet**, einseitig und wiederholbar.

Eine laufende Synchronisation gibt es nicht: Apple bietet für den iCloud-Schlüsselbund weder einen Linux-Client noch eine öffentliche Schnittstelle. Möglich ist der CSV-Export am Mac und der Import hier. Beim erneuten Ausführen werden geänderte Einträge aktualisiert, unveränderte bleiben unberührt.

**So geht's:**
1. Am **Mac**: *Passwörter* → *Ablage* → *Alle Passwörter in Datei exportieren …* und die CSV speichern. Auf iPhone/iPad gibt es keinen Datei-Export.
2. Die CSV sicher auf den Linux-Rechner bringen, dann:

```sh
kwallet-icloud-import --dry-run --list Passwörter.csv   # Vorschau, schreibt nichts
kwallet-icloud-import --delete-csv Passwörter.csv        # importieren, danach CSV überschreiben und löschen
```

Die Einträge landen im Wallet-Ordner **„iCloud Passwords“**. Jeder Eintrag heißt `<host> (<benutzer>)` und enthält die Felder `Title`, `URL`, `Username`, `Password`, `Notes`, `OTPAuth` und `Source`. Ansehen kannst du sie im **KWallet-Manager**. `--prune` entfernt früher importierte Einträge, die in iCloud gelöscht wurden. Es fragt vorher nach und fasst selbst angelegte Einträge nie an.

**Sicherheit:** Die CSV enthält **alle Passwörter im Klartext**, also sofort nach dem Import löschen (`--delete-csv`). Auf btrfs/ZFS und SSDs garantiert Überschreiben nicht, dass alte Blöcke verschwinden; der eigentliche Schutz ist Festplattenverschlüsselung. Passwörter werden nie ausgegeben, und es gibt keinen Netzwerkzugriff. Passkeys sind nicht im Apple-CSV enthalten, dafür gibt es [kpasskey](https://github.com/Tojori/kpasskey).
