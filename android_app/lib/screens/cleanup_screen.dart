import 'package:flutter/material.dart';
import '../services/ble_service.dart';

class CleanupScreen extends StatefulWidget {
  final BleService bleService;
  const CleanupScreen({super.key, required this.bleService});

  @override
  State<CleanupScreen> createState() => _CleanupScreenState();
}

class _CleanupScreenState extends State<CleanupScreen> {
  double _speed = 0.0;

  @override
  void initState() {
    super.initState();
    widget.bleService.setCleanupMode(true);
  }

  @override
  void dispose() {
    widget.bleService.setCleanupSpeed(0.0);
    widget.bleService.setCleanupMode(false);
    super.dispose();
  }

  void _stop() {
    setState(() => _speed = 0.0);
    widget.bleService.setCleanupSpeed(0.0);
  }

  @override
  Widget build(BuildContext context) {
    final running = _speed != 0.0;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Cleanup Mode'),
        leading: BackButton(
          onPressed: () {
            widget.bleService.setCleanupSpeed(0.0);
            widget.bleService.setCleanupMode(false);
            Navigator.pop(context);
          },
        ),
      ),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // Status card
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Row(
                  children: [
                    Icon(
                      running ? Icons.settings_backup_restore : Icons.stop_circle,
                      color: running ? Colors.amber : Colors.grey,
                      size: 32,
                    ),
                    const SizedBox(width: 12),
                    Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          running ? 'Motor Running' : 'Motor Stopped',
                          style: TextStyle(
                            fontWeight: FontWeight.bold,
                            color: running ? Colors.amber : Colors.grey,
                          ),
                        ),
                        Text(
                          'Speed: ${_speed.toStringAsFixed(2)} rps',
                          style: const TextStyle(fontSize: 12, color: Colors.grey),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 32),

            // Speed label and value
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text('Trickler Speed (rps)',
                    style: TextStyle(fontWeight: FontWeight.w500)),
                Text(
                  _speed.toStringAsFixed(1),
                  style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
                ),
              ],
            ),
            const SizedBox(height: 8),

            // Speed slider
            Slider(
              value: _speed,
              min: -5.0,
              max: 5.0,
              divisions: 100,
              label: _speed.toStringAsFixed(1),
              onChanged: (v) => setState(() => _speed = v),
              onChangeEnd: (v) => widget.bleService.setCleanupSpeed(v),
            ),

            // Range labels
            const Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text('-5 (reverse)', style: TextStyle(fontSize: 11, color: Colors.grey)),
                Text('+5 (forward)', style: TextStyle(fontSize: 11, color: Colors.grey)),
              ],
            ),
            const SizedBox(height: 32),

            // Stop button
            OutlinedButton.icon(
              icon: const Icon(Icons.stop),
              label: const Text('Stop'),
              style: OutlinedButton.styleFrom(
                minimumSize: const Size.fromHeight(52),
                side: const BorderSide(color: Colors.orange),
                foregroundColor: Colors.orange,
              ),
              onPressed: running ? _stop : null,
            ),
            const Spacer(),

            const Text(
              'Both motors run at the set speed.\nNegative values = reverse direction.\nSpeed resets on exit.',
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 12, color: Colors.grey),
            ),
          ],
        ),
      ),
    );
  }
}
