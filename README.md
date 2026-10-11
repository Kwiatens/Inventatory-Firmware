<div align="center">

<a href="https://kwiatens.github.io/Inventatory-Site/">
  <img src="branding/inventatory-lockup.svg" alt="Inventatory" width="560">
</a>

<img src="branding/scanner-firmware.svg" alt="Scanner Firmware" width="560" height="22">

**Open-source firmware for the Inventatory Scanner R1 device.**

Purpose made scanner for my [Inventatory](https://github.com/Kwiatens/Inventatory-Software) system.
It allows you to interact with your inventory, without having to constantly go back and forth to your computer during PCB work.

![Release](https://img.shields.io/badge/release-v0.1.0-58B9B0?style=flat-square&labelColor=0D1010)
[![License](https://img.shields.io/badge/license-GPL--3.0-58B9B0?style=flat-square&labelColor=0D1010)](LICENSE)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-58B9B0?style=flat-square&labelColor=0D1010)
![C++17](https://img.shields.io/badge/C%2B%2B-17-58B9B0?style=flat-square&labelColor=0D1010)

[**Website**](https://kwiatens.github.io/Inventatory-Site/) &nbsp;·&nbsp;
[Documentation](https://kwiatens.github.io/Inventatory-Site/docs/) &nbsp;·&nbsp;
[Report an issue](https://github.com/Kwiatens/Inventatory-Firmware/issues)

</div>

---

> **Status:** pre-release (`0.1.0`, see [CHANGELOG.md](CHANGELOG.md)).

## Build the scanner

Visit [**Website**](https://kwiatens.github.io/Inventatory-Site/) for documentation and guides on how to build the scanner.

All the information that's below is only useful, when you want to manually compile the firmware.

## Build

Use PlatformIO. Create the private configuration from the tracked example, then build. On Windows, use `Copy-Item` instead of `cp`.

```bash
cp src/config/config.example.h src/config/config.h
pio run -e tenstar_esp32s3_zero_usb
```

## Test

```bash
make -C host-tests test
pio test -e tenstar_esp32s3_zero_usb --without-uploading --without-testing
```

## Documentation

- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)
- [Architecture and invariants](AGENTS.md), also the guide for coding agents
- [Changelog](CHANGELOG.md)
- [Release steps](docs/releasing.md)

## License

GPL-3.0-only. See [LICENSE](LICENSE). Dependency licenses are here [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
