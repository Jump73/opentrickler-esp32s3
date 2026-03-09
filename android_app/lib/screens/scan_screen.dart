import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:provider/provider.dart';
import '../services/ble_service.dart';
import '../models/app_state.dart';

class ScanScreen extends StatefulWidget {
  final BleService bleService;
  const ScanScreen({super.key, required this.bleService});

  @override
  State<ScanScreen> createState() => _ScanScreenState();
}

class _ScanScreenState extends State<ScanScreen> {
  @override
  void initState() {
    super.initState();
    widget.bleService.startScan();
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();

    return Scaffold(
      appBar: AppBar(
        title: const Text('OpenTrickler'),
        centerTitle: true,
      ),
      body: Column(
        children: [
          if (state.isConnecting)
            const LinearProgressIndicator()
          else
            const SizedBox(height: 4),
          Padding(
            padding: const EdgeInsets.all(16),
            child: Text(
              state.isConnecting
                  ? 'Connecting...'
                  : 'Searching for OpenTrickler...',
              style: Theme.of(context).textTheme.bodyMedium,
            ),
          ),
          Expanded(
            child: StreamBuilder<List<ScanResult>>(
              stream: widget.bleService.scanResults,
              builder: (ctx, snap) {
                final results = snap.data ?? [];
                if (results.isEmpty) {
                  return const Center(
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        CircularProgressIndicator(),
                        SizedBox(height: 24),
                        Text('Scanning for BLE devices...'),
                      ],
                    ),
                  );
                }
                return ListView.builder(
                  itemCount: results.length,
                  itemBuilder: (ctx, i) {
                    final r = results[i];
                    final name = r.advertisementData.advName.isNotEmpty
                        ? r.advertisementData.advName
                        : r.device.platformName.isNotEmpty
                            ? r.device.platformName
                            : 'Unknown';
                    return ListTile(
                      leading: const Icon(Icons.bluetooth),
                      title: Text(name),
                      subtitle: Text(r.device.remoteId.toString()),
                      trailing: Text('${r.rssi} dBm'),
                      onTap: state.isConnecting ? null : () => _connect(r.device),
                    );
                  },
                );
              },
            ),
          ),
        ],
      ),
      floatingActionButton: StreamBuilder<bool>(
        stream: widget.bleService.isScanning,
        builder: (ctx, snap) {
          final scanning = snap.data ?? false;
          return FloatingActionButton(
            onPressed: scanning
                ? widget.bleService.stopScan
                : widget.bleService.startScan,
            child: Icon(scanning ? Icons.stop : Icons.search),
          );
        },
      ),
    );
  }

  Future<void> _connect(BluetoothDevice device) async {
    await widget.bleService.stopScan();
    try {
      await widget.bleService.connect(device);
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Connection failed: $e')),
        );
        widget.bleService.startScan();
      }
    }
  }
}
