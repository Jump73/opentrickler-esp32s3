import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/ble_service.dart';
import '../models/app_state.dart';

class ProfilesScreen extends StatefulWidget {
  final BleService bleService;
  const ProfilesScreen({super.key, required this.bleService});

  @override
  State<ProfilesScreen> createState() => _ProfilesScreenState();
}

class _ProfilesScreenState extends State<ProfilesScreen> {
  @override
  void initState() {
    super.initState();
    widget.bleService.requestProfileSummary();
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();

    return Scaffold(
      appBar: AppBar(title: const Text('Profiles')),
      body: state.profiles.isEmpty
          ? const Center(child: CircularProgressIndicator())
          : ListView.builder(
              itemCount: state.profiles.length,
              itemBuilder: (ctx, i) {
                final p = state.profiles[i];
                final isActive = p.index == state.currentProfileIndex;
                return ListTile(
                  leading: CircleAvatar(
                    backgroundColor:
                        isActive ? Colors.amber.withOpacity(0.3) : null,
                    child: Text(
                      '${p.index}',
                      style: TextStyle(
                          color: isActive ? Colors.amber : null),
                    ),
                  ),
                  title: Text(
                    p.name.isEmpty ? '(empty)' : p.name,
                    style: TextStyle(
                        fontWeight: isActive ? FontWeight.bold : null),
                  ),
                  trailing: isActive
                      ? const Icon(Icons.check, color: Colors.amber)
                      : null,
                  onTap: () => _openProfile(ctx, p),
                );
              },
            ),
    );
  }

  void _openProfile(BuildContext ctx, ProfileInfo p) {
    widget.bleService.selectProfile(p.index);
    widget.bleService.requestProfileConfig(p.index);
    Navigator.push(
      ctx,
      MaterialPageRoute(
        builder: (_) => ProfileEditScreen(
          bleService: widget.bleService,
          profileIndex: p.index,
        ),
      ),
    );
  }
}

// ---------------------------------------------------------------------------

class ProfileEditScreen extends StatefulWidget {
  final BleService bleService;
  final int profileIndex;

  const ProfileEditScreen({
    super.key,
    required this.bleService,
    required this.profileIndex,
  });

  @override
  State<ProfileEditScreen> createState() => _ProfileEditScreenState();
}

class _ProfileEditScreenState extends State<ProfileEditScreen> {
  final _name = TextEditingController();
  final _cKp = TextEditingController();
  final _cKi = TextEditingController();
  final _cKd = TextEditingController();
  final _cMin = TextEditingController();
  final _cMax = TextEditingController();
  final _fKp = TextEditingController();
  final _fKi = TextEditingController();
  final _fKd = TextEditingController();
  final _fMin = TextEditingController();
  final _fMax = TextEditingController();
  bool _loaded = false;

  @override
  void dispose() {
    for (final c in [_name, _cKp, _cKi, _cKd, _cMin, _cMax,
                     _fKp, _fKi, _fKd, _fMin, _fMax]) {
      c.dispose();
    }
    super.dispose();
  }

  void _loadFrom(ProfileDetails d) {
    if (_loaded) return;
    _loaded = true;
    _name.text = d.name;
    _cKp.text = d.coarseKp.toStringAsFixed(5);
    _cKi.text = d.coarseKi.toStringAsFixed(5);
    _cKd.text = d.coarseKd.toStringAsFixed(5);
    _cMin.text = d.coarseMinSpeed.toStringAsFixed(4);
    _cMax.text = d.coarseMaxSpeed.toStringAsFixed(3);
    _fKp.text = d.fineKp.toStringAsFixed(5);
    _fKi.text = d.fineKi.toStringAsFixed(5);
    _fKd.text = d.fineKd.toStringAsFixed(5);
    _fMin.text = d.fineMinSpeed.toStringAsFixed(4);
    _fMax.text = d.fineMaxSpeed.toStringAsFixed(3);
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    if (state.currentProfileDetails?.index == widget.profileIndex) {
      _loadFrom(state.currentProfileDetails!);
    }

    return Scaffold(
      appBar: AppBar(
        title: Text('Profile ${widget.profileIndex}'),
        actions: [
          IconButton(
            icon: const Icon(Icons.save),
            tooltip: 'Save',
            onPressed: _save,
          ),
        ],
      ),
      body: !_loaded
          ? const Center(child: CircularProgressIndicator())
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                _field('Name', _name, text: true),
                const SizedBox(height: 20),
                _section('Coarse Motor PID'),
                Row(children: [
                  Expanded(child: _field('Kp', _cKp)),
                  const SizedBox(width: 8),
                  Expanded(child: _field('Ki', _cKi)),
                  const SizedBox(width: 8),
                  Expanded(child: _field('Kd', _cKd)),
                ]),
                const SizedBox(height: 8),
                Row(children: [
                  Expanded(child: _field('Min speed', _cMin)),
                  const SizedBox(width: 8),
                  Expanded(child: _field('Max speed', _cMax)),
                ]),
                const SizedBox(height: 20),
                _section('Fine Motor PID'),
                Row(children: [
                  Expanded(child: _field('Kp', _fKp)),
                  const SizedBox(width: 8),
                  Expanded(child: _field('Ki', _fKi)),
                  const SizedBox(width: 8),
                  Expanded(child: _field('Kd', _fKd)),
                ]),
                const SizedBox(height: 8),
                Row(children: [
                  Expanded(child: _field('Min speed', _fMin)),
                  const SizedBox(width: 8),
                  Expanded(child: _field('Max speed', _fMax)),
                ]),
                const SizedBox(height: 24),
                FilledButton(
                  onPressed: _save,
                  child: const Text('Save Profile'),
                ),
              ],
            ),
    );
  }

  Widget _section(String label) => Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Text(label,
            style: const TextStyle(
                fontWeight: FontWeight.bold, fontSize: 14)),
      );

  Widget _field(String label, TextEditingController ctrl,
      {bool text = false}) =>
      TextField(
        controller: ctrl,
        keyboardType: text
            ? TextInputType.text
            : const TextInputType.numberWithOptions(
                decimal: true, signed: false),
        decoration: InputDecoration(
          labelText: label,
          border: const OutlineInputBorder(),
          isDense: true,
        ),
      );

  void _save() {
    final state = context.read<AppState>();
    final orig = state.currentProfileDetails;
    final d = ProfileDetails(
      index: widget.profileIndex,
      name: _name.text,
      coarseKp: double.tryParse(_cKp.text) ?? orig?.coarseKp ?? 0.1,
      coarseKi: double.tryParse(_cKi.text) ?? orig?.coarseKi ?? 0.05,
      coarseKd: double.tryParse(_cKd.text) ?? orig?.coarseKd ?? 0.001,
      coarseMinSpeed: double.tryParse(_cMin.text) ?? orig?.coarseMinSpeed ?? 0.001,
      coarseMaxSpeed: double.tryParse(_cMax.text) ?? orig?.coarseMaxSpeed ?? 7.0,
      fineKp: double.tryParse(_fKp.text) ?? orig?.fineKp ?? 0.3,
      fineKi: double.tryParse(_fKi.text) ?? orig?.fineKi ?? 0.08,
      fineKd: double.tryParse(_fKd.text) ?? orig?.fineKd ?? 0.005,
      fineMinSpeed: double.tryParse(_fMin.text) ?? orig?.fineMinSpeed ?? 0.001,
      fineMaxSpeed: double.tryParse(_fMax.text) ?? orig?.fineMaxSpeed ?? 2.0,
    );
    widget.bleService.saveProfileConfig(d);
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Profile saved')),
      );
      Navigator.pop(context);
    }
  }
}
