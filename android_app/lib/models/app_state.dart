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

class MotorConfig {
  double angularAcceleration;
  int fullStepsPerRotation;
  int currentMa;
  int microsteps;
  int maxSpeedRps;
  int rSense;
  double minSpeedRps;
  double gearRatio;
  bool invertedEnable;
  bool invertedDirection;

  MotorConfig({
    this.angularAcceleration = 10.0,
    this.fullStepsPerRotation = 200,
    this.currentMa = 800,
    this.microsteps = 16,
    this.maxSpeedRps = 10,
    this.rSense = 110,
    this.minSpeedRps = 0.1,
    this.gearRatio = 1.0,
    this.invertedEnable = false,
    this.invertedDirection = false,
  });

  factory MotorConfig.coarseDefaults() => MotorConfig(
        currentMa: 800,
        maxSpeedRps: 10,
        minSpeedRps: 0.1,
        angularAcceleration: 10.0,
      );

  factory MotorConfig.fineDefaults() => MotorConfig(
        currentMa: 600,
        maxSpeedRps: 5,
        minSpeedRps: 0.05,
        angularAcceleration: 5.0,
      );

  factory MotorConfig.fromJson(Map<String, dynamic> j) => MotorConfig(
        angularAcceleration:   (j['m0'] as num?)?.toDouble() ?? 10.0,
        fullStepsPerRotation:  (j['m1'] as num?)?.toInt()    ?? 200,
        currentMa:             (j['m2'] as num?)?.toInt()    ?? 800,
        microsteps:            (j['m3'] as num?)?.toInt()    ?? 16,
        maxSpeedRps:           (j['m4'] as num?)?.toInt()    ?? 10,
        rSense:                (j['m5'] as num?)?.toInt()    ?? 110,
        minSpeedRps:           (j['m6'] as num?)?.toDouble() ?? 0.1,
        gearRatio:             (j['m7'] as num?)?.toDouble() ?? 1.0,
        invertedEnable:        j['m8'] as bool?              ?? false,
        invertedDirection:     j['m9'] as bool?              ?? false,
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
  int chargeColorNormal   = 0x00FF00; // Green
  int chargeColorUnder    = 0xFFFF00; // Yellow
  int chargeColorOver     = 0xFF0000; // Red
  int chargeColorNotReady = 0x0000FF; // Blue
  double coarseStopThreshold = 1.0;
  double fineStopThreshold = 0.1;
  double fineTrickleThreshold = 0.2;
  double resultTolerance = 0.02;
  bool prechargeEnable = false;
  int prechargeTimeMs = 500;
  double prechargeSpeedRps = 1.0;

  // --- NeoPixel config ---
  int neopixelBacklight = 0x0F0F0F;
  int neopixelLed1 = 0x00FF00;
  int neopixelLed2 = 0x00FF00;
  int neopixelChainCount = 1;
  bool neopixelIsRgbw = false;
  int neopixelColorOrder = 0; // 0=RGB, 1=GRB

  // --- Cleanup mode ---
  bool cleanupActive = false;
  double cleanupSpeed = 0.0;

  // --- Motor config ---
  MotorConfig coarseMotor = MotorConfig.coarseDefaults();
  MotorConfig fineMotor = MotorConfig.fineDefaults();

  // --- Display config ---
  bool displayInvertedEncoder = false;
  int displayRotation = 0; // 0=0°, 2=180°

  // --- System info ---
  String deviceId = '';
  String firmwareVersion = '';

  // --- Loaded flags (true only after first real BLE response) ---
  bool scaleConfigLoaded    = false;
  bool chargeConfigLoaded   = false;
  bool neopixelConfigLoaded = false;
  bool displayConfigLoaded  = false;
  bool coarseMotorLoaded    = false;
  bool fineMotorLoaded      = false;

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
    scaleDriver        = (j['s0'] as num?)?.toInt() ?? scaleDriver;
    scaleBaudrate      = (j['s1'] as num?)?.toInt() ?? scaleBaudrate;
    scaleConfigLoaded  = true;
    notifyListeners();
  }

  void updateChargeConfig(Map<String, dynamic> j) {
    chargeColorNormal    = (j['c1'] as num?)?.toInt() ?? chargeColorNormal;
    chargeColorUnder     = (j['c2'] as num?)?.toInt() ?? chargeColorUnder;
    chargeColorOver      = (j['c3'] as num?)?.toInt() ?? chargeColorOver;
    chargeColorNotReady  = (j['c4'] as num?)?.toInt() ?? chargeColorNotReady;
    coarseStopThreshold  = (j['c5']  as num?)?.toDouble() ?? coarseStopThreshold;
    fineStopThreshold    = (j['c6']  as num?)?.toDouble() ?? fineStopThreshold;
    fineTrickleThreshold = (j['c13'] as num?)?.toDouble() ?? fineTrickleThreshold;
    resultTolerance      = (j['c14'] as num?)?.toDouble() ?? resultTolerance;
    prechargeEnable      = j['c10']  as bool?             ?? prechargeEnable;
    prechargeTimeMs      = (j['c11'] as num?)?.toInt()    ?? prechargeTimeMs;
    prechargeSpeedRps    = (j['c12'] as num?)?.toDouble() ?? prechargeSpeedRps;
    chargeConfigLoaded   = true;
    notifyListeners();
  }

  void updateNeopixelConfig(Map<String, dynamic> j) {
    neopixelBacklight     = (j['bl'] as num?)?.toInt() ?? neopixelBacklight;
    neopixelLed1          = (j['l1'] as num?)?.toInt() ?? neopixelLed1;
    neopixelLed2          = (j['l2'] as num?)?.toInt() ?? neopixelLed2;
    neopixelChainCount    = (j['l3'] as num?)?.toInt() ?? neopixelChainCount;
    neopixelIsRgbw        = j['l4'] as bool?           ?? neopixelIsRgbw;
    neopixelColorOrder    = (j['l5'] as num?)?.toInt() ?? neopixelColorOrder;
    neopixelConfigLoaded  = true;
    notifyListeners();
  }

  void updateCleanupState(Map<String, dynamic> j) {
    cleanupActive = ((j['s0'] as num?)?.toInt() ?? 0) == 1;
    cleanupSpeed  = (j['s1'] as num?)?.toDouble() ?? cleanupSpeed;
    notifyListeners();
  }

  void updateMotorConfig(Map<String, dynamic> j) {
    final mt = (j['mt'] as num?)?.toInt() ?? -1;
    if (mt == 0) {
      coarseMotor      = MotorConfig.fromJson(j);
      coarseMotorLoaded = true;
    } else if (mt == 1) {
      fineMotor      = MotorConfig.fromJson(j);
      fineMotorLoaded = true;
    }
    notifyListeners();
  }

  void updateDisplayConfig(Map<String, dynamic> j) {
    displayInvertedEncoder = j['b0'] as bool? ?? displayInvertedEncoder;
    displayRotation        = (j['b1'] as num?)?.toInt() ?? displayRotation;
    displayConfigLoaded    = true;
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

  /// Forces all loaded flags so settings tabs don't spin forever
  /// when the device firmware doesn't support a command yet.
  void forceDefaultsLoaded() {
    scaleConfigLoaded    = true;
    chargeConfigLoaded   = true;
    neopixelConfigLoaded = true;
    displayConfigLoaded  = true;
    coarseMotorLoaded    = true;
    fineMotorLoaded      = true;
    notifyListeners();
  }
}
