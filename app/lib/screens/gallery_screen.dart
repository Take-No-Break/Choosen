// The gallery: rated (or all) images as a grid, or one tile per folder; a tap opens them full screen.
import 'package:flutter/material.dart';

import '../core/api.dart';
import '../core/models.dart';
import '../ui/photo.dart';
import '../ui/theme.dart';
import 'folder_page.dart';

class GalleryScreen extends StatefulWidget {
  const GalleryScreen({super.key, required this.state});
  final AppState state;

  @override
  State<GalleryScreen> createState() => _GalleryScreenState();
}

class _GalleryScreenState extends State<GalleryScreen> {
  static const _filters = [
    ('liked', '好評'),
    ('super', '超好評'),
    ('passed', '見送り'),
    ('unrated', '未評価'),
    ('all', 'すべて'),
    ('archived', 'アーカイブ'),
  ];
  static const _sorts = [
    ('score', '点数順'),
    ('recent', '最近評価した順'),
    ('folder', 'フォルダ順'),
    ('name', '名前順'),
    ('random', 'ランダム'),
  ];
  static const _folderSorts = [('rank', '順位'), ('count', '枚数'), ('name', '名前')];

  String _filter = 'liked', _sort = 'score', _folderSort = 'rank';
  bool _byFolder = false; // the "フォルダ別" chip
  final List<CardItem> _items = [];
  List<FolderCard> _folders = const [];
  int _total = 0, _seed = 0;
  bool _loading = false;
  int _shownVersion = -1;
  int _req = 0; // counts reloads: an answer for an earlier filter or sort is dropped

  // The core's folder view has no archive (archived images belong to no card).
  bool get _folderView => _byFolder && _filter != 'archived';

  @override
  void initState() {
    super.initState();
    widget.state.addListener(_onState);
    _reload();
  }

  @override
  void dispose() {
    widget.state.removeListener(_onState);
    super.dispose();
  }

  // Showing the tab again (or closing a page on top) changes these dependencies.
  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _onState();
  }

  // New ratings change the lists; reload now if the gallery is on screen, else when it is shown next
  // (cheap: one page). Its tab is shown when Visibility says so (IndexedStack) and no page covers it.
  void _onState() {
    final v = widget.state.state['version'] as int? ?? 0;
    if (v != _shownVersion && mounted && Visibility.of(context) && TickerMode.valuesOf(context).enabled) _reload();
  }

  Future<void> _reload() async {
    _req++;
    _items.clear();
    _folders = const [];
    _total = 0;
    _loading = false;
    _shownVersion = widget.state.state['version'] as int? ?? 0;
    await (_folderView ? _loadFolders() : _more());
  }

  Future<void> _more() async {
    if (_loading || (_total > 0 && _items.length >= _total)) return;
    final req = _req;
    _loading = true;
    try {
      final r = await Api.get('/api/items', {
        'filter': _filter,
        'sort': _sort,
        'offset': '${_items.length}',
        'limit': '180',
        'seed': '$_seed',
      }) as Map;
      if (!mounted || req != _req) return;
      setState(() {
        _total = r['total'] as int;
        _items.addAll([for (final j in r['items'] as List) CardItem(Map<String, dynamic>.from(j as Map))]);
      });
    } catch (_) {
      // not ready yet
    } finally {
      if (req == _req) _loading = false;
    }
  }

  Future<void> _loadFolders() async {
    final req = _req;
    _loading = true;
    var folders = const <FolderCard>[];
    try {
      final r = await Api.get('/api/folderCards', {'filter': _filter, 'sort': _folderSort, 'scope': ''}) as Map;
      folders = [for (final f in r['folders'] as List) FolderCard(Map<String, dynamic>.from(f as Map))];
    } catch (_) {
      // not ready yet
    }
    if (!mounted || req != _req) return;
    setState(() {
      _folders = folders;
      _loading = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    final wide = MediaQuery.sizeOf(context).width >= 760;
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    final byFolder = _folderView;
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 10, 16, 0),
          child: Row(
            children: [
              const Text('ギャラリー', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700)),
              const Spacer(),
              Text(byFolder ? '${_folders.length}フォルダ' : '$_total枚', style: TextStyle(color: muted)),
              const SizedBox(width: 8),
              if (!wide)
                IconButton(
                  tooltip: 'フォルダ別',
                  isSelected: byFolder,
                  icon: const Icon(Icons.folder_outlined),
                  selectedIcon: const Icon(Icons.folder),
                  onPressed: _filter == 'archived'
                      ? null
                      : () {
                          setState(() => _byFolder = !_byFolder);
                          _reload();
                        },
                ),
              PopupMenuButton<String>(
                tooltip: '並び順',
                icon: const Icon(Icons.sort),
                initialValue: byFolder ? _folderSort : _sort,
                onSelected: (v) {
                  setState(() {
                    if (byFolder) {
                      _folderSort = v;
                    } else {
                      _sort = v;
                      _seed++;
                    }
                  });
                  _reload();
                },
                itemBuilder: (_) => [
                  for (final s in byFolder ? _folderSorts : _sorts) PopupMenuItem(value: s.$1, child: Text(s.$2)),
                ],
              ),
            ],
          ),
        ),
        SizedBox(
          height: 48,
          child: ListView(
            scrollDirection: Axis.horizontal,
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
            children: [
              for (final f in _filters)
                Padding(
                  padding: const EdgeInsets.only(right: 6),
                  child: ChoiceChip(
                    label: Text(f.$2),
                    selected: _filter == f.$1,
                    onSelected: (_) {
                      setState(() => _filter = f.$1);
                      _reload();
                    },
                  ),
                ),
              if (wide) const VerticalDivider(width: 14, indent: 4, endIndent: 4),
              if (wide)
                FilterChip(
                avatar: const Icon(Icons.folder_outlined, size: 18),
                showCheckmark: false,
                label: const Text('フォルダ別'),
                selected: byFolder,
                onSelected: _filter == 'archived'
                    ? null
                    : (on) {
                        setState(() => _byFolder = on);
                        _reload();
                      },
              ),
            ],
          ),
        ),
        Expanded(
          child: byFolder
              ? _folders.isEmpty
                    ? Center(
                        child: Text(_loading ? '読み込み中…' : '該当するフォルダがありません', style: TextStyle(color: muted)),
                      )
                    : GridView.builder(
                        padding: const EdgeInsets.all(6),
                        gridDelegate: SliverGridDelegateWithMaxCrossAxisExtent(
                          maxCrossAxisExtent: wide ? 240 : 170,
                          mainAxisSpacing: 6,
                          crossAxisSpacing: 6,
                          childAspectRatio: .8,
                        ),
                        itemCount: _folders.length,
                        itemBuilder: (context, i) => FolderTile(folder: _folders[i], onTap: () => _openFolder(_folders[i])),
                      )
              : _items.isEmpty
              ? Center(
                  child: Text(_loading ? '読み込み中…' : '該当する画像がありません', style: TextStyle(color: muted)),
                )
              : NotificationListener<ScrollNotification>(
                  onNotification: (n) {
                    if (n.metrics.extentAfter < 800) _more();
                    return false;
                  },
                  child: GridView.builder(
                    padding: const EdgeInsets.all(6),
                    gridDelegate: SliverGridDelegateWithMaxCrossAxisExtent(
                      maxCrossAxisExtent: wide ? 220 : 130,
                      mainAxisSpacing: 6,
                      crossAxisSpacing: 6,
                    ),
                    itemCount: _items.length,
                    itemBuilder: (context, i) => GalleryTile(item: _items[i], onTap: () => Viewer.open(context, _items, i)),
                  ),
                ),
        ),
      ],
    );
  }

  void _openFolder(FolderCard f) => FolderImagesPage.open(context, title: f.name, scope: f.scope, filter: _filter);
}

/// A folder of the folder view: its best image, its name and how many of its images match.
class FolderTile extends StatelessWidget {
  const FolderTile({super.key, required this.folder, required this.onTap});
  final FolderCard folder;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final dpr = MediaQuery.devicePixelRatioOf(context);
    final medal = medalColor(folder.rank);
    return GestureDetector(
      onTap: onTap,
      child: ClipRRect(
        borderRadius: BorderRadius.circular(12),
        child: Stack(
          fit: StackFit.expand,
          children: [
            Photo(path: folder.coverPath, fit: BoxFit.cover, cacheWidth: (260 * dpr).round()),
            Positioned(
              left: 0,
              right: 0,
              bottom: 0,
              child: DecoratedBox(
                decoration: const BoxDecoration(
                  gradient: LinearGradient(
                    begin: Alignment.topCenter,
                    end: Alignment.bottomCenter,
                    colors: [Color(0x00000000), Color(0xCC000000)],
                  ),
                ),
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(8, 22, 8, 7),
                  child: Row(
                    children: [
                      Expanded(
                        child: Text(
                          folder.name,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w700, color: Colors.white),
                        ),
                      ),
                      const SizedBox(width: 6),
                      Text('${folder.count}枚', style: const TextStyle(fontSize: 12, color: Colors.white70)),
                    ],
                  ),
                ),
              ),
            ),
            // a photo folder's own tile (depth 0) may carry its rank among the photo folders instead
            if (folder.rank > 0 && folder.depth > 0)
              Positioned(
                left: 6,
                top: 6,
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                  decoration: BoxDecoration(
                    color: medal ?? Colors.black.withValues(alpha: .55),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Text(
                    '${folder.rank}位',
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w800,
                      color: medal == null ? Colors.white : const Color(0xFF15171B),
                    ),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

/// An image of a grid (the gallery, a folder's page): the picture and its latest rating.
class GalleryTile extends StatelessWidget {
  const GalleryTile({super.key, required this.item, required this.onTap});
  final CardItem item;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final dpr = MediaQuery.devicePixelRatioOf(context);
    return GestureDetector(
      onTap: onTap,
      child: ClipRRect(
        borderRadius: BorderRadius.circular(10),
        child: Stack(
          fit: StackFit.expand,
          children: [
            Photo(path: item.path, fit: BoxFit.cover, cacheWidth: (240 * dpr).round()),
            if (item.rated)
              Positioned(
                right: 6,
                bottom: 6,
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 2),
                  decoration: BoxDecoration(
                    color: Colors.black.withValues(alpha: .55),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(
                        item.last == 'S'
                            ? Icons.star
                            : item.last == 'D'
                            ? Icons.close
                            : Icons.favorite,
                        size: 13,
                        color: item.last == 'S'
                            ? Rate.superLike
                            : item.last == 'D'
                            ? Rate.pass
                            : Rate.like,
                      ),
                      const SizedBox(width: 3),
                      Text('${item.points.round()}', style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w600)),
                    ],
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

/// Full screen: swipe sideways for the next picture, pinch or double-tap to zoom, tap for info.
class Viewer extends StatefulWidget {
  const Viewer({super.key, required this.items, required this.start});
  final List<CardItem> items;
  final int start;

  static Future<void> open(BuildContext context, List<CardItem> items, int start) => Navigator.of(context).push(
    PageRouteBuilder<void>(
      opaque: true,
      transitionDuration: const Duration(milliseconds: 180),
      pageBuilder: (_, _, _) => Viewer(items: items, start: start),
      transitionsBuilder: (_, a, _, child) => FadeTransition(opacity: a, child: child),
    ),
  );

  @override
  State<Viewer> createState() => _ViewerState();
}

class _ViewerState extends State<Viewer> {
  late final PageController _pages = PageController(initialPage: widget.start);
  late int _index = widget.start;
  bool _chrome = true;

  @override
  Widget build(BuildContext context) {
    final item = widget.items[_index];
    return Scaffold(
      backgroundColor: Colors.black,
      body: Stack(
        children: [
          GestureDetector(
            onTap: () => setState(() => _chrome = !_chrome),
            child: PageView.builder(
              controller: _pages,
              itemCount: widget.items.length,
              onPageChanged: (i) => setState(() => _index = i),
              itemBuilder: (_, i) => InteractiveViewer(
                maxScale: 5,
                child: SizedBox.expand(child: Photo(path: widget.items[i].path)),
              ),
            ),
          ),
          AnimatedOpacity(
            opacity: _chrome ? 1 : 0,
            duration: const Duration(milliseconds: 180),
            child: SafeArea(
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  IconButton.filledTonal(
                    onPressed: () => Navigator.pop(context),
                    icon: const Icon(Icons.close),
                    tooltip: '閉じる',
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Padding(
                      padding: const EdgeInsets.only(top: 6),
                      child: Text(
                        '${item.place.isEmpty ? '写真フォルダの直下' : item.place}\n${item.name}',
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(shadows: [Shadow(blurRadius: 6)], fontSize: 13),
                      ),
                    ),
                  ),
                  Padding(
                    padding: const EdgeInsets.all(10),
                    child: Text(
                      '${_index + 1} / ${widget.items.length}',
                      style: const TextStyle(color: Colors.white70),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
