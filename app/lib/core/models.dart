// What the screens show, read from the core's JSON.
import 'dart:async';

import 'package:flutter/foundation.dart';

import 'api.dart';
import 'native.dart';

/// One card (an image, or a Pixiv work with several pages).
class CardItem {
  CardItem(this.json)
    : id = json['id'] as int,
      fp = json['fp'] as String,
      name = json['name'] as String? ?? '',
      dir = json['dir'] as String? ?? '',
      kind = json['kind'] as String? ?? '';

  final Map<String, dynamic> json;
  final int id;
  final String fp, name, dir, kind;

  bool get rated => json['rated'] == true;
  num get points => json['p'] as num? ?? 0;
  int get timesRated => json['n'] as int? ?? 0;
  int get likes => json['nL'] as int? ?? 0;
  int get supers => json['nS'] as int? ?? 0;
  String get last => json['last'] as String? ?? '';
  bool get archived => json['archived'] == true;

  /// The image file, or "" if the core no longer knows it.
  String get path => Native.itemPath(id, fp);

  /// The folder shown above the file name: a further photo folder's full path becomes its name.
  String get place {
    final first = dir.split('/').first;
    if (first.contains(':') || first.contains('\\')) {
      final name = first.split(RegExp(r'[\\/]')).where((s) => s.isNotEmpty).last;
      return [name, ...dir.split('/').skip(1)].join(' › ');
    }
    return dir.split('/').join(' › ');
  }
}

/// The scope token (/api/items?scope=, /api/next?scope=) for one folder with its subfolders. A photo
/// folder itself (depth 0) stands for the images directly in it, as its tile does in Version 1's
/// folder view; besides, the first photo folder's own path "" alone would mean everything.
String folderScope(String path, int depth) => depth == 0 ? '$path/' : path;

/// A folder row of the ranking (and of the scope picker).
class FolderRow {
  FolderRow(Map<String, dynamic> j)
    : path = j['path'] as String,
      name = j['name'] as String,
      depth = j['depth'] as int,
      total = j['total'] as int,
      rated = j['rated'] as int,
      liked = j['liked'] as int,
      supers = j['supers'] as int? ?? 0,
      score = (j['score'] as num).toDouble(),
      rank = j['rank'] as int,
      rank0 = j['rank0'] as int? ?? 0,
      artist = j['artist'] == true;
  final String path, name;
  final int depth, total, rated, liked, supers, rank, rank0;
  final double score;
  final bool artist;

  String get scope => folderScope(path, depth);
}

/// A tile of the gallery's folder view (/api/folderCards): a ranking-level folder, how many of its
/// images match the filter, and the best of them as cover.
class FolderCard {
  FolderCard(Map<String, dynamic> j)
    : path = j['path'] as String,
      name = j['name'] as String,
      depth = j['depth'] as int,
      rank = j['rank'] as int,
      count = j['count'] as int,
      coverId = (j['cover'] as Map)['id'] as int,
      coverFp = (j['cover'] as Map)['fp'] as String;
  final String path, name, coverFp;
  final int depth, rank, count, coverId;

  String get scope => folderScope(path, depth);

  /// The cover's image file, or "" if the core no longer knows it.
  late final String coverPath = Native.itemPath(coverId, coverFp);
}

/// The library's state (counts, settings, scan progress). Refreshes itself whenever the core's
/// version changes, so every screen sees new ratings and scans at once.
class AppState extends ChangeNotifier {
  AppState() {
    _timer = Timer.periodic(const Duration(milliseconds: 700), (_) => _poll());
    refresh();
  }

  Map<String, dynamic> state = const {};
  int _version = -1;
  Timer? _timer;

  bool get ready => state.isNotEmpty;
  bool get hasRoot => state['hasRoot'] == true;
  bool get scanning => state['scanning'] == true;
  int get items => state['items'] as int? ?? 0;
  int get rated => state['rated'] as int? ?? 0;
  int get keyMode => state['keyMode'] as int? ?? 1;
  int get mouseMode => state['mouseMode'] as int? ?? 1;
  String get customMouse => state['customMouse'] as String? ?? ''; // an own mouse layout saved by Version 1
  bool get swipeButtons => state['swipeButtons'] != false; // the round buttons on the card
  double get reviewShare => (state['reviewShare'] as num?)?.toDouble() ?? .1;
  int get artistDepth => state['artistDepth'] as int? ?? 1;
  String get root => state['root'] as String? ?? '';
  List<String> get moreRoots => [for (final r in state['moreRoots'] as List? ?? const []) r as String];
  List<int> get maxDepths => [for (final d in state['maxDepths'] as List? ?? const []) d as int];

  Future<void> refresh() async {
    try {
      state = Map<String, dynamic>.from(await Api.get('/api/state') as Map);
      notifyListeners();
    } catch (_) {
      // the core answers once its index is loaded; the next poll tries again
    }
  }

  void _poll() {
    final v = Native.version();
    if (v != _version) {
      _version = v;
      refresh();
    }
  }

  Future<String?> setConfig(Map<String, String> values) async {
    final r = await Api.post('/api/config', values) as Map;
    await refresh();
    return r['ok'] == true ? null : r['error'] as String?;
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }
}
