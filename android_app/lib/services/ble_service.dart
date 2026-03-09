import 'dart:async';
import 'dart:convert';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/app_state.dart';

class BleService {
  static const String _serviceUuid = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
  static const String _rxCharUuid  = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
  static const String _txCharUuid  = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

  final AppState _state;
  BluetoothDevice? _device;
  BluetoothCharacteristic? _rxChar;
  StreamSubscription? _txSub;
  StreamSubscription? _connSub;
  Timer? _pollTimer;
  final StringBuffer _rxBuf = StringBuffer();

  BleService(this._state);

  // --- Scan ---

  Future<void> startScan() async {
    if (FlutterBluePlus.isScanningNow) return;
    await FlutterBluePlus.startScan(
      withNames: ['OpenTrickler'],
      timeout: const Duration(seconds: 20),
    );
  }

  Future<void> stopScan() => FlutterBluePlus.stopScan();

  Stream<List<ScanResult>> get scanResults => FlutterBluePlus.scanResults;
  Stream<bool> get isScanning => FlutterBluePlus.isScanning;

  // --- Connect / Disconnect ---

  Future<void> connect(BluetoothDevice device) async {
    _state.setConnecting(true);
    try {
      await device.connect(autoConnect: false, timeout: const Duration(seconds: 15));
      _device = device;

      _connSub = device.connectionState.listen((s) {
        if (s == BluetoothConnectionState.disconnected) _onDisconnected();
      });

      // Request larger MTU for fewer chunks
      try { await device.requestMtu(185); } catch (_) {}

      await _discoverServices(device);
      _state.setConnected(true, name: device.platformName);
      _startPolling();
    } catch (e) {
      _state.setConnecting(false);
      rethrow;
    }
  }

  Future<void> disconnect() async {
    _stopPolling();
    await _device?.disconnect();
    _device = null;
    _rxChar = null;
  }

  void _onDisconnected() {
    _stopPolling();
    _txSub?.cancel();
    _connSub?.cancel();
    _txSub = null;
    _connSub = null;
    _device = null;
    _rxChar = null;
    _rxBuf.clear();
    _state.setConnected(false);
  }

  // --- Service discovery ---

  Future<void> _discoverServices(BluetoothDevice device) async {
    final services = await device.discoverServices();
    for (final svc in services) {
      if (svc.uuid.toString().toLowerCase() == _serviceUuid) {
        for (final chr in svc.characteristics) {
          final uuid = chr.uuid.toString().toLowerCase();
          if (uuid == _txCharUuid) {
            await chr.setNotifyValue(true);
            _txSub = chr.onValueReceived.listen(_onTxData);
          }
          if (uuid == _rxCharUuid) {
            _rxChar = chr;
          }
        }
      }
    }
  }

  // --- RX / TX ---

  void _onTxData(List<int> data) {
    _rxBuf.write(utf8.decode(data, allowMalformed: true));
    while (true) {
      final content = _rxBuf.toString();
      final idx = content.indexOf('\n');
      if (idx < 0) break;
      final line = content.substring(0, idx).trim();
      _rxBuf.clear();
      _rxBuf.write(content.substring(idx + 1));
      if (line.isNotEmpty) _handleResponse(line);
    }
  }

  void _handleResponse(String line) {
    try {
      final j = jsonDecode(line) as Map<String, dynamic>;
      switch (j['cmd'] as String?) {
        case 'charge_mode_state':
          _state.updateChargeState(j);
          break;
        case 'profile_summary':
          _state.updateProfileSummary(j);
          break;
        case 'profile_config':
          _state.updateProfileDetails(j);
          break;
        case 'charge_mode_config':
          _state.updateChargeConfig(j);
          break;
        case 'neopixel_config':
          _state.updateNeopixelConfig(j);
          break;
        case 'cleanup_mode_state':
          _state.updateCleanupState(j);
          break;
        case 'motor_config':
          _state.updateMotorConfig(j);
          break;
        case 'display_config':
          _state.updateDisplayConfig(j);
          break;
        case 'scale_config':
          _state.updateScaleConfig(j);
          break;
        case 'system_control':
          _state.updateSystemInfo(j);
          break;
      }
    } catch (_) {}
  }

  Future<void> send(Map<String, dynamic> cmd) async {
    if (_rxChar == null) return;
    try {
      await _rxChar!.write(
        utf8.encode('${jsonEncode(cmd)}\n'),
        withoutResponse: true,
      );
    } catch (_) {}
  }

  // --- Polling ---

  void _startPolling() {
    _pollTimer = Timer.periodic(const Duration(milliseconds: 500), (_) {
      send({'cmd': 'charge_mode_state'});
    });
  }

  void _stopPolling() {
    _pollTimer?.cancel();
    _pollTimer = null;
  }

  // --- Commands ---

  Future<void> setTargetWeight(double w) =>
      send({'cmd': 'charge_mode_state', 's0': w});

  Future<void> startCharging(double w) =>
      send({'cmd': 'charge_mode_state', 's0': w, 's2': 1});

  Future<void> abortCharging() =>
      send({'cmd': 'charge_mode_state', 's2': 0});

  Future<void> requestProfileSummary() =>
      send({'cmd': 'profile_summary'});

  Future<void> selectProfile(int idx) =>
      send({'cmd': 'profile_summary', 'sel': idx});

  Future<void> requestProfileConfig(int idx) =>
      send({'cmd': 'profile_config', 'pf': idx});

  Future<void> saveProfileConfig(ProfileDetails p) => send({
        'cmd': 'profile_config',
        'pf': p.index,
        'p2': p.name,
        'p3': p.coarseKp,
        'p4': p.coarseKi,
        'p5': p.coarseKd,
        'p6': p.coarseMinSpeed,
        'p7': p.coarseMaxSpeed,
        'p8': p.fineKp,
        'p9': p.fineKi,
        'p10': p.fineKd,
        'p11': p.fineMinSpeed,
        'p12': p.fineMaxSpeed,
        'ee': true,
      });

  Future<void> requestChargeConfig() =>
      send({'cmd': 'charge_mode_config'});

  Future<void> saveChargeConfig({
    required int colorNormal,
    required int colorUnder,
    required int colorOver,
    required int colorNotReady,
    required double coarseStop,
    required double fineStop,
    required double fineTrickle,
    required double resultTolerance,
    required bool prechargeEnable,
    required int prechargeTimeMs,
    required double prechargeSpeedRps,
  }) =>
      send({
        'cmd': 'charge_mode_config',
        'c1': colorNormal,
        'c2': colorUnder,
        'c3': colorOver,
        'c4': colorNotReady,
        'c5': coarseStop,
        'c6': fineStop,
        'c10': prechargeEnable,
        'c11': prechargeTimeMs,
        'c12': prechargeSpeedRps,
        'c13': fineTrickle,
        'c14': resultTolerance,
        'ee': true,
      });

  Future<void> requestNeopixelConfig() =>
      send({'cmd': 'neopixel_config'});

  Future<void> saveNeopixelConfig({
    required int backlight,
    required int led1,
    required int led2,
    required int chainCount,
    required bool isRgbw,
    required int colorOrder,
  }) =>
      send({
        'cmd': 'neopixel_config',
        'bl': backlight,
        'l1': led1,
        'l2': led2,
        'l3': chainCount,
        'l4': isRgbw,
        'l5': colorOrder,
        'ee': true,
      });

  Future<void> requestScaleConfig() =>
      send({'cmd': 'scale_config'});

  Future<void> saveScaleConfig(int driver, int baud) =>
      send({'cmd': 'scale_config', 's0': driver, 's1': baud, 'ee': true});

  Future<void> zeroScale() =>
      send({'cmd': 'scale_action', 'a0': 1});

  Future<void> requestMotorConfig(int motorType) =>
      send({'cmd': 'motor_config', 'mt': motorType});

  Future<void> saveMotorConfig(int motorType, MotorConfig c) => send({
        'cmd': 'motor_config',
        'mt': motorType,
        'm0': c.angularAcceleration,
        'm1': c.fullStepsPerRotation,
        'm2': c.currentMa,
        'm3': c.microsteps,
        'm4': c.maxSpeedRps,
        'm5': c.rSense,
        'm6': c.minSpeedRps,
        'm7': c.gearRatio,
        'm8': c.invertedEnable,
        'm9': c.invertedDirection,
        'ee': true,
      });

  Future<void> requestDisplayConfig() =>
      send({'cmd': 'display_config'});

  Future<void> saveDisplayConfig({
    required bool invertedEncoder,
    required int rotation,
  }) =>
      send({
        'cmd': 'display_config',
        'b0': invertedEncoder,
        'b1': rotation,
        'ee': true,
      });

  Future<void> requestSystemInfo() =>
      send({'cmd': 'system_control'});

  Future<void> reboot() =>
      send({'cmd': 'system_control', 's5': true});

  Future<void> eraseNvs() =>
      send({'cmd': 'system_control', 's6': true});

  Future<void> setCleanupMode(bool active) =>
      send({'cmd': 'cleanup_mode_state', 's0': active ? 1 : 0});

  Future<void> setCleanupSpeed(double speed) =>
      send({'cmd': 'cleanup_mode_state', 's1': speed});

  void dispose() {
    _stopPolling();
    _txSub?.cancel();
    _connSub?.cancel();
    disconnect();
  }
}
