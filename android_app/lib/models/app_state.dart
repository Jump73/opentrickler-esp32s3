import 'package:flutter/foundation.dart';

enum ChargeState { exit, waitForZero, charging, removeCup, returnCup }

// Charge event bitmask (matches ESP32 charge_mode.c)
const int kEventUnder = 1 << 1; // 0x02
const int kEventOver  = 1 << 2; // 0x04

class ChargeRecord {
  final double target;
  final String weight;
  final String time;
  final int event;

  const ChargeRecord({
    required this.target,
    required this.weight,
    required this.time,
    required this.event,
  });

  String get result {
    if (event & kEventOver  != 0) return 'OVER';
    if (event & kEventUnder != 0) return 'UNDER';
    return 'OK';
  }

  double get error {
    final w = double.tryParse(weight) ?? 0;
    return w - target;
  }
}

class ProfileInfo {
  final int index;
  final String name;
  const ProfileInfo(this.index, this.name);
}

class ProfileDetails {
  int index;
  String name;
  double coarseKp, coarseKi, coarseKd;
  double coarseMinSpeed, coarseMaxSpeed;
  double fineKp, fineKi, fineKd;
  double fineMinSpeed, fineMaxSpeed;

  ProfileDetails({
    this.index = 0,
    this.name = '',
    this.coarseKp = 0.1,
    this.coarseKi = 0.05,
    this.coarseKd = 0.001,
    this.coarseMinSpeed = 0.001,
    this.coarseMaxSpeed = 7.0,
    this.fineKp = 0.3,
    this.fineKi = 0.08,
    this.fineKd = 0.005,
    this.fineMinSpeed = 0.001,
    this.fineMaxSpeed = 2.0,
  });

  factory ProfileDetails.fromJson(Map<String, dynamic> j) => ProfileDetails(
        index: (j['pf'] as num?)?.toInt() ?? 0,
        name: j['p2'] as String? ?? '',
        coarseKp: (j['p3'] as num?)?.toDouble() ?? 0.1,
        coarseKi: (j['p4'] as num?)?.toDouble() ?? 0.05,
        coarseKd: (j['p5'] as num?)?.toDouble() ?? 0.001,
        coarseMinSpeed: (j['p6'] as num?)?.toDouble() ?? 0.001,
        coarseMaxSpeed: (j['p7'] as num?)?.toDouble() ?? 7.0,
        fineKp: (j['p8'] as num?)?.toDouble() ?? 0.3,
        fineKi: (j['p9'] as num?)?.toDouble() ?? 0.08,
        fineKd: (j['p10'] as num?)?.toDouble() ?? 0.005,
        fineMinSpeed: (j['p11'] as num?)?.toDouble() ?? 0.001,
        fineMaxSpeed: (j['p12'] as num?)?.toDouble() ?? 2.0,
      );
}

class AppState extends ChangeNotifier {
  // --- Connection ---
  bool isConnected = false;
  bool isConnecting = false;
  String connectedDeviceName = '';

  // --- Charge mode state ---
  double targetWeight = 0.0;
  String currentWeight = '---';
  ChargeState chargeState = ChargeState.exit;
  ChargeState _prevChargeState = ChargeState.exit;
  int chargeEvent = 0;
  String profileName = '';
  String elapsedTime = '0.0';
  String settledWeight = '---';
  String settledTime = '---';

  // --- Charge history ---
  List<ChargeRecord> chargeHistory = [];

  // --- Profiles ---
  List<ProfileInfo> profiles = [];
  int currentProfileIndex = 0;
  ProfileDetails? currentProfileDetails;

  // --- Scale config ---
  int scaleDriver = 0;
  int scaleBaudrate = 1;

  // --- Charge config ---
  double coarseStopThreshold = 1.0;
  double fineStopThreshold = 0.1;
  double fineTrickleThreshold = 0.2;
  double resultTolerance = 0.02;

  // --- System info ---
  String deviceId = '';
  String firmwareVersion = '';

  // -------------------------------------------------------------------

  void updateChargeState(Map<String, dynamic> j) {
    targetWeight   = (j['s0'] as num?)?.toDouble() ?? targetWeight;
    currentWeight  = j['s1'] as String? ?? '---';
    final s2       = (j['s2'] as num?)?.toInt() ?? 0;
    chargeState    = ChargeState.values[s2.clamp(0, 4)];
    chargeEvent    = (j['s3'] as num?)?.toInt() ?? 0;
    profileName    = j['s4'] as String? ?? profileName;
    elapsedTime    = j['s5'] as String? ?? '0.0';
    settledWeight  = j['s6'] as String? ?? '---';
    settledTime    = j['s7'] as String? ?? '---';

    // Record history on transition into REMOVE_CUP
    if (chargeState == ChargeState.removeCup &&
        _prevChargeState != ChargeState.removeCup) {
      chargeHistory.insert(0, ChargeRecord(
        target: targetWeight,
        weight: settledWeight,
        time:   elapsedTime,
        event:  chargeEvent,
      ));
      if (chargeHistory.length > 20) chargeHistory.removeLast();
    }
    _prevChargeState = chargeState;

    notifyListeners();
  }

  void updateProfileSummary(Map<String, dynamic> j) {
    final s0 = j['s0'] as Map<String, dynamic>? ?? {};
    currentProfileIndex = (j['s1'] as num?)?.toInt() ?? 0;
    profiles = s0.entries
        .map((e) => ProfileInfo(int.tryParse(e.key) ?? 0, e.value as String? ?? ''))
        .toList()
      ..sort((a, b) => a.index.compareTo(b.index));
    notifyListeners();
  }

  void updateProfileDetails(Map<String, dynamic> j) {
    currentProfileDetails = ProfileDetails.fromJson(j);
    notifyListeners();
  }

  void updateScaleConfig(Map<String, dynamic> j) {
    scaleDriver   = (j['s0'] as num?)?.toInt() ?? scaleDriver;
    scaleBaudrate = (j['s1'] as num?)?.toInt() ?? scaleBaudrate;
    notifyListeners();
  }

  void updateChargeConfig(Map<String, dynamic> j) {
    coarseStopThreshold  = (j['c5']  as num?)?.toDouble() ?? coarseStopThreshold;
    fineStopThreshold    = (j['c6']  as num?)?.toDouble() ?? fineStopThreshold;
    fineTrickleThreshold = (j['c13'] as num?)?.toDouble() ?? fineTrickleThreshold;
    resultTolerance      = (j['c14'] as num?)?.toDouble() ?? resultTolerance;
    notifyListeners();
  }

  void updateSystemInfo(Map<String, dynamic> j) {
    deviceId        = j['s0'] as String? ?? deviceId;
    firmwareVersion = j['s1'] as String? ?? firmwareVersion;
    notifyListeners();
  }

  void setConnected(bool connected, {String name = ''}) {
    isConnected = connected;
    isConnecting = false;
    connectedDeviceName = name;
    if (!connected) {
      currentWeight = '---';
      chargeState   = ChargeState.exit;
      _prevChargeState = ChargeState.exit;
    }
    notifyListeners();
  }

  void setConnecting(bool v) {
    isConnecting = v;
    notifyListeners();
  }
}
