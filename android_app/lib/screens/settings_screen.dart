import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:flutter_colorpicker/flutter_colorpicker.dart';
import '../services/ble_service.dart';
import '../models/app_state.dart';

class SettingsScreen extends StatefulWidget {
  final BleService bleService;
  const SettingsScreen({super.key, required this.bleService});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen>
    with SingleTickerProviderStateMixin {
  late final TabController _tabs;
  Timer? _loadTimer;

  @override
  void initState() {
    super.initState();
    _tabs = TabController(length: 6, vsync: this);
    widget.bleService.requestScaleConfig();
    widget.bleService.requestChargeConfig();
    widget.bleService.requestMotorConfig(0);
    widget.bleService.requestMotorConfig(1);
    widget.bleService.requestDisplayConfig();
    widget.bleService.requestNeopixelConfig();
    widget.bleService.requestSystemInfo();
    // Fallback: if device doesn't respond within 4 s (old firmware),
    // force-load tabs with defaults so they don't spin forever.
    _loadTimer = Timer(const Duration(seconds: 4), () {
      if (mounted) {
        context.read<AppState>().forceDefaultsLoaded();
      }
    });
  }

  @override
  void dispose() {
    _loadTimer?.cancel();
    _tabs.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(
          title: const Text('Settings'),
          bottom: TabBar(
            controller: _tabs,
            isScrollable: true,
            tabs: const [
              Tab(text: 'Scale'),
              Tab(text: 'Charge'),
              Tab(text: 'Motors'),
              Tab(text: 'NeoPixel'),
              Tab(text: 'Display'),
              Tab(text: 'System'),
            ],
          ),
        ),
        body: TabBarView(
          controller: _tabs,
          children: [
            _ScaleTab(bleService: widget.bleService),
            _ChargeTab(bleService: widget.bleService),
            _MotorsTab(bleService: widget.bleService),
            _NeopixelTab(bleService: widget.bleService),
            _DisplayTab(bleService: widget.bleService),
            _SystemTab(bleService: widget.bleService),
          ],
        ),
      );
}

// ---------------------------------------------------------------------------
// Scale tab
// ---------------------------------------------------------------------------

class _ScaleTab extends StatefulWidget {
  final BleService bleService;
  const _ScaleTab({required this.bleService});

  @override
  State<_ScaleTab> createState() => _ScaleTabState();
}

class _ScaleTabState extends State<_ScaleTab> {
  static const _drivers = [
    'AND FX-i',
    'Steinberg SBS',
    'GNG JJB',
    'US Solid JFDBS',
    'JM Science',
    'Creedmoor',
    'Radwag PS R2',
    'Sartorius',
    'Generic',
    'Ohaus Pioneer',
  ];
  static const _baudrates = ['4800', '9600', '19200'];

  int? _driver;
  int? _baud;
  bool _loaded = false;

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    if (!_loaded && state.scaleConfigLoaded) {
      _loaded = true;
      _driver = state.scaleDriver;
      _baud   = state.scaleBaudrate;
    }

    if (!_loaded) {
      return const Center(child: CircularProgressIndicator());
    }

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          DropdownButtonFormField<int>(
            value: _driver?.clamp(0, _drivers.length - 1),
            decoration: const InputDecoration(
              labelText: 'Scale Driver',
              border: OutlineInputBorder(),
            ),
            items: List.generate(
              _drivers.length,
              (i) => DropdownMenuItem(value: i, child: Text(_drivers[i])),
            ),
            onChanged: (v) => setState(() => _driver = v),
          ),
          const SizedBox(height: 16),
          DropdownButtonFormField<int>(
            value: _baud?.clamp(0, _baudrates.length - 1),
            decoration: const InputDecoration(
              labelText: 'Baudrate',
              border: OutlineInputBorder(),
            ),
            items: List.generate(
              _baudrates.length,
              (i) => DropdownMenuItem(value: i, child: Text(_baudrates[i])),
            ),
            onChanged: (v) => setState(() => _baud = v),
          ),
          const SizedBox(height: 24),
          FilledButton(
            onPressed: () {
              widget.bleService.saveScaleConfig(_driver!, _baud!);
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(content: Text('Scale config saved')),
              );
            },
            child: const Text('Apply'),
          ),
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Charge tab
// ---------------------------------------------------------------------------

class _ChargeTab extends StatefulWidget {
  final BleService bleService;
  const _ChargeTab({required this.bleService});

  @override
  State<_ChargeTab> createState() => _ChargeTabState();
}

class _ChargeTabState extends State<_ChargeTab> {
  final _coarse         = TextEditingController();
  final _fine           = TextEditingController();
  final _trickle        = TextEditingController();
  final _tolerance      = TextEditingController();
  final _prechargeTime  = TextEditingController();
  final _prechargeSpeed = TextEditingController();
  bool _prechargeEnable = false;
  bool _loaded = false;

  @override
  void dispose() {
    _coarse.dispose();
    _fine.dispose();
    _trickle.dispose();
    _tolerance.dispose();
    _prechargeTime.dispose();
    _prechargeSpeed.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    if (!_loaded && state.chargeConfigLoaded) {
      _loaded = true;
      _coarse.text         = state.coarseStopThreshold.toStringAsFixed(2);
      _fine.text           = state.fineStopThreshold.toStringAsFixed(3);
      _trickle.text        = state.fineTrickleThreshold.toStringAsFixed(2);
      _tolerance.text      = state.resultTolerance.toStringAsFixed(2);
      _prechargeEnable     = state.prechargeEnable;
      _prechargeTime.text  = state.prechargeTimeMs.toString();
      _prechargeSpeed.text = state.prechargeSpeedRps.toStringAsFixed(2);
    }

    if (!_loaded) return const Center(child: CircularProgressIndicator());

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _numField('Coarse stop threshold (gn)', _coarse),
        const SizedBox(height: 16),
        _numField('Fine stop threshold (gn)', _fine),
        const SizedBox(height: 16),
        _numField('Fine trickle threshold (gn)', _trickle),
        const SizedBox(height: 16),
        _numField('Result tolerance ± (gn)', _tolerance),
        const SizedBox(height: 20),
        const Divider(),
        CheckboxListTile(
          title: const Text('Precharge'),
          subtitle: const Text('Short burst before main charge'),
          value: _prechargeEnable,
          onChanged: (v) => setState(() => _prechargeEnable = v ?? false),
          controlAffinity: ListTileControlAffinity.leading,
          contentPadding: EdgeInsets.zero,
        ),
        if (_prechargeEnable) ...[
          const SizedBox(height: 8),
          _numField('Precharge time (ms)', _prechargeTime),
          const SizedBox(height: 12),
          _numField('Precharge speed (rps)', _prechargeSpeed),
        ],
        const SizedBox(height: 24),
        FilledButton(
          onPressed: _apply,
          child: const Text('Apply'),
        ),
      ],
    );
  }

  Widget _numField(String label, TextEditingController ctrl) => TextField(
        controller: ctrl,
        keyboardType:
            const TextInputType.numberWithOptions(decimal: true),
        decoration: InputDecoration(
          labelText: label,
          border: const OutlineInputBorder(),
        ),
      );

  void _apply() {
    widget.bleService.saveChargeConfig(
      coarseStop:        double.tryParse(_coarse.text)         ?? 1.0,
      fineStop:          double.tryParse(_fine.text)           ?? 0.03,
      fineTrickle:       double.tryParse(_trickle.text)        ?? 0.2,
      resultTolerance:   double.tryParse(_tolerance.text)      ?? 0.02,
      prechargeEnable:   _prechargeEnable,
      prechargeTimeMs:   int.tryParse(_prechargeTime.text)     ?? 500,
      prechargeSpeedRps: double.tryParse(_prechargeSpeed.text) ?? 1.0,
    );
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Charge config saved')),
    );
  }
}

// ---------------------------------------------------------------------------
// Motors tab
// ---------------------------------------------------------------------------

class _MotorsTab extends StatefulWidget {
  final BleService bleService;
  const _MotorsTab({required this.bleService});

  @override
  State<_MotorsTab> createState() => _MotorsTabState();
}

class _MotorsTabState extends State<_MotorsTab> {
  // coarse
  final _cAccel   = TextEditingController();
  final _cSteps   = TextEditingController();
  final _cCurrent = TextEditingController();
  final _cMicro   = TextEditingController();
  final _cMaxSpd  = TextEditingController();
  final _cMinSpd  = TextEditingController();
  final _cRsense  = TextEditingController();
  final _cGear    = TextEditingController();
  bool _cInvEn = false;
  bool _cInvDir = false;

  // fine
  final _fAccel   = TextEditingController();
  final _fSteps   = TextEditingController();
  final _fCurrent = TextEditingController();
  final _fMicro   = TextEditingController();
  final _fMaxSpd  = TextEditingController();
  final _fMinSpd  = TextEditingController();
  final _fRsense  = TextEditingController();
  final _fGear    = TextEditingController();
  bool _fInvEn = false;
  bool _fInvDir = false;

  bool _cLoaded = false;
  bool _fLoaded = false;

  static const _microstepOptions = [16, 32, 64, 128, 256];
  static const _stepsOptions = [200, 400];
  int? _cMicrostepSel;
  int? _fMicrostepSel;
  int? _cStepsSel;
  int? _fStepsSel;

  @override
  void dispose() {
    for (final c in [
      _cAccel, _cSteps, _cCurrent, _cMicro, _cMaxSpd, _cMinSpd, _cRsense, _cGear,
      _fAccel, _fSteps, _fCurrent, _fMicro, _fMaxSpd, _fMinSpd, _fRsense, _fGear,
    ]) {
      c.dispose();
    }
    super.dispose();
  }

  void _loadCoarse(MotorConfig m) {
    if (_cLoaded) return;
    _cLoaded = true;
    _cAccel.text   = m.angularAcceleration.toStringAsFixed(2);
    _cCurrent.text = m.currentMa.toString();
    _cMaxSpd.text  = m.maxSpeedRps.toString();
    _cMinSpd.text  = m.minSpeedRps.toStringAsFixed(3);
    _cRsense.text  = m.rSense.toString();
    _cGear.text    = m.gearRatio.toStringAsFixed(3);
    _cInvEn        = m.invertedEnable;
    _cInvDir       = m.invertedDirection;
    _cMicrostepSel = _microstepOptions.contains(m.microsteps) ? m.microsteps : 16;
    _cStepsSel     = _stepsOptions.contains(m.fullStepsPerRotation) ? m.fullStepsPerRotation : 200;
  }

  void _loadFine(MotorConfig m) {
    if (_fLoaded) return;
    _fLoaded = true;
    _fAccel.text   = m.angularAcceleration.toStringAsFixed(2);
    _fCurrent.text = m.currentMa.toString();
    _fMaxSpd.text  = m.maxSpeedRps.toString();
    _fMinSpd.text  = m.minSpeedRps.toStringAsFixed(3);
    _fRsense.text  = m.rSense.toString();
    _fGear.text    = m.gearRatio.toStringAsFixed(3);
    _fInvEn        = m.invertedEnable;
    _fInvDir       = m.invertedDirection;
    _fMicrostepSel = _microstepOptions.contains(m.microsteps) ? m.microsteps : 16;
    _fStepsSel     = _stepsOptions.contains(m.fullStepsPerRotation) ? m.fullStepsPerRotation : 200;
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    if (!_cLoaded && state.coarseMotorLoaded) _loadCoarse(state.coarseMotor);
    if (!_fLoaded && state.fineMotorLoaded)   _loadFine(state.fineMotor);

    if (!_cLoaded || !_fLoaded) return const Center(child: CircularProgressIndicator());

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _sectionHeader('Coarse Motor'),
        _motorFields(
          accel: _cAccel, current: _cCurrent, maxSpd: _cMaxSpd, minSpd: _cMinSpd,
          rsense: _cRsense, gear: _cGear,
          microstepSel: _cMicrostepSel, stepsSel: _cStepsSel,
          invEn: _cInvEn, invDir: _cInvDir,
          onMicrostep: (v) => setState(() => _cMicrostepSel = v),
          onSteps: (v) => setState(() => _cStepsSel = v),
          onInvEn: (v) => setState(() => _cInvEn = v ?? false),
          onInvDir: (v) => setState(() => _cInvDir = v ?? false),
        ),
        const SizedBox(height: 12),
        FilledButton(
          onPressed: _saveCoarse,
          child: const Text('Apply Coarse'),
        ),
        const SizedBox(height: 24),
        _sectionHeader('Fine Motor'),
        _motorFields(
          accel: _fAccel, current: _fCurrent, maxSpd: _fMaxSpd, minSpd: _fMinSpd,
          rsense: _fRsense, gear: _fGear,
          microstepSel: _fMicrostepSel, stepsSel: _fStepsSel,
          invEn: _fInvEn, invDir: _fInvDir,
          onMicrostep: (v) => setState(() => _fMicrostepSel = v),
          onSteps: (v) => setState(() => _fStepsSel = v),
          onInvEn: (v) => setState(() => _fInvEn = v ?? false),
          onInvDir: (v) => setState(() => _fInvDir = v ?? false),
        ),
        const SizedBox(height: 12),
        FilledButton(
          onPressed: _saveFine,
          child: const Text('Apply Fine'),
        ),
      ],
    );
  }

  Widget _sectionHeader(String label) => Padding(
        padding: const EdgeInsets.only(bottom: 12),
        child: Text(label,
            style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 15)),
      );

  Widget _motorFields({
    required TextEditingController accel,
    required TextEditingController current,
    required TextEditingController maxSpd,
    required TextEditingController minSpd,
    required TextEditingController rsense,
    required TextEditingController gear,
    required int? microstepSel,
    required int? stepsSel,
    required bool invEn,
    required bool invDir,
    required ValueChanged<int?> onMicrostep,
    required ValueChanged<int?> onSteps,
    required ValueChanged<bool?> onInvEn,
    required ValueChanged<bool?> onInvDir,
  }) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(children: [
          Expanded(child: _nf('Accel (rps²)', accel)),
          const SizedBox(width: 8),
          Expanded(child: _nf('Current (mA)', current)),
        ]),
        const SizedBox(height: 8),
        Row(children: [
          Expanded(child: _nf('Max speed (rps)', maxSpd)),
          const SizedBox(width: 8),
          Expanded(child: _nf('Min speed (rps)', minSpd)),
        ]),
        const SizedBox(height: 8),
        Row(children: [
          Expanded(child: _nf('R-sense (Ω)', rsense)),
          const SizedBox(width: 8),
          Expanded(child: _nf('Gear ratio', gear)),
        ]),
        const SizedBox(height: 8),
        Row(children: [
          Expanded(
            child: DropdownButtonFormField<int>(
              value: stepsSel,
              decoration: const InputDecoration(
                labelText: 'Steps/rev',
                border: OutlineInputBorder(),
                isDense: true,
              ),
              items: _stepsOptions
                  .map((v) => DropdownMenuItem(value: v, child: Text('$v')))
                  .toList(),
              onChanged: onSteps,
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: DropdownButtonFormField<int>(
              value: microstepSel,
              decoration: const InputDecoration(
                labelText: 'Microsteps',
                border: OutlineInputBorder(),
                isDense: true,
              ),
              items: _microstepOptions
                  .map((v) => DropdownMenuItem(value: v, child: Text('$v')))
                  .toList(),
              onChanged: onMicrostep,
            ),
          ),
        ]),
        const SizedBox(height: 4),
        CheckboxListTile(
          dense: true,
          title: const Text('Invert enable pin'),
          value: invEn,
          onChanged: onInvEn,
          controlAffinity: ListTileControlAffinity.leading,
        ),
        CheckboxListTile(
          dense: true,
          title: const Text('Invert direction'),
          value: invDir,
          onChanged: onInvDir,
          controlAffinity: ListTileControlAffinity.leading,
        ),
      ],
    );
  }

  Widget _nf(String label, TextEditingController ctrl) => TextField(
        controller: ctrl,
        keyboardType: const TextInputType.numberWithOptions(decimal: true),
        decoration: InputDecoration(
          labelText: label,
          border: const OutlineInputBorder(),
          isDense: true,
        ),
      );

  MotorConfig _buildCoarse() => MotorConfig(
        angularAcceleration:  double.tryParse(_cAccel.text)   ?? 10.0,
        fullStepsPerRotation: _cStepsSel                      ?? 200,
        currentMa:            int.tryParse(_cCurrent.text)    ?? 800,
        microsteps:           _cMicrostepSel                  ?? 16,
        maxSpeedRps:          int.tryParse(_cMaxSpd.text)     ?? 10,
        rSense:               int.tryParse(_cRsense.text)     ?? 110,
        minSpeedRps:          double.tryParse(_cMinSpd.text)  ?? 0.1,
        gearRatio:            double.tryParse(_cGear.text)    ?? 1.0,
        invertedEnable:       _cInvEn,
        invertedDirection:    _cInvDir,
      );

  MotorConfig _buildFine() => MotorConfig(
        angularAcceleration:  double.tryParse(_fAccel.text)   ?? 5.0,
        fullStepsPerRotation: _fStepsSel                      ?? 200,
        currentMa:            int.tryParse(_fCurrent.text)    ?? 600,
        microsteps:           _fMicrostepSel                  ?? 16,
        maxSpeedRps:          int.tryParse(_fMaxSpd.text)     ?? 5,
        rSense:               int.tryParse(_fRsense.text)     ?? 110,
        minSpeedRps:          double.tryParse(_fMinSpd.text)  ?? 0.05,
        gearRatio:            double.tryParse(_fGear.text)    ?? 1.0,
        invertedEnable:       _fInvEn,
        invertedDirection:    _fInvDir,
      );

  void _saveCoarse() {
    widget.bleService.saveMotorConfig(0, _buildCoarse());
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Coarse motor config saved')),
    );
  }

  void _saveFine() {
    widget.bleService.saveMotorConfig(1, _buildFine());
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Fine motor config saved')),
    );
  }
}

// ---------------------------------------------------------------------------
// NeoPixel tab
// ---------------------------------------------------------------------------

class _NeopixelTab extends StatefulWidget {
  final BleService bleService;
  const _NeopixelTab({required this.bleService});

  @override
  State<_NeopixelTab> createState() => _NeopixelTabState();
}

class _NeopixelTabState extends State<_NeopixelTab> {
  Color _backlight = const Color(0xFF0F0F0F);
  Color _led1      = const Color(0xFF00FF00);
  Color _led2      = const Color(0xFF00FF00);
  int   _chainCount = 1;
  bool  _isRgbw    = false;
  int   _colorOrder = 0;
  bool  _loaded    = false;

  // ignore: deprecated_member_use
  static int _toInt(Color c) => (c.red << 16) | (c.green << 8) | c.blue;

  static Color _fromInt(int v) => Color(0xFF000000 | (v & 0xFFFFFF));

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    if (!_loaded && state.neopixelConfigLoaded) {
      _loaded     = true;
      _backlight  = _fromInt(state.neopixelBacklight);
      _led1       = _fromInt(state.neopixelLed1);
      _led2       = _fromInt(state.neopixelLed2);
      _chainCount = state.neopixelChainCount;
      _isRgbw     = state.neopixelIsRgbw;
      _colorOrder = state.neopixelColorOrder;
    }

    if (!_loaded) return const Center(child: CircularProgressIndicator());

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _colorRow('Backlight', _backlight, (c) => setState(() => _backlight = c)),
        const SizedBox(height: 12),
        _colorRow('LED 1', _led1, (c) => setState(() => _led1 = c)),
        const SizedBox(height: 12),
        _colorRow('LED 2', _led2, (c) => setState(() => _led2 = c)),
        const SizedBox(height: 16),
        DropdownButtonFormField<int>(
          value: _chainCount.clamp(1, 16),
          decoration: const InputDecoration(
            labelText: 'PWM OUT chain count',
            border: OutlineInputBorder(),
          ),
          items: List.generate(
            16,
            (i) => DropdownMenuItem(value: i + 1, child: Text('${i + 1}')),
          ),
          onChanged: (v) => setState(() => _chainCount = v ?? 1),
        ),
        const SizedBox(height: 12),
        DropdownButtonFormField<int>(
          value: _colorOrder,
          decoration: const InputDecoration(
            labelText: 'Colour order',
            border: OutlineInputBorder(),
          ),
          items: const [
            DropdownMenuItem(value: 0, child: Text('RGB')),
            DropdownMenuItem(value: 1, child: Text('GRB')),
          ],
          onChanged: (v) => setState(() => _colorOrder = v ?? 0),
        ),
        const SizedBox(height: 4),
        CheckboxListTile(
          title: const Text('RGBW LEDs (SK6812)'),
          subtitle: const Text('Uncheck for RGB (WS2812B)'),
          value: _isRgbw,
          onChanged: (v) => setState(() => _isRgbw = v ?? false),
          controlAffinity: ListTileControlAffinity.leading,
          contentPadding: EdgeInsets.zero,
        ),
        const SizedBox(height: 16),
        FilledButton(
          onPressed: _apply,
          child: const Text('Apply'),
        ),
      ],
    );
  }

  Widget _colorRow(String label, Color color, ValueChanged<Color> onChanged) {
    return InkWell(
      onTap: () => _pickColor(label, color, onChanged),
      child: InputDecorator(
        decoration: InputDecoration(
          labelText: label,
          border: const OutlineInputBorder(),
        ),
        child: Row(
          children: [
            Container(
              width: 32,
              height: 24,
              decoration: BoxDecoration(
                color: color,
                border: Border.all(color: Colors.grey),
                borderRadius: BorderRadius.circular(4),
              ),
            ),
            const SizedBox(width: 12),
            Text(
              '#${_toInt(color).toRadixString(16).padLeft(6, '0').toUpperCase()}',
              style: const TextStyle(fontFamily: 'monospace'),
            ),
            const Spacer(),
            const Icon(Icons.colorize, size: 18, color: Colors.grey),
          ],
        ),
      ),
    );
  }

  Future<void> _pickColor(String label, Color current, ValueChanged<Color> onChanged) async {
    Color picked = current;
    await showDialog<void>(
      context: context,
      builder: (_) => AlertDialog(
        title: Text('Pick $label colour'),
        content: SingleChildScrollView(
          child: HueRingPicker(
            pickerColor: current,
            onColorChanged: (c) => picked = c,
            enableAlpha: false,
            displayThumbColor: true,
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () {
              onChanged(picked);
              Navigator.pop(context);
            },
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  void _apply() {
    widget.bleService.saveNeopixelConfig(
      backlight:  _toInt(_backlight),
      led1:       _toInt(_led1),
      led2:       _toInt(_led2),
      chainCount: _chainCount,
      isRgbw:     _isRgbw,
      colorOrder: _colorOrder,
    );
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('NeoPixel config saved')),
    );
  }
}

// ---------------------------------------------------------------------------
// Display tab
// ---------------------------------------------------------------------------

class _DisplayTab extends StatefulWidget {
  final BleService bleService;
  const _DisplayTab({required this.bleService});

  @override
  State<_DisplayTab> createState() => _DisplayTabState();
}

class _DisplayTabState extends State<_DisplayTab> {
  bool _invertedEncoder = false;
  int  _rotation = 0; // 0 or 2
  bool _loaded = false;

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    if (!_loaded && state.displayConfigLoaded) {
      _loaded          = true;
      _invertedEncoder = state.displayInvertedEncoder;
      _rotation        = state.displayRotation;
    }

    if (!_loaded) return const Center(child: CircularProgressIndicator());

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          DropdownButtonFormField<int>(
            value: _rotation,
            decoration: const InputDecoration(
              labelText: 'Display rotation',
              border: OutlineInputBorder(),
            ),
            items: const [
              DropdownMenuItem(value: 0, child: Text('0°')),
              DropdownMenuItem(value: 2, child: Text('180°')),
            ],
            onChanged: (v) => setState(() => _rotation = v ?? 0),
          ),
          const SizedBox(height: 8),
          CheckboxListTile(
            title: const Text('Invert encoder direction'),
            subtitle: const Text('Swap CW/CCW rotation'),
            value: _invertedEncoder,
            onChanged: (v) => setState(() => _invertedEncoder = v ?? false),
            controlAffinity: ListTileControlAffinity.leading,
          ),
          const SizedBox(height: 24),
          FilledButton(
            onPressed: _apply,
            child: const Text('Apply'),
          ),
        ],
      ),
    );
  }

  void _apply() {
    widget.bleService.saveDisplayConfig(
      invertedEncoder: _invertedEncoder,
      rotation: _rotation,
    );
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Display config saved')),
    );
  }
}

// ---------------------------------------------------------------------------
// System tab
// ---------------------------------------------------------------------------

class _SystemTab extends StatelessWidget {
  final BleService bleService;
  const _SystemTab({required this.bleService});

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (state.deviceId.isNotEmpty) ...[
            ListTile(
              dense: true,
              title: const Text('Device ID'),
              subtitle: Text(state.deviceId),
            ),
            ListTile(
              dense: true,
              title: const Text('Firmware'),
              subtitle: Text(state.firmwareVersion),
            ),
            const Divider(),
          ],
          const SizedBox(height: 8),
          OutlinedButton.icon(
            icon: const Icon(Icons.restart_alt),
            label: const Text('Reboot device'),
            onPressed: () => _confirm(
              context,
              title: 'Reboot?',
              body: 'The device will restart.',
              onConfirm: bleService.reboot,
            ),
          ),
          const SizedBox(height: 8),
          FilledButton.icon(
            style: FilledButton.styleFrom(backgroundColor: Colors.red),
            icon: const Icon(Icons.delete_forever),
            label: const Text('Factory reset (erase NVS)'),
            onPressed: () => _confirm(
              context,
              title: 'Erase all settings?',
              body: 'All configuration will be reset to factory defaults.',
              onConfirm: bleService.eraseNvs,
              danger: true,
            ),
          ),
        ],
      ),
    );
  }

  void _confirm(
    BuildContext context, {
    required String title,
    required String body,
    required VoidCallback onConfirm,
    bool danger = false,
  }) {
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: Text(title),
        content: Text(body),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          TextButton(
            style: danger
                ? TextButton.styleFrom(foregroundColor: Colors.red)
                : null,
            onPressed: () {
              Navigator.pop(context);
              onConfirm();
            },
            child: Text(danger ? 'Erase' : 'OK'),
          ),
        ],
      ),
    );
  }
}
