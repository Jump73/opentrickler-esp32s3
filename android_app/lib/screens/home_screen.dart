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

    if (!_targetEditing && state.targetWeight > 0 && _localTarget == 0.0) {
      _localTarget = state.targetWeight;
      _targetCtrl.text = _localTarget.toStringAsFixed(2);
    }

    final isActive = state.chargeState != ChargeState.exit;

    return Scaffold(
      appBar: AppBar(
        title: const Text('OpenTrickler'),
        centerTitle: true,
        actions: [
          IconButton(
            icon: const Icon(Icons.bluetooth_connected),
            tooltip: 'Disconnect',
            onPressed: widget.bleService.disconnect,
          ),
        ],
      ),
      drawer: _buildDrawer(context, state),
      body: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // ── Stat boxes row ───────────────────────────────────────
              Row(
                children: [
                  Expanded(child: _weightBox(context, state)),
                  const SizedBox(width: 12),
                  Expanded(child: _timeBox(context, state)),
                ],
              ),
              const SizedBox(height: 10),

              // ── Progress bar ─────────────────────────────────────────
              _progressBar(state),
              const SizedBox(height: 10),

              // ── Stepper ──────────────────────────────────────────────
              _chargeStepper(context, state),
              const SizedBox(height: 10),

              // ── Result banner ─────────────────────────────────────────
              if (state.chargeState == ChargeState.removeCup ||
                  state.chargeState == ChargeState.returnCup)
                _resultBanner(context, state),

              // ── Profile name ─────────────────────────────────────────
              if (state.profileName.isNotEmpty)
                Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: GestureDetector(
                    onTap: () => _openProfiles(context),
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        const Icon(Icons.tune, size: 16, color: Colors.amber),
                        const SizedBox(width: 4),
                        Text(
                          state.profileName,
                          style: const TextStyle(
                              color: Colors.amber, fontWeight: FontWeight.w600),
                        ),
                      ],
                    ),
                  ),
                ),

              // ── Target weight input ──────────────────────────────────
              Row(
                children: [
                  _stepBtn(Icons.remove,              () => _adjustTarget(-0.02)),
                  _stepBtn(Icons.remove_circle_outline, () => _adjustTarget(-0.1)),
                  Expanded(
                    child: TextField(
                      controller: _targetCtrl,
                      keyboardType:
                          const TextInputType.numberWithOptions(decimal: true),
                      textAlign: TextAlign.center,
                      style: const TextStyle(
                          fontSize: 18, fontWeight: FontWeight.bold),
                      decoration: const InputDecoration(
                        labelText: 'Target (gn)',
                        border: OutlineInputBorder(),
                        contentPadding: EdgeInsets.symmetric(vertical: 12),
                      ),
                      onTap: () => _targetEditing = true,
                      onSubmitted: (v) {
                        _targetEditing = false;
                        _localTarget = double.tryParse(v) ?? _localTarget;
                        _targetCtrl.text = _localTarget.toStringAsFixed(2);
                        widget.bleService.setTargetWeight(_localTarget);
                      },
                    ),
                  ),
                  _stepBtn(Icons.add_circle_outline, () => _adjustTarget(0.1)),
                  _stepBtn(Icons.add,                () => _adjustTarget(0.02)),
                ],
              ),
              const SizedBox(height: 10),

              // ── Action buttons ───────────────────────────────────────
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
                        isActive ? 'STOP' : 'START',
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
                          minimumSize: const Size.fromHeight(56)),
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
              const SizedBox(height: 16),

              // ── Charge history ───────────────────────────────────────
              if (state.chargeHistory.isNotEmpty) ...[
                const Divider(),
                _historyTable(context, state),
              ],
            ],
          ),
        ),
      ),
    );
  }

  // ---------------------------------------------------------------------------
  // Widgets
  // ---------------------------------------------------------------------------

  Widget _weightBox(BuildContext context, AppState state) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Weight',
                style: Theme.of(context)
                    .textTheme
                    .labelSmall
                    ?.copyWith(color: Colors.grey)),
            const SizedBox(height: 4),
            FittedBox(
              fit: BoxFit.scaleDown,
              alignment: Alignment.centerLeft,
              child: Text(
                _fmt(state.currentWeight),
                style: TextStyle(
                  fontSize: 52,
                  fontWeight: FontWeight.bold,
                  color: _weightColor(state),
                  letterSpacing: -1,
                ),
              ),
            ),
            Text('gn',
                style: Theme.of(context)
                    .textTheme
                    .bodySmall
                    ?.copyWith(color: Colors.grey)),
          ],
        ),
      ),
    );
  }

  Widget _timeBox(BuildContext context, AppState state) {
    final isActive = state.chargeState == ChargeState.waitForZero ||
        state.chargeState == ChargeState.charging;
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Time',
                style: Theme.of(context)
                    .textTheme
                    .labelSmall
                    ?.copyWith(color: Colors.grey)),
            const SizedBox(height: 4),
            FittedBox(
              fit: BoxFit.scaleDown,
              alignment: Alignment.centerLeft,
              child: Text(
                isActive ? '${state.elapsedTime} s' : '--- s',
                style: const TextStyle(
                    fontSize: 52, fontWeight: FontWeight.bold),
              ),
            ),
            Text(
              (state.chargeState == ChargeState.removeCup ||
                      state.chargeState == ChargeState.returnCup)
                  ? 'Settled: ${_fmt(state.settledWeight)} gn'
                  : 'Target: ${state.targetWeight.toStringAsFixed(2)} gn',
              style: Theme.of(context)
                  .textTheme
                  .bodySmall
                  ?.copyWith(color: Colors.grey),
            ),
          ],
        ),
      ),
    );
  }

  Widget _progressBar(AppState state) {
    double frac = 0;
    if (state.targetWeight > 0) {
      final w = double.tryParse(state.currentWeight) ?? 0;
      frac = (w / state.targetWeight).clamp(0.0, 1.0);
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        ClipRRect(
          borderRadius: BorderRadius.circular(4),
          child: LinearProgressIndicator(
            value: frac,
            minHeight: 8,
            backgroundColor: Colors.grey[800],
            color: _progressColor(state, frac),
          ),
        ),
        const SizedBox(height: 2),
        Text(
          '${(frac * 100).toStringAsFixed(0)}%',
          textAlign: TextAlign.right,
          style: const TextStyle(fontSize: 11, color: Colors.grey),
        ),
      ],
    );
  }

  Widget _chargeStepper(BuildContext context, AppState state) {
    const steps = [
      ('Wait',    ChargeState.waitForZero),
      ('Charge',  ChargeState.charging),
      ('Remove',  ChargeState.removeCup),
      ('Return',  ChargeState.returnCup),
    ];
    final activeIdx = switch (state.chargeState) {
      ChargeState.exit        => -1,
      ChargeState.waitForZero => 0,
      ChargeState.charging    => 1,
      ChargeState.removeCup   => 2,
      ChargeState.returnCup   => 3,
    };
    return Row(
      children: List.generate(steps.length * 2 - 1, (i) {
        if (i.isOdd) {
          final stepIdx = i ~/ 2;
          final done = stepIdx < activeIdx;
          return Expanded(
            child: Container(
              height: 2,
              color: done ? Colors.amber : Colors.grey[700],
            ),
          );
        }
        final stepIdx = i ~/ 2;
        final (label, _) = steps[stepIdx];
        final active = stepIdx == activeIdx;
        final done   = stepIdx < activeIdx;
        return Column(
          children: [
            Container(
              width: 28,
              height: 28,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: active
                    ? Colors.amber
                    : done
                        ? Colors.amber.withOpacity(0.4)
                        : Colors.grey[800],
                border: Border.all(
                  color: active || done ? Colors.amber : Colors.grey,
                  width: 1.5,
                ),
              ),
              child: Icon(
                done ? Icons.check : Icons.circle,
                size: done ? 16 : 8,
                color: active || done ? Colors.black : Colors.grey,
              ),
            ),
            const SizedBox(height: 4),
            Text(label,
                style: TextStyle(
                  fontSize: 10,
                  color: active ? Colors.amber : Colors.grey,
                  fontWeight:
                      active ? FontWeight.bold : FontWeight.normal,
                )),
          ],
        );
      }),
    );
  }

  Widget _resultBanner(BuildContext context, AppState state) {
    final event = state.chargeEvent;
    final isOver  = event & kEventOver  != 0;
    final isUnder = event & kEventUnder != 0;
    final (text, color) = isOver
        ? ('OVER CHARGE', Colors.red)
        : isUnder
            ? ('UNDER CHARGE', Colors.orange)
            : ('OK', Colors.green);

    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 16),
      decoration: BoxDecoration(
        color: color.withOpacity(0.15),
        border: Border.all(color: color),
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            isOver ? Icons.arrow_upward :
            isUnder ? Icons.arrow_downward : Icons.check_circle,
            color: color, size: 20,
          ),
          const SizedBox(width: 8),
          Text(text,
              style: TextStyle(
                  color: color,
                  fontWeight: FontWeight.bold,
                  fontSize: 16)),
          if (!isOver && !isUnder) ...[
            const SizedBox(width: 8),
            Text(_fmt(state.settledWeight),
                style: TextStyle(color: color, fontSize: 14)),
            Text(' gn', style: TextStyle(color: color.withOpacity(0.7), fontSize: 14)),
          ],
        ],
      ),
    );
  }

  Widget _historyTable(BuildContext context, AppState state) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text('History',
            style: Theme.of(context)
                .textTheme
                .titleSmall
                ?.copyWith(color: Colors.grey)),
        const SizedBox(height: 6),
        SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          child: DataTable(
            headingRowHeight: 32,
            dataRowMinHeight: 28,
            dataRowMaxHeight: 32,
            columnSpacing: 16,
            headingTextStyle: const TextStyle(
                fontSize: 11, color: Colors.grey, fontWeight: FontWeight.bold),
            dataTextStyle: const TextStyle(fontSize: 12),
            columns: const [
              DataColumn(label: Text('#')),
              DataColumn(label: Text('Target'), numeric: true),
              DataColumn(label: Text('Weight'), numeric: true),
              DataColumn(label: Text('Error'), numeric: true),
              DataColumn(label: Text('Time'), numeric: true),
              DataColumn(label: Text('Result')),
            ],
            rows: state.chargeHistory.asMap().entries.map((e) {
              final idx = e.key;
              final r   = e.value;
              final color = r.result == 'OK'
                  ? Colors.green
                  : r.result == 'OVER'
                      ? Colors.red
                      : Colors.orange;
              return DataRow(cells: [
                DataCell(Text('${state.chargeHistory.length - idx}')),
                DataCell(Text(r.target.toStringAsFixed(2))),
                DataCell(Text(_fmt(r.weight))),
                DataCell(Text(
                  '${r.error >= 0 ? '+' : ''}${r.error.toStringAsFixed(2)}',
                  style: TextStyle(
                      color: r.error.abs() < 0.01 ? Colors.green : Colors.orange),
                )),
                DataCell(Text('${r.time} s')),
                DataCell(Text(r.result,
                    style: TextStyle(
                        color: color, fontWeight: FontWeight.bold))),
              ]);
            }).toList(),
          ),
        ),
      ],
    );
  }

  Widget _buildDrawer(BuildContext context, AppState state) {
    return Drawer(
      child: ListView(
        padding: EdgeInsets.zero,
        children: [
          DrawerHeader(
            decoration: BoxDecoration(
              color: Theme.of(context).colorScheme.primaryContainer,
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                const Text('OpenTrickler',
                    style:
                        TextStyle(fontSize: 22, fontWeight: FontWeight.bold)),
                if (state.profileName.isNotEmpty)
                  Text(state.profileName,
                      style: const TextStyle(color: Colors.amber)),
              ],
            ),
          ),
          ListTile(
            leading: const Icon(Icons.tune),
            title: const Text('Profiles'),
            onTap: () {
              Navigator.pop(context);
              _openProfiles(context);
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

  // ---------------------------------------------------------------------------
  // Helpers
  // ---------------------------------------------------------------------------

  // Reformat weight string from ESP32 (always "X.XXX") to 2 decimal places.
  String _fmt(String w) {
    final v = double.tryParse(w);
    return v != null ? v.toStringAsFixed(2) : w;
  }

  Color _weightColor(AppState state) => switch (state.chargeState) {
        ChargeState.exit        => Colors.white70,
        ChargeState.waitForZero => Colors.yellow,
        ChargeState.charging    => Colors.amber,
        ChargeState.removeCup   => Colors.green,
        ChargeState.returnCup   => Colors.lightBlue,
      };

  Color _progressColor(AppState state, double frac) {
    if (frac >= 1.0) return Colors.red;
    if (frac >= 0.9) return Colors.orange;
    return Colors.amber;
  }

  Widget _stepBtn(IconData icon, VoidCallback onTap) => IconButton(
        icon: Icon(icon, size: 22),
        onPressed: onTap,
        padding: EdgeInsets.zero,
        constraints: const BoxConstraints(minWidth: 36),
      );

  void _adjustTarget(double delta) {
    _targetEditing = false;
    _localTarget = (_localTarget + delta).clamp(0.0, 200.0);
    _localTarget = ((_localTarget) * 1000).round() / 1000;
    _targetCtrl.text = _localTarget.toStringAsFixed(2);
    widget.bleService.setTargetWeight(_localTarget);
  }

  void _start() {
    _targetEditing = false;
    final w = double.tryParse(_targetCtrl.text) ?? _localTarget;
    _localTarget = w;
    widget.bleService.startCharging(w);
  }

  void _abort() => widget.bleService.abortCharging();

  void _openProfiles(BuildContext context) {
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => ProfilesScreen(bleService: widget.bleService),
      ),
    );
  }
}
