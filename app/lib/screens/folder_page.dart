// One folder's images, best first: opened from the gallery's folder view and from the ranking. A tap
// shows them full screen.
import 'package:flutter/material.dart';

import '../core/api.dart';
import '../core/models.dart';
import '../ui/theme.dart';
import 'gallery_screen.dart';

/// Gold, silver and bronze for the three best folders (the ranking and the gallery's folder tiles).
Color? medalColor(int rank) => switch (rank) {
  1 => Rate.superLike,
  2 => const Color(0xFFC0C6D0),
  3 => const Color(0xFFCD8B5A),
  _ => null,
};

class FolderImagesPage extends StatefulWidget {
  const FolderImagesPage({super.key, required this.title, required this.scope, required this.filter});
  final String title;
  final String scope; // the folder as a scope token (folderScope)
  final String filter; // which images, as in the gallery: 'liked', 'all', ...

  static Future<void> open(BuildContext context, {required String title, required String scope, required String filter}) =>
      Navigator.of(context).push(
        MaterialPageRoute<void>(builder: (_) => FolderImagesPage(title: title, scope: scope, filter: filter)),
      );

  @override
  State<FolderImagesPage> createState() => _FolderImagesPageState();
}

class _FolderImagesPageState extends State<FolderImagesPage> {
  final List<CardItem> _items = [];
  int _total = 0;
  bool _loading = false, _done = false;

  @override
  void initState() {
    super.initState();
    _more();
  }

  Future<void> _more() async {
    if (_loading || _done) return;
    _loading = true;
    List<CardItem>? page;
    var total = _total;
    try {
      final r = await Api.get('/api/items', {
        'filter': widget.filter,
        'scope': widget.scope,
        'sort': 'score',
        'offset': '${_items.length}',
        'limit': '180',
      }) as Map;
      total = r['total'] as int;
      page = [for (final j in r['items'] as List) CardItem(Map<String, dynamic>.from(j as Map))];
    } catch (_) {
      // not ready yet: scrolling asks again
    }
    if (!mounted) return;
    setState(() {
      _loading = false;
      if (page == null) return;
      _total = total;
      _items.addAll(page);
      _done = page.isEmpty || _items.length >= _total;
    });
  }

  @override
  Widget build(BuildContext context) {
    final wide = MediaQuery.sizeOf(context).width >= 760;
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(4, 6, 16, 2),
              child: Row(
                children: [
                  IconButton(
                    onPressed: () => Navigator.pop(context),
                    icon: const Icon(Icons.arrow_back),
                    tooltip: '戻る',
                  ),
                  const SizedBox(width: 4),
                  Expanded(
                    child: Text(
                      widget.title,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w700),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Text('$_total枚', style: TextStyle(color: muted)),
                ],
              ),
            ),
            Expanded(
              child: _items.isEmpty
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
                        itemBuilder: (context, i) =>
                            GalleryTile(item: _items[i], onTap: () => Viewer.open(context, _items, i)),
                      ),
                    ),
            ),
          ],
        ),
      ),
    );
  }
}
