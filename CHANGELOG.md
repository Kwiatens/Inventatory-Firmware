# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Holding `#` opens the re-pair confirmation (idle only); `C` returns to Ready.
- CI logging gate fails closed and has real patterns; jobs have timeouts.

### Changed
- Result dwell timer is cancelled by every screen change.
- Passkey rotates after every attempt, three failures or five minutes lock pairing until `A`.
- OTA ignored in setup, two minute wait timeout, `C` cancels.
- Counters are reserved in blocks of 32.
- `null` optional sync objects are accepted as absent.
- Legacy migration only without a current header; bounded legacy string copies.
- A typed `0` can no longer produce a hidden default quantity.
- Operator-confirmed discard of a stuck head event.

### Fixed
- Local save failures preempt stale results.
- Bonds removed, stack calls stay on Core 1 for BLE setup.
- Placeholder password rejected at build time.

### Security
- Transport HTTP status is published only after the response MAC verifies; a missing counter key fails closed.

## [0.1.0]
*(Not yet released)*
