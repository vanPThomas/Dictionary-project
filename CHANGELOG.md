# Changelog
All notable changes to **Freia Thiwi Client** will be documented here.

## [0.5.4] - 2026-02-12
### Changed
- Added sendWithLengthPrefix function for cleaner code.

----

## [0.5.3] - 2026-02-10
### Changed
- Refactored ClientConnect.cpp

---

## [0.5.2] - 2026-02-09
### Changed
- Refactored FreiaUI.cpp

---

## [0.5.1] - 2026-02-08
### Changed
-Upgraded from c-style arrays to proper c++ strings for better legible code

---

## [0.5.0] - 2026-02-07
### Added
- Account-based authentication: login with existing account or create new one
- Tabbed connection panel: "Login" (existing account) and "Create Account" (new account with password confirmation)
- Account password field + confirmation on creation
- Derived account key sent via new PROT4 protocol after PROT2 handshake
- Handles PROT4 SUCCESS/FAIL replies from server with clear messages
- Full round-trip support for account creation and login

### Changed
- `configureWithAccount()` now takes `bool isCreate` flag to distinguish login vs. creation
- Connection flow: PROT2 (transport + username) → PROT4 (account auth) → normal chat
- UI validation for account password fields

### Fixed
- Payload parsing for multi-line PROT3 messages (userList now reads all names correctly)

This version brings real user accounts to Freia Thiwi, making it possible to have persistent, per-user identities while keeping the ultra-lightweight spirit intact.

---

## [v0.4.5] - 2026-01-30
### Changed
- Cleaned up code

---

## [v0.4.4] - 2026-01-28
### Fixed
- Fixed bug where all online people wasn't properly shown

---

## [v0.4.3] - 2026-01-26
### Added
- Added element that shows what people are connected to the server

---

## [v0.4.2] - 2026-01-25
### Added
- FreiaUI: added an options menu in the menu bar
- FreiaUI: Added themed colors

---

## [v0.4.1] - 2026-01-23
### Added
- Added PROT3 recognition for server messages

---

## [v0.4.0] - 2026-01-22
### Added
- Encrypted handshake (PROT2) sent immediately after TCP connect
  - Contains username, encrypted with serverSessionKey
  - Waits synchronously for server's encrypted "PROT2" reply
- Full server authentication verification
  - Connection only succeeds if server reply decrypts correctly and contains expected "OK"
  - Clear error messages on failure ("wrong server password?", incomplete reply, etc.)
- Handshake is now the first step — normal chat messages (PROT1) only sent after successful auth

### Changed
- `connectToServer()` now performs full handshake before starting receive thread
- Improved connection feedback in chat window during auth phase

### Security
- Username never appears in plaintext on the wire
- Immediate failure feedback if server password is incorrect

---

## [0.3.4] - 2026-01-18
### Changed
- FreiaUI: Proper text wrapping in the chat window

---

## [0.3.3] - 2026-01-15
### Changed
- FreiaUI: Cleaned up UI for better user experience

---

## [0.3.2] - 2026-01-14
### Changed
- ClientConnect: Refactored sendMessage function
- ClientConnect: dedicated PROT1 building function
- Validation: added username sanitization
- Validation: improved user name validation


---

## [0.3.1] - 2026-01-13
### Changed
- ClientConnect: better parameter handling via ConnectionParams struct
- ClientConnect: safer newline splitting implementation

---

## [0.3.0] - 2025-12-02
### Added
- Added Protocol and package framing
- Added Server password and server encryption
- Added Protocol one (PROT1)

---

## [0.2.3] - 2025-11-21
### Changed
- Fixed double shutdown bug

---

## [0.2.2] - 2025-11-21
### Added
- Added Error popup windows
- Added Auto scroll down in chat window

---

## [0.2.1] - 2025-11-19
### Changed
- Optimized encryption and decryption

---

## [0.2.0] - 2025-11-18
### Added
- End to End Encryption has been added
- All messages coming and going are now secure and even unreadable by the server.

---

## [0.1.4] - 2025-11-18
### added
- Added validation class
- Added UI and networking validation.
- If connection is not made in 3 seconds, there is a time out
- Added a disconnect button when connected
- Added menu bar with exit button

---

## [0.1.3] - 2025-11-17
### Added
- UI class
- Moved all ui logic to FreiaUI class
- Clean separation of the connection class and the UI class

---

## [0.1.2] - 2025-11-17
### Added
- Client class
- separated UI from connectivity logic.
- cleaned up connectivity code

---

## [0.1.1] - 2025-11-16
### Changed
- Separated the UI logic from the networking logic in preparation of separate class creation.

---

## [0.1.0] - 2025-11-16
### Added
- Initial project structure with CMake build system
- Basic ImGui-based UI: connection panel + chat window
- Socket connection to Freia Thiwi server
- Real-time message sending and receiving
- Threaded message listener
- Minimal mutex locking for thread-safe UI updates
- Fixed font loading path

### Removed
- All Windows-specific networking code (Linux-only for now)
- Legacy AES encryption logic (to be reintroduced and improved later)

### Known Limitations
- No user presence/identity validation
- No reconnection handling after server disconnect

