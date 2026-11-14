# Verschlüsselte binäre Snapshots (FA-19) – Umsetzung v0.9

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.9) |
| Bezug | Anforderungen FA-19 (Kann), NFA-06 (keine externen Abhängigkeiten) |

## API

```cpp
db.saveToFileEncrypted("backup.enc", "passwort");
Database db2 = Database::loadFromFileEncrypted("backup.enc", "passwort");
```

`CryptoError` bei: leerem Passwort, falschem Passwort, beschädigter Datei
(HMAC-Prüfung), falscher Magic. `Error` bei IO-Fehlern, `JsonError` bei
ungültigem entschlüsseltem Inhalt.

## Dateiformat (binär, little-endian)

```
offset 0   : Magic 8 Bytes  "INMEMDB\x01"
offset 8   : Salt 16 Bytes (zufällig pro Datei)
offset 24  : PBKDF2-Iterationen uint32 LE (100.000)
offset 28  : ChaCha20-Nonce 12 Bytes (zufällig pro Datei)
offset 40  : Ciphertext (ChaCha20, Länge = JSON-Länge)
letzte 32  : Tag = HMAC-SHA256(macKey, alle vorherigen Bytes)
```

- **Schlüsselableitung:** PBKDF2-HMAC-SHA256(passwort, salt, 100.000) → 64 Bytes:
  32 Bytes Verschlüsselungsschlüssel (ChaCha20), 32 Bytes MAC-Schlüssel.
- **Verschlüsselung:** ChaCha20 (RFC 8439), Counter startet bei 1.
- **Integrität:** encrypt-then-MAC; Tag-Vergleich in konstanter Zeit.
- Payload ist der normale JSON-Snapshot (`toJson()`), inkl. Index-Definitionen.

## Implementierung ohne externe Abhängigkeiten
`src/crypto.cpp`: SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, ChaCha20, Zufall
via `std::random_device` – alles selbst implementiert und gegen bekannte
Testvektoren geprüft (SHA-256("abc"), HMAC RFC 4231 TC1, ChaCha20 RFC 8439
Keystream- und Verschlüsselungs-Vektor, PBKDF2-HMAC-SHA256 c=1/c=2).

## Sicherheitshinweise / Limitationen (v0.9)
- Passwort-basiert; Stärke hängt vom Passwort ab (PBKDF2 erschwert Offline-Angriffe).
- Kein zeroization sensibler Schlüssel im Speicher; kein FIPS/CC-zertifiziertes Modul.
- Kein Schlüsselmanagement (keine KEK/DEK-Hierarchie) – Ausblick.
