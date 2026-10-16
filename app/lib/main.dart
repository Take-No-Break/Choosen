// Choosen Version 2: the same core (ratings, schedule, ranking) with a new Flutter interface for
// Windows and Android.
import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';

import 'core/models.dart';
import 'core/native.dart';
import 'screens/gallery_screen.dart';
import 'screens/ranking_screen.dart';
import 'screens/settings_screen.dart';
import 'screens/swipe_screen.dart';
import 'ui/theme.dart';

Future<void> main([List<String> args = const []]) async {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(_Root(dataDir: await _dataDir(args)));
}

/// Windows: `--data <folder>` (the desktop shortcut passes Version 1's data folder, so both
/// versions share the ratings), else CHOOSEN_DATA (development runs use a copy), else
/// "choosen-data" next to the exe ("data" there holds Flutter's own files). Android: the app's
/// own storage, removed with the app.
Future<String> _dataDir(List<String> args) async {
  final i = args.indexOf('--data');
  final arg = i >= 0 && i + 1 < args.length ? args[i + 1] : '';
  final env = Platform.environment['CHOOSEN_DATA'] ?? '';
  final String dir;
  if (arg.isNotEmpty) {
    dir = arg;
  } else if (env.isNotEmpty) {
    dir = env;
  } else if (Platform.isWindows) {
    dir = '${File(Platform.resolvedExecutable).parent.path}\\choosen-data';
  } else {
    dir = '${(await getApplicationSupportDirectory()).path}/data';
  }
  Directory(dir).createSync(recursive: true);
  return dir;
}

/// Opens the library once this app may use the data folder. Version 1 (or another Version 2) may
/// have it open, and two apps writing one rating journal would break it; until it is free, this
/// shows who has it and tries again every second.
class _Root extends StatefulWidget {
  const _Root({required this.dataDir});
  final String dataDir;

  @override
  State<_Root> createState() => _RootState();
}

class _RootState extends State<_Root> {
  AppState? _state;
  String _holder = '';
  Timer? _retry;

  @override
  void initState() {
    super.initState();
    _open();
    if (_state == null) _retry = Timer.periodic(const Duration(seconds: 1), (_) => setState(_open));
  }

  void _open() {
    if (_state != null) return;
    _holder = Native.init(widget.dataDir);
    if (_holder.isEmpty) {
      _retry?.cancel();
      _state = AppState();
    }
  }

  @override
  void dispose() {
    _retry?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final state = _state;
    if (state != null) return ChoosenApp(state: state);
    return MaterialApp(
      title: 'Choosen V2',
      debugShowCheckedModeBanner: false,
      theme: choosenTheme(),
      home: _InUse(holder: _holder, dataDir: widget.dataDir),
    );
  }
}

class _InUse extends StatefulWidget {
  const _InUse({required this.holder, required this.dataDir});
  final String holder, dataDir;

  @override
  State<_InUse> createState() => _InUseState();
}

class _InUseState extends State<_InUse> {
  bool _quitting = false, _failed = false;

  /// Asks Version 1 to quit (the same request as its "アプリを終了" button); the retry opens the
  /// library as soon as it is gone.
  Future<void> _quitV1() async {
    setState(() => _quitting = true);
    var ok = false;
    try {
      final cfg = File('${widget.dataDir}${Platform.pathSeparator}config.txt');
      final m = RegExp(r'^port=(\d+)', multiLine: true).firstMatch(cfg.existsSync() ? cfg.readAsStringSync() : '');
      final port = int.tryParse(m?.group(1) ?? '') ?? 47800;
      final client = HttpClient()..connectionTimeout = const Duration(seconds: 2);
      final req = await client.postUrl(Uri.parse('http://127.0.0.1:$port/api/quit'));
      req.headers.set('X-Choosen', '1');
      final res = await req.close();
      ok = res.statusCode == 200;
      await res.drain<void>();
      client.close();
    } catch (_) {
      // not reachable: V1 may be closing already, or runs on another port
    }
    if (mounted && !ok) setState(() => _failed = true);
  }

  @override
  Widget build(BuildContext context) {
    final v2 = widget.holder.startsWith('Choosen V2');
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    return Scaffold(
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.lock_clock_outlined, size: 56, color: muted),
              const SizedBox(height: 14),
              Text(
                v2 ? 'Choosen V2 はすでに開いています' : 'Choosen（V1）が開いています',
                style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w700),
              ),
              const SizedBox(height: 8),
              Text(
                v2
                    ? 'もう一つのウィンドウを使ってください。'
                    : _failed
                    ? 'V1 を終了できませんでした。V1 の設定の「アプリを終了」で終了してください。'
                    : '同じ評価データは 1 つずつしか開けません。V1 が終了すると、自動でこちらが開きます。',
                textAlign: TextAlign.center,
                style: TextStyle(color: muted),
              ),
              if (!v2) ...[
                const SizedBox(height: 20),
                FilledButton(
                  onPressed: _quitting && !_failed ? null : _quitV1,
                  child: Text(_quitting && !_failed ? '終了を待っています…' : 'V1 を終了する'),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

class ChoosenApp extends StatelessWidget {
  const ChoosenApp({super.key, required this.state});
  final AppState state;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Choosen V2',
      debugShowCheckedModeBanner: false,
      theme: choosenTheme(),
      home: Shell(state: state),
    );
  }
}

class Shell extends StatefulWidget {
  const Shell({super.key, required this.state});
  final AppState state;

  @override
  State<Shell> createState() => _ShellState();
}

class _ShellState extends State<Shell> {
  // CHOOSEN_TAB (0-3) opens another tab first: used to take screenshots of each screen.
  int _tab = int.tryParse(Platform.environment['CHOOSEN_TAB'] ?? '') ?? 0;

  static const _destinations = [
    (Icons.style_outlined, Icons.style, 'スワイプ'),
    (Icons.leaderboard_outlined, Icons.leaderboard, 'ランキング'),
    (Icons.photo_library_outlined, Icons.photo_library, 'ギャラリー'),
    (Icons.tune_outlined, Icons.tune, '設定'),
  ];

  @override
  void initState() {
    super.initState();
    widget.state.addListener(_firstRun);
  }

  // Without a photo folder there is nothing to swipe: start in the settings.
  void _firstRun() {
    if (!widget.state.ready) return;
    widget.state.removeListener(_firstRun);
    if (!widget.state.hasRoot && !Platform.environment.containsKey('CHOOSEN_TAB')) setState(() => _tab = 3);
  }

  void go(int tab) => setState(() => _tab = tab);

  @override
  Widget build(BuildContext context) {
    final wide = MediaQuery.sizeOf(context).width >= 760;
    // every screen redraws when the library state changes (new ratings, scan progress, settings)
    final body = ListenableBuilder(
      listenable: widget.state,
      // the screens are created here, inside the builder, so they really are rebuilt
      builder: (_, _) => IndexedStack(
        index: _tab,
        children: [
          SwipeScreen(state: widget.state, active: _tab == 0),
          RankingScreen(state: widget.state, onOpenGallery: () => go(2)),
          GalleryScreen(state: widget.state),
          SettingsScreen(state: widget.state, onFolderReady: () => go(0)),
        ],
      ),
    );
    if (wide) {
      return Scaffold(
        body: Row(
          children: [
            NavigationRail(
              selectedIndex: _tab,
              onDestinationSelected: go,
              labelType: NavigationRailLabelType.all,
              leading: const Padding(
                padding: EdgeInsets.symmetric(vertical: 12),
                child: Text('Choosen V2', style: TextStyle(fontWeight: FontWeight.w800, letterSpacing: .5)),
              ),
              destinations: [
                for (final d in _destinations)
                  NavigationRailDestination(icon: Icon(d.$1), selectedIcon: Icon(d.$2), label: Text(d.$3)),
              ],
            ),
            const VerticalDivider(width: 1),
            Expanded(child: body),
          ],
        ),
      );
    }
    return Scaffold(
      body: SafeArea(bottom: false, child: body),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tab,
        onDestinationSelected: go,
        height: 64,
        destinations: [
          for (final d in _destinations) NavigationDestination(icon: Icon(d.$1), selectedIcon: Icon(d.$2), label: d.$3),
        ],
      ),
    );
  }
}
