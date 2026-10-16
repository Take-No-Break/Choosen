// "範囲": which folders to swipe. A searchable list of the photo folders' folders with check boxes;
// nothing checked = everything. Returns the scope tokens the core reads (/api/next?scope=a|b): a
// folder's path, and with several photo folders each photo folder itself ("/*" = the first one).
import 'package:flutter/material.dart';

import '../core/api.dart';
import '../core/models.dart';

class ScopeChoice {
  const ScopeChoice(this.tokens, this.label);
  final List<String> tokens;
  final String label; // for the chip: "すべて", or the folder names
}

class ScopePicker extends StatefulWidget {
  const ScopePicker({super.key, required this.current});
  final List<String> current;

  static Future<ScopeChoice?> show(BuildContext context, List<String> current) =>
      showDialog<ScopeChoice>(context: context, builder: (_) => ScopePicker(current: current));

  @override
  State<ScopePicker> createState() => _ScopePickerState();
}

class _Entry {
  const _Entry(this.folder, this.token, this.indent, this.top);
  final FolderRow folder;
  final String token;
  final int indent;
  final bool top;
}

class _ScopePickerState extends State<ScopePicker> {
  static const firstRoot = '/*';
  List<_Entry>? _entries;
  late final Set<String> _sel = {...widget.current};
  String _query = '';

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    List<FolderRow> folders = const [];
    try {
      final r = await Api.get('/api/folders') as Map;
      folders = [for (final f in r['folders'] as List) FolderRow(Map<String, dynamic>.from(f as Map))];
    } catch (_) {}
    if (!mounted) return;
    setState(() => _entries = _tree(folders));
  }

  /// The folders as a tree, in the order of the list: with several photo folders, each one on top
  /// with its folders below it.
  static List<_Entry> _tree(List<FolderRow> folders) {
    String parentOf(String p) => p.contains('/') ? p.substring(0, p.lastIndexOf('/')) : '';
    final kids = <String, List<FolderRow>>{};
    for (final f in folders) {
      if (f.depth > 0) kids.putIfAbsent(parentOf(f.path), () => []).add(f);
    }
    for (final list in kids.values) {
      list.sort((a, b) => a.name.compareTo(b.name));
    }
    final tops = folders.where((f) => f.depth == 0).toList();
    final multi = tops.length > 1;
    final out = <_Entry>[];
    void walk(String p) {
      for (final f in kids[p] ?? const <FolderRow>[]) {
        out.add(_Entry(f, f.path, f.depth - 1 + (multi ? 1 : 0), false));
        walk(f.path);
      }
    }

    if (multi) {
      for (final t in tops) {
        out.add(_Entry(t, t.path.isEmpty ? firstRoot : t.path, 0, true));
        walk(t.path);
      }
    } else {
      walk('');
    }
    return out;
  }

  ScopeChoice _choice() {
    final names = [
      for (final e in _entries ?? const <_Entry>[])
        if (_sel.contains(e.token)) e.folder.name,
    ];
    final label = names.isEmpty
        ? 'すべて'
        : names.length <= 2
        ? names.join('、')
        : '${names.take(2).join('、')} ほか${names.length - 2}件';
    return ScopeChoice([..._sel], label);
  }

  @override
  Widget build(BuildContext context) {
    final entries = _entries;
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    final q = _query.toLowerCase();
    final shown = entries == null || q.isEmpty
        ? entries
        : entries.where((e) => e.folder.name.toLowerCase().contains(q)).toList();
    return AlertDialog(
      title: Row(
        children: [
          const Text('範囲'),
          const Spacer(),
          Text(
            _sel.isEmpty ? 'すべて' : '${_sel.length} 個',
            style: TextStyle(fontSize: 13, color: muted),
          ),
        ],
      ),
      content: SizedBox(
        width: 520,
        height: 480,
        child: Column(
          children: [
            TextField(
              autofocus: true,
              decoration: const InputDecoration(prefixIcon: Icon(Icons.search), hintText: 'フォルダを探す', isDense: true),
              onChanged: (v) => setState(() => _query = v),
            ),
            const SizedBox(height: 8),
            Expanded(
              child: shown == null
                  ? const Center(child: CircularProgressIndicator())
                  : ListView.builder(
                      itemCount: shown.length,
                      itemBuilder: (_, i) {
                        final e = shown[i];
                        return CheckboxListTile(
                          dense: true,
                          controlAffinity: ListTileControlAffinity.leading,
                          contentPadding: EdgeInsets.only(left: q.isEmpty ? e.indent * 18.0 : 0),
                          value: _sel.contains(e.token),
                          title: Text(
                            e.folder.name,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(fontWeight: e.top ? FontWeight.w700 : FontWeight.normal),
                          ),
                          secondary: Text('${e.folder.total}枚 · ♥${e.folder.liked}', style: TextStyle(fontSize: 12, color: muted)),
                          onChanged: (v) => setState(() => v == true ? _sel.add(e.token) : _sel.remove(e.token)),
                        );
                      },
                    ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context, const ScopeChoice([], 'すべて')), child: const Text('すべて')),
        FilledButton(onPressed: () => Navigator.pop(context, _choice()), child: const Text('決定')),
      ],
    );
  }
}
