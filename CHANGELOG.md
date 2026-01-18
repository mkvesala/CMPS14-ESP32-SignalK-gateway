# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] - 2026-01-09

### Added
- Dedicated OTA_PASS, set in secrets.(example).h and used in CMPS14Application.cpp::initWifiServices()

### Changed
- In SignalKBroker::sendHdgPitchRollDelta() and SignalKBroker::sendPitchRollMinMaxDelta() the delta will be requested from the compass only after ws_open has been checked

## [1.0.0] - 2026-01-02

### Added
- Object-oriented refactored architecture
  - CMPS14Sensor for communicating with the actual sensor
  - CMPS14Processor for main compass logic
  - CMPS14Preferences for persistent storage in ESP32 NVS
  - SignalKBroker for websocket integration to SignalK server
  - DisplayManager for handling LCD and LEDs
  - CMPS14Application to provide the app orchestrating everything
  - DeviationLookup for lookup table for deviation values
- Added attitude leveling feature
- Performance monitoring (heap, loop runtime, stack)
- Performance data, SW/FW versions, system uptime to web ui status block
- Message queue (fifo) for LCD message buffering
- New led states for connection statuses

### Changed
- Complete rewrite from procedural v0.5.x to OOP v1.0.0
- Performance optimization during rewrite

## [0.5.1] - 2025-11-24 (legacy/procedural-0.5.x)

### Added
- Initial procedural implementation

[1.0.1]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.0.1
[1.0.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.0.0
[0.5.1]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v0.5.1
