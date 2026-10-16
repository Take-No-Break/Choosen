// The folder ranking: one row per folder at the chosen level, best first, each with a glimpse of the
// folder's best images; a tap opens the folder.
import 'dart:async';

import 'package:flutter/material.dart';

import '../core/api.dart';
import '../core/models.dart';
import '../ui/photo.dart';
import '../ui/theme.dart';
import 'folder_page.dart';

class RankingScreen extends StatefulWidget {
  const RankingScreen({super.key, required this.state, required this.onOpenGallery});
  final AppState state;
  final VoidCallback onOpenGallery;

  @override
  State<RankingScreen> createState() => _RankingScreenState();
}

class _RankingScreenState extends State<RankingScreen> {
  static const _sorts = [('score', 'スコア'), ('liked', '好評の数'), ('rated', '評価した数'), ('name', '名前')];
  List<FolderRow> _folders = [];
  final _previews = FolderPreviews();
  int? _level;
  String _sort = 'score', _query = '';
  int _shownVersion = -1;

  @override
  void initState() {
    super.initState();
    widget.state.addListener(_onState);
    _load();
  }

  @override
  void dispose() {
    widget.state.removeListener(_onState);
    _previews.dispose();
    super.dispose();
  }

  // Showing the tab again (or closing a page on top) changes these dependencies.
  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _onState();
  }

  // Reload after new ratings when on screen, else when shown next: its tab is shown when Visibility
  // says so (IndexedStack) and no page covers it.
  void _onState() {
    final v = widget.state.state['version'] as int? ?? 0;
    if (v != _shownVersion && mounted && Visibility.of(context) && TickerMode.valuesOf(context).enabled) _load();
  }

  Future<void> _load() async {
    _shownVersion = widget.state.state['version'] as int? ?? 0;
    try {
      final r = await Api.get('/api/folders') as Map;
      if (!mounted) return;
      setState(() => _folders = [for (final f in r['folders'] as List) FolderRow(Map<String, dynamic>.from(f as Map))]);
      _previews.refresh();
    } catch (_) {}
  }

  void _open(FolderRow row) => FolderImagesPage.open(context, title: row.name, scope: row.scope, filter: 'all');

  @override
  Widget build(BuildContext context) {
    final ad = widget.state.artistDepth;
    final level = _level ?? ad;
    final maxDepth = _folders.fold<int>(1, (m, f) => f.depth > m ? f.depth : m);
    // At the ranking level: every folder ranked there, including a photo folder without subfolders.
    var rows = _folders.where((f) => level == ad ? f.artist : f.depth == level).toList();
    if (_query.isNotEmpty) rows = rows.where((f) => f.name.toLowerCase().contains(_query)).toList();
    rows.sort(switch (_sort) {
      'liked' => (a, b) => b.liked - a.liked,
      'rated' => (a, b) => b.rated - a.rated,
      'name' => (a, b) => a.name.compareTo(b.name),
      _ => (a, b) => (a.rank == 0 ? 1 << 30 : a.rank) - (b.rank == 0 ? 1 << 30 : b.rank),
    });
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 10, 8, 4),
          child: Row(
            children: [
              const Text('ランキング', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700)),
              const Spacer(),
              PopupMenuButton<int>(
                tooltip: '階層',
                initialValue: level,
                onSelected: (v) => setState(() => _level = v),
                itemBuilder: (_) => [
                  for (var d = 1; d <= maxDepth; d++)
                    PopupMenuItem(value: d, child: Text(d == ad ? '$d 階層目（標準）' : '$d 階層目')),
                ],
                child: Padding(
                  padding: const EdgeInsets.all(8),
                  child: Text('$level 階層目 ▾', style: TextStyle(color: muted)),
                ),
              ),
              PopupMenuButton<String>(
                tooltip: '並び順',
                icon: const Icon(Icons.sort),
                initialValue: _sort,
                onSelected: (v) => setState(() => _sort = v),
                itemBuilder: (_) => [for (final s in _sorts) PopupMenuItem(value: s.$1, child: Text(s.$2))],
              ),
            ],
          ),
        ),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          child: TextField(
            decoration: const InputDecoration(
              prefixIcon: Icon(Icons.search),
              hintText: 'フォルダ名で絞り込み',
              isDense: true,
              border: OutlineInputBorder(borderRadius: BorderRadius.all(Radius.circular(24))),
            ),
            onChanged: (v) => setState(() => _query = v.trim().toLowerCase()),
          ),
        ),
        const SizedBox(height: 6),
        Expanded(
          child: rows.isEmpty
              ? Center(
                  child: Text('まだ順位のついたフォルダがありません', style: TextStyle(color: muted)),
                )
              : LayoutBuilder(
                  builder: (_, box) => ListView.builder(
                    padding: const EdgeInsets.fromLTRB(12, 4, 12, 16),
                    itemCount: rows.length,
                    itemBuilder: (_, i) => FolderRankRow(
                      row: rows[i],
                      previews: _previews,
                      wide: box.maxWidth >= 720,
                      onTap: () => _open(rows[i]),
                    ),
                  ),
                ),
        ),
      ],
    );
  }
}

/// One folder of the ranking: its rank (the best three in medal colours), name, likes, super likes,
/// rated / all cards, score and up to four of its best images.
class FolderRankRow extends StatefulWidget {
  const FolderRankRow({
    super.key,
    required this.row,
    required this.previews,
    required this.wide,
    required this.onTap,
  });
  final FolderRow row;
  final FolderPreviews previews;
  final bool wide;
  final VoidCallback onTap;

  @override
  State<FolderRankRow> createState() => _FolderRankRowState();
}

class _FolderRankRowState extends State<FolderRankRow> {
  @override
  void initState() {
    super.initState();
    widget.previews.watch(widget.row.scope, _changed);
  }

  // The list reuses a row for another folder when it is sorted or searched.
  @override
  void didUpdateWidget(FolderRankRow old) {
    super.didUpdateWidget(old);
    if (old.row.scope != widget.row.scope || old.previews != widget.previews) {
      old.previews.unwatch(old.row.scope, _changed);
      widget.previews.watch(widget.row.scope, _changed);
    }
  }

  @override
  void dispose() {
    widget.previews.unwatch(widget.row.scope, _changed);
    super.dispose();
  }

  void _changed() {
    if (mounted) setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    final row = widget.row, wide = widget.wide;
    final medal = medalColor(row.rank);
    final paths = widget.previews.of(row.scope);
    final name = Text(
      row.name,
      maxLines: 1,
      overflow: TextOverflow.ellipsis,
      style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 15),
    );
    final score = Text(
      row.rated > 0 ? row.score.toStringAsFixed(1) : '—',
      style: TextStyle(
        fontSize: wide ? 20 : 17,
        fontWeight: FontWeight.w800,
        color: medal,
        fontFeatures: const [FontFeature.tabularFigures()],
      ),
    );
    final rank = _RankMark(
      rank: row.rank,
      move: row.rank > 0 && row.rank0 > 0 ? row.rank0 - row.rank : 0,
      medal: medal,
      width: wide ? 56 : 44,
    );
    final Widget body;
    if (wide) {
      body = Row(
        children: [
          rank,
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [name, const SizedBox(height: 6), _Stats(row: row)],
            ),
          ),
          const SizedBox(width: 12),
          _Strip(paths: paths, size: 76),
          const SizedBox(width: 16),
          SizedBox(
            width: 60,
            child: Align(alignment: Alignment.centerRight, child: score),
          ),
        ],
      );
    } else {
      body = Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          rank,
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(children: [Expanded(child: name), const SizedBox(width: 8), score]),
                const SizedBox(height: 4),
                _Stats(row: row),
                const SizedBox(height: 8),
                LayoutBuilder(
                  builder: (_, box) =>
                      _Strip(paths: paths, size: ((box.maxWidth - 3 * _Strip.gap) / 4).clamp(40.0, 84.0)),
                ),
              ],
            ),
          ),
        ],
      );
    }
    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      clipBehavior: Clip.antiAlias,
      shape: medal == null
          ? null
          : RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
              side: BorderSide(color: medal.withValues(alpha: .5)),
            ),
      child: InkWell(
        onTap: widget.onTap,
        child: Ink(
          padding: EdgeInsets.fromLTRB(wide ? 14 : 10, 10, wide ? 16 : 12, 10),
          decoration: medal == null
              ? null
              : BoxDecoration(
                  gradient: LinearGradient(
                    colors: [medal.withValues(alpha: .16), medal.withValues(alpha: 0)],
                    stops: const [0, .6],
                  ),
                ),
          child: body,
        ),
      ),
    );
  }
}

class _RankMark extends StatelessWidget {
  const _RankMark({required this.rank, required this.move, required this.medal, required this.width});
  final int rank, move; // move: places gained since this session began (negative = lost)
  final Color? medal;
  final double width;

  @override
  Widget build(BuildContext context) {
    final text = rank > 0 ? '$rank' : '—';
    final m = medal;
    return SizedBox(
      width: width,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (m != null)
            Container(
              width: 38,
              height: 38,
              alignment: Alignment.center,
              decoration: BoxDecoration(
                color: m,
                shape: BoxShape.circle,
                boxShadow: [BoxShadow(color: m.withValues(alpha: .45), blurRadius: 12)],
              ),
              child: Text(
                text,
                style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w900, color: Color(0xFF15171B)),
              ),
            )
          else
            FittedBox(
              fit: BoxFit.scaleDown,
              child: Text(
                text,
                style: const TextStyle(
                  fontSize: 22,
                  fontWeight: FontWeight.w800,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
            ),
          if (move != 0)
            Text(
              move > 0 ? '▲$move' : '▼${-move}',
              style: TextStyle(fontSize: 11, color: move > 0 ? Rate.like : Rate.pass),
            ),
        ],
      ),
    );
  }
}

class _Stats extends StatelessWidget {
  const _Stats({required this.row});
  final FolderRow row;

  @override
  Widget build(BuildContext context) {
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    final style = TextStyle(color: muted, fontSize: 12.5, fontFeatures: const [FontFeature.tabularFigures()]);
    Widget stat(String tip, IconData icon, Color color, String value) => Tooltip(
      message: tip,
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [Icon(icon, size: 14, color: color), const SizedBox(width: 3), Text(value, style: style)],
      ),
    );
    return Wrap(
      spacing: 12,
      runSpacing: 2,
      children: [
        stat('好評', Icons.favorite, Rate.like, '${row.liked}'),
        stat('超好評', Icons.star, Rate.superLike, '${row.supers}'),
        stat('評価済み / 全体', Icons.photo_library_outlined, muted, '${row.rated}/${row.total}'),
      ],
    );
  }
}

/// Up to four of the folder's best images as small squares (grey while they load).
class _Strip extends StatelessWidget {
  const _Strip({required this.paths, required this.size});
  static const gap = 4.0;
  final List<String>? paths; // null = not loaded yet
  final double size;

  @override
  Widget build(BuildContext context) {
    final dpr = MediaQuery.devicePixelRatioOf(context);
    final p = paths;
    return SizedBox(
      height: size,
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          for (var k = 0; k < FolderPreviews.count; k++) ...[
            if (k > 0) const SizedBox(width: gap),
            SizedBox.square(
              dimension: size,
              child: p != null && k >= p.length
                  ? null
                  : ClipRRect(
                      borderRadius: BorderRadius.circular(8),
                      child: p == null
                          ? const ColoredBox(color: Color(0xFF22252B))
                          // 1.5x the width: a landscape picture is cropped from its height
                          : Photo(path: p[k], fit: BoxFit.cover, cacheWidth: (size * dpr * 1.5).round()),
                    ),
            ),
          ],
        ],
      ),
    );
  }
}

/// The best images of the ranking's folders: up to [count] liked ones by score, else any. Asked
/// only for rows that are built and stay a moment ([settle]: a fast scroll through 2,000 folders asks
/// for nothing), [parallel] at a time, once per folder until the ranking reloads; a reload asks again
/// for the rows on screen (keeping their images meanwhile) and for the others when they are built.
class FolderPreviews {
  FolderPreviews({Future<List<String>> Function(String scope)? fetch}) : _fetch = fetch ?? _fromCore;

  static const count = 4, parallel = 2;
  static const settle = Duration(milliseconds: 150);
  final Future<List<String>> Function(String scope) _fetch;
  final Map<String, _Preview> _byScope = {};
  final List<_Preview> _queue = [];
  int _gen = 0, _running = 0;
  bool _disposed = false;

  /// The image files of the folder [scope], best first, or null while they are not known.
  List<String>? of(String scope) => _byScope[scope]?.paths;

  /// A row of [scope] is built; [onChange] runs when its images arrive.
  void watch(String scope, VoidCallback onChange) {
    final p = _byScope.putIfAbsent(scope, () => _Preview(scope));
    p.watchers.add(onChange);
    _want(p);
  }

  /// The row is gone (scrolled away, or now shows another folder).
  void unwatch(String scope, VoidCallback onChange) {
    final p = _byScope[scope];
    if (p == null || !p.watchers.remove(onChange) || p.watchers.isNotEmpty) return;
    p.wait?.cancel();
    p.wait = null;
    if (p.queued) {
      p.queued = false;
      _queue.remove(p);
    }
  }

  /// The ranking was loaded again (new ratings).
  void refresh() {
    _gen++;
    for (final p in _byScope.values) {
      if (p.watchers.isNotEmpty) _want(p);
    }
  }

  void dispose() {
    _disposed = true;
    for (final p in _byScope.values) {
      p.wait?.cancel();
    }
    _queue.clear();
  }

  void _want(_Preview p) {
    if (_disposed || p.watchers.isEmpty || p.gen == _gen || p.busy || p.queued || p.wait != null) return;
    p.wait = Timer(settle, () {
      p.wait = null;
      if (p.watchers.isEmpty) return;
      p.queued = true;
      _queue.add(p);
      _pump();
    });
  }

  void _pump() {
    while (!_disposed && _running < parallel && _queue.isNotEmpty) {
      _load(_queue.removeAt(0)..queued = false);
    }
  }

  Future<void> _load(_Preview p) async {
    final gen = _gen;
    _running++;
    p.busy = true;
    try {
      p.paths = await _fetch(p.scope);
    } catch (_) {
      // not ready yet: the row stays grey until the ranking reloads
    }
    p.gen = gen;
    p.busy = false;
    _running--;
    if (_disposed) return;
    for (final f in [...p.watchers]) {
      f();
    }
    _want(p); // the ranking reloaded while this was loading
    _pump();
  }

  static Future<List<String>> _fromCore(String scope) async {
    Future<List> best(String filter) async {
      final r = await Api.get('/api/items', {
        'filter': filter,
        'scope': scope,
        'sort': 'score',
        'limit': '$count',
        'offset': '0',
      }) as Map;
      return r['items'] as List;
    }

    var items = await best('liked');
    if (items.isEmpty) items = await best('all');
    return [for (final j in items) CardItem(Map<String, dynamic>.from(j as Map)).path];
  }
}

class _Preview {
  _Preview(this.scope);
  final String scope;
  final Set<VoidCallback> watchers = {};
  List<String>? paths;
  int gen = -1; // the ranking load these paths belong to
  Timer? wait; // settling before it asks
  bool queued = false, busy = false;
}
