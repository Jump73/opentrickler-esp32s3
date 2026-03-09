import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/ble_service.dart';
import '../models/app_state.dart';
import 'profiles_screen.dart';
import 'settings_screen.dart';

class HomeScreen extends StatefulWidget {
  final BleService bleService;
  const HomeScreen({super.key, required this.bleService});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  final _targetCtrl = TextEditingController();
  double _localTarget = 0.0;
  bool _targetEditing = false;

  @override
  void initState() {
    super.initState();
    widget.bleService.requestProfileSummary();
  }

  @override
  void dispose() {
    _targetCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();

    // Sync target weight from device if user isn't editing
    if (!_targetEditing && state.targetWeight > 0 && _localTarget == 0.0) {
      _localTarget = state.targetWeight;
      _targetCtrl.text = _localTarget.toStringAsFixed(3);
    }

    final isActive = state.chargeState == ChargeState.waitForZero ||
        state.chargeState == ChargeState.charging;

    return Scaffold(
      appBar: AppBar(
        title: Text(
          state.profileName.isNotEmpty ? state.profileName : 'OpenTrickler',
        ),
        actions: [
          IconButton(
            icon: const Icon(Icons.bluetooth_connected),
            tooltip: 'Disconnect',
            onPressed: widget.bleService.disconnect,
          ),
        ],
      ),
      drawer: _buildDrawer(context),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          child: Column(
            children: [
              // --- Weight display ---
              Expanded(
                flex: 3,
                child: Center(
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Text(
                        state.currentWeight,
                        style: TextStyle(
                          fontSize: 80,
                          fontWeight: FontWeight.bold,
                          fontFeatures: const [],
                          color: _weightColor(state),
                          letterSpacing: -2,
                        ),
                      ),
                      Text(
                        'gr',
                        style: Theme.of(context)
                            .textTheme
                            .headlineMedium
                            ?.copyWith(color: Colors.grey),
                      ),
                      const SizedBox(height: 12),
                      _stateChip(state),
                      if (state.chargeState == ChargeState.removeCup ||
                          state.chargeState == ChargeState.returnCup)
                        Padding(
                          padding: const EdgeInsets.only(top: 8),
                          child: Text(
                            'Settled: ${state.settledWeight} gr  •  ${state.settledTime} s',
                            style: Theme.of(context)
                                .textTheme
                                .bodyMedium
                                ?.copyWith(color: Colors.grey),
                          ),
                        ),
                      if (isActive)
                        Padding(
                          padding: const EdgeInsets.only(top: 4),
                          child: Text(
                            '⏱  ${state.elapsedTime} s',
                            style: Theme.of(context).textTheme.bodyMedium,
                          ),
                        ),
                    ],
                  ),
                ),
              ),

              // --- Target weight ---
              Row(
                children: [
                  _stepBtn(Icons.remove, () => _adjustTarget(-0.02)),
                  _stepBtn(Icons.remove_circle_outline, () => _adjustTarget(-0.1)),
                  Expanded(
                    child: TextField(
                      controller: _targetCtrl,
                      keyboardType: const TextInputType.numberWithOptions(decimal: true),
                      textAlign: TextAlign.center,
                      decoration: const InputDecoration(
                        labelText: 'Target (gr)',
                        border: OutlineInputBorder(),
                        contentPadding: EdgeInsets.symmetric(vertical: 12),
                      ),
                      onTap: () => _targetEditing = true,
                      onSubmitted: (v) {
                        _targetEditing = false;
                        _localTarget = double.tryParse(v) ?? _localTarget;
                        _targetCtrl.text = _localTarget.toStringAsFixed(3);
                        widget.bleService.setTargetWeight(_localTarget);
                      },
                    ),
                  ),
                  _stepBtn(Icons.add_circle_outline, () => _adjustTarget(0.1)),
                  _stepBtn(Icons.add, () => _adjustTarget(0.02)),
                ],
              ),
              const SizedBox(height: 12),

              // --- Action buttons ---
              Row(
                children: [
                  Expanded(
                    flex: 4,
                    child: FilledButton(
                      style: FilledButton.styleFrom(
                        minimumSize: const Size.fromHeight(56),
                        backgroundColor: isActive ? Colors.red : Colors.amber,
                        foregroundColor: Colors.black,
                      ),
                      onPressed: isActive ? _abort : _start,
                      child: Text(
                        isActive ? 'ABORT' : 'START',
                        style: const TextStyle(
                            fontSize: 22, fontWeight: FontWeight.bold),
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    flex: 1,
                    child: OutlinedButton(
                      style: OutlinedButton.styleFrom(
                        minimumSize: const Size.fromHeight(56),
                      ),
                      onPressed: widget.bleService.zeroScale,
                      child: const Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(Icons.exposure_zero, size: 20),
                          Text('Zero', style: TextStyle(fontSize: 11)),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 8),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildDrawer(BuildContext context) {
    return Drawer(
      child: ListView(
        padding: EdgeInsets.zero,
        children: [
          DrawerHeader(
            decoration: BoxDecoration(
              color: Theme.of(context).colorScheme.primaryContainer,
            ),
            child: const Text(
              'OpenTrickler',
              style: TextStyle(fontSize: 24, fontWeight: FontWeight.bold),
            ),
          ),
          ListTile(
            leading: const Icon(Icons.tune),
            title: const Text('Profiles'),
            onTap: () {
              Navigator.pop(context);
              Navigator.push(
                context,
                MaterialPageRoute(
                  builder: (_) =>
                      ProfilesScreen(bleService: widget.bleService),
                ),
              );
            },
          ),
          ListTile(
            leading: const Icon(Icons.settings),
            title: const Text('Settings'),
            onTap: () {
              Navigator.pop(context);
              Navigator.push(
                context,
                MaterialPageRoute(
                  builder: (_) =>
                      SettingsScreen(bleService: widget.bleService),
                ),
              );
            },
          ),
          const Divider(),
          ListTile(
            leading: const Icon(Icons.bluetooth_disabled),
            title: const Text('Disconnect'),
            onTap: widget.bleService.disconnect,
          ),
        ],
      ),
    );
  }

  Widget _stateChip(AppState state) {
    final labels = {
      ChargeState.exit: ('Ready', Colors.grey),
      ChargeState.waitForZero: ('Waiting...', Colors.yellow),
      ChargeState.charging: ('Charging', Colors.amber),
      ChargeState.removeCup: ('Remove cup ✓', Colors.green),
      ChargeState.returnCup: ('Place new cup', Colors.blue),
    };
    final (label, color) = labels[state.chargeState]!;
    return Chip(
      label: Text(label),
      backgroundColor: color.withOpacity(0.2),
      side: BorderSide(color: color),
    );
  }

  Color _weightColor(AppState state) {
    switch (state.chargeState) {
      case ChargeState.exit:
        return Colors.white70;
      case ChargeState.waitForZero:
        return Colors.yellow;
      case ChargeState.charging:
        return Colors.amber;
      case ChargeState.removeCup:
        return Colors.green;
      case ChargeState.returnCup:
        return Colors.lightBlue;
    }
  }

  Widget _stepBtn(IconData icon, VoidCallback onTap) => IconButton(
        icon: Icon(icon, size: 20),
        onPressed: onTap,
        padding: EdgeInsets.zero,
        constraints: const BoxConstraints(minWidth: 32),
      );

  void _adjustTarget(double delta) {
    _targetEditing = false;
    _localTarget = (_localTarget + delta).clamp(0.0, 200.0);
    _localTarget = ((_localTarget) * 1000).round() / 1000;
    _targetCtrl.text = _localTarget.toStringAsFixed(3);
    widget.bleService.setTargetWeight(_localTarget);
  }

  void _start() {
    _targetEditing = false;
    final w = double.tryParse(_targetCtrl.text) ?? _localTarget;
    _localTarget = w;
    widget.bleService.startCharging(w);
  }

  void _abort() => widget.bleService.abortCharging();
}
