import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'models/app_state.dart';
import 'services/ble_service.dart';
import 'screens/scan_screen.dart';
import 'screens/home_screen.dart';

void main() {
  final appState = AppState();
  final bleService = BleService(appState);
  runApp(
    MultiProvider(
      providers: [
        ChangeNotifierProvider.value(value: appState),
        Provider.value(value: bleService),
      ],
      child: const OpenTricklerApp(),
    ),
  );
}

class OpenTricklerApp extends StatelessWidget {
  const OpenTricklerApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'OpenTrickler',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.amber,
          brightness: Brightness.dark,
        ),
      ),
      home: Consumer<AppState>(
        builder: (ctx, state, _) {
          final ble = ctx.read<BleService>();
          return state.isConnected
              ? HomeScreen(bleService: ble)
              : ScanScreen(bleService: ble);
        },
      ),
    );
  }
}
