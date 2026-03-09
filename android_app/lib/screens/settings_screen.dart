import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
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

  @override
  void initState() {
    super.initState();
    _tabs = TabController(length: 3, vsync: this);
    widget.bleService.requestScaleConfig();
    widget.bleService.requestChargeConfig();
    widget.bleService.requestSystemInfo();
  }

  @override
  void dispose() {
    _tabs.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(
          title: const Text('Settings'),
          bottom: TabBar(
            controller: _tabs,
            tabs: const [
              Tab(text: 'Scale'),
              Tab(text: 'Charge'),
              Tab(text: 'System'),
            ],
          ),
        ),
        body: TabBarView(
          controller: _tabs,
          children: [
            _ScaleTab(bleService: widget.bleService),
            _ChargeTab(bleService: widget.bleService),
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

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    _driver ??= state.scaleDriver;
    _baud ??= state.scaleBaudrate;

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
  final _coarse = TextEditingController();
  final _fine = TextEditingController();
  final _trickle = TextEditingController();
  bool _loaded = false;

  @override
  void dispose() {
    _coarse.dispose();
    _fine.dispose();
    _trickle.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    if (!_loaded && state.coarseStopThreshold > 0) {
      _loaded = true;
      _coarse.text = state.coarseStopThreshold.toStringAsFixed(3);
      _fine.text = state.fineStopThreshold.toStringAsFixed(3);
      _trickle.text = state.fineTrickleThreshold.toStringAsFixed(3);
    }

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _numField('Coarse stop threshold (gr)', _coarse),
          const SizedBox(height: 16),
          _numField('Fine stop threshold (gr)', _fine),
          const SizedBox(height: 16),
          _numField('Fine trickle threshold (gr)', _trickle),
          const SizedBox(height: 24),
          FilledButton(
            onPressed: _apply,
            child: const Text('Apply'),
          ),
        ],
      ),
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
      coarseStop: double.tryParse(_coarse.text) ?? 1.0,
      fineStop: double.tryParse(_fine.text) ?? 0.1,
      fineTrickle: double.tryParse(_trickle.text) ?? 0.2,
    );
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Charge config saved')),
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
