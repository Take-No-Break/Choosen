// Settings: the photo folders first, then how swiping works, then export (書き出し).
import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';

import '../core/api.dart';
import '../core/models.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key, required this.state, required this.onFolderReady});
  final AppState state;
  final VoidCallback onFolderReady;

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _Folder {
  _Folder(this.path, this.maxDepth);
  String path;
  int maxDepth; // -1 all subfolders, 0 none
}

class _SettingsScreenState extends State<SettingsScreen> {
  List<_Folder>? _folders; // edited copy; null until the state is known
  bool _saving = false;

  // Export: copies or moves the images of one kind into a new folder (src/core_export.cpp).
  String _exportWhat = 'archived';
  bool _exportMove = false;
  bool _exportBusy = false; // from the count to the start: one export dialog at a time

  // Moving favourites out would change the folder ranking: only archived and passed images move.
  bool get _canMove => _exportWhat == 'archived' || _exportWhat == 'passed';
  String? _exportTo;
  Map? _exportJob; // the running job, or the last one this screen watched
  Timer? _exportPoll;

  @override
  void initState() {
    super.initState();
    widget.state.addListener(_fromState);
    _fromState();
    _watchExport(); // a job may still be running from before this screen opened
  }

  @override
  void dispose() {
    _exportPoll?.cancel();
    widget.state.removeListener(_fromState);
    super.dispose();
  }

  void _fromState() {
    final s = widget.state;
    if (!s.ready || _folders != null) return;
    final depths = s.maxDepths;
    setState(() {
      _folders = [
        if (s.root.isNotEmpty) _Folder(s.root, depths.isNotEmpty ? depths[0] : -1),
        for (var i = 0; i < s.moreRoots.length; i++)
          _Folder(s.moreRoots[i], i + 1 < depths.length ? depths[i + 1] : -1),
      ];
    });
  }

  Future<void> _save() async {
    final f = _folders ?? [];
    final firstFolder = !widget.state.hasRoot && f.isNotEmpty;
    setState(() => _saving = true);
    final err = await widget.state.setConfig({
      'root': f.isEmpty ? '' : f.first.path,
      'more_roots': f.skip(1).map((e) => e.path).join('|'),
      'max_depths': f.map((e) => e.maxDepth).join(','),
    });
    if (!mounted) return;
    setState(() => _saving = false);
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(err ?? '保存しました。フォルダを読み込んでいます')));
    if (err == null && firstFolder) widget.onFolderReady();
  }

  Future<void> _add() async {
    final path = await FolderBrowser.show(context);
    if (path == null || path.isEmpty) return;
    setState(() => _folders!.add(_Folder(path, -1)));
    await _save();
  }

  Future<void> _pickExportFolder() async {
    final path = await FolderBrowser.show(context, title: '保存先を選ぶ');
    if (path == null || path.isEmpty || !mounted) return;
    setState(() => _exportTo = path);
  }

  Future<void> _startExport() async {
    final to = _exportTo;
    if (to == null) return;
    if (_exportBusy) return;
    final what = _exportWhat, move = _exportMove && _canMove, mode = move ? 'move' : 'copy';
    final messenger = ScaffoldMessenger.of(context);
    setState(() => _exportBusy = true);
    try {
      final r = await Api.get('/api/export', {'what': what, 'mode': mode}) as Map;
      final n = r['count'] as int? ?? 0;
      if (!mounted) return;
      if (n == 0) {
        messenger.showSnackBar(const SnackBar(content: Text('該当する画像がありません')));
        return;
      }
      if (await _confirmExport(n, to, move, what == 'passed') != true || !mounted) return;
      final res = await Api.post('/api/export', {'what': what, 'to': to, 'mode': mode}) as Map;
      if (!mounted) return;
      if (res['ok'] != true) {
        messenger.showSnackBar(SnackBar(content: Text(res['error'] as String? ?? '書き出せませんでした')));
        return;
      }
      _exportPoll ??= Timer.periodic(const Duration(milliseconds: 500), (_) => _watchExport());
      await _watchExport();
    } catch (_) {
      messenger.showSnackBar(const SnackBar(content: Text('書き出せませんでした')));
    } finally {
      if (mounted) setState(() => _exportBusy = false);
    }
  }

  Future<bool?> _confirmExport(int n, String to, bool move, bool passes) {
    final cs = Theme.of(context).colorScheme;
    return showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        icon: Icon(move ? Icons.drive_file_move_outline : Icons.content_copy),
        content: Text(
          !move
              ? '$n 枚を「$to」へコピーします。'
              : passes
              ? '$n 枚を写真フォルダから移動します。元の場所からはなくなり、ランキングからも外れます。'
              : '$n 枚を写真フォルダから移動します。元の場所からはなくなります。',
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('キャンセル')),
          FilledButton(
            style: move ? FilledButton.styleFrom(backgroundColor: cs.error, foregroundColor: cs.onError) : null,
            onPressed: () => Navigator.pop(c, true),
            child: Text(move ? '移動する' : 'コピーする'),
          ),
        ],
      ),
    );
  }

  /// Reads the job's progress; polled every 0.5 s while it runs. A job that had already finished
  /// before this screen opened is not shown.
  Future<void> _watchExport() async {
    final Map job;
    try {
      job = await Api.get('/api/export') as Map;
    } catch (_) {
      return;
    }
    if (!mounted) return;
    final running = job['running'] == true;
    if (!running && _exportPoll == null) return;
    setState(() => _exportJob = job);
    if (running) {
      _exportPoll ??= Timer.periodic(const Duration(milliseconds: 500), (_) => _watchExport());
    } else {
      _exportPoll?.cancel();
      _exportPoll = null;
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.state;
    final folders = _folders;
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    final narrow = MediaQuery.sizeOf(context).width < 600; // a phone-sized window: short labels
    if (folders == null) return const Center(child: CircularProgressIndicator());
    return Align(
      alignment: Alignment.topLeft,
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 720),
        child: ListView(
          padding: const EdgeInsets.fromLTRB(16, 12, 16, 32),
          children: [
            const Text('設定', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700)),
            const SizedBox(height: 12),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(10),
                color: Theme.of(context).colorScheme.surfaceContainerHighest.withValues(alpha: .5),
              ),
              child: Row(
                children: [
                  Icon(Icons.verified_user_outlined, size: 20, color: muted),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      '写真フォルダの中身を変更・削除することはありません（「書き出し」の移動を除く）。評価はアプリの中だけに記録されます。',
                      style: TextStyle(color: muted, fontSize: 12),
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 18),
            _Section(
              title: '写真フォルダ',
              children: [
                if (folders.isEmpty)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 8),
                    child: Text('評価したい写真のフォルダを選んでください。', style: TextStyle(color: muted)),
                  ),
                for (final f in folders)
                  Card(
                    margin: const EdgeInsets.only(bottom: 8),
                    child: Padding(
                      padding: const EdgeInsets.fromLTRB(14, 8, 6, 4),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Row(
                            children: [
                              const Icon(Icons.folder, size: 20),
                              const SizedBox(width: 8),
                              Expanded(child: Text(f.path, style: const TextStyle(fontSize: 13))),
                              IconButton(
                                tooltip: 'このフォルダを外す（評価は残ります）',
                                icon: const Icon(Icons.close, size: 20),
                                onPressed: () {
                                  setState(() => folders.remove(f));
                                  _save();
                                },
                              ),
                            ],
                          ),
                          SwitchListTile(
                            dense: true,
                            contentPadding: EdgeInsets.zero,
                            title: const Text('サブフォルダも読む'),
                            value: f.maxDepth != 0,
                            onChanged: (v) {
                              setState(() => f.maxDepth = v ? -1 : 0);
                              _save();
                            },
                          ),
                        ],
                      ),
                    ),
                  ),
                Row(
                  children: [
                    FilledButton.icon(
                      onPressed: _saving ? null : _add,
                      icon: const Icon(Icons.add),
                      label: const Text('写真フォルダを追加'),
                    ),
                    const SizedBox(width: 12),
                    if (s.scanning) Text('読み込み中…', style: TextStyle(color: muted)),
                    if (!s.scanning && s.hasRoot) Text('${s.items}枚', style: TextStyle(color: muted)),
                  ],
                ),
                const SizedBox(height: 8),
                Text('外したフォルダの評価は残り、もう一度追加すると戻ります。', style: TextStyle(color: muted, fontSize: 12)),
              ],
            ),
            if (Platform.isWindows)
              _Section(
                title: 'マウス',
                children: [
                  SegmentedButton<int>(
                    segments: [
                      ButtonSegment(value: 1, label: Text(narrow ? '1' : '1  つかんで投げる')),
                      ButtonSegment(value: 2, label: Text(narrow ? '2' : '2  ホイールとクリック')),
                      ButtonSegment(value: 3, label: Text(narrow ? '3' : '3  フリック')),
                      if (s.customMouse.isNotEmpty) ButtonSegment(value: 4, label: Text(narrow ? '自分' : '自分の割り当て')),
                    ],
                    selected: {s.customMouse.isEmpty ? s.mouseMode.clamp(1, 3) : s.mouseMode.clamp(1, 4)},
                    onSelectionChanged: (v) => s.setConfig({'mouse_mode': '${v.first}'}),
                  ),
                  const SizedBox(height: 6),
                  Text(switch (s.mouseMode) {
                    1 => '左か右ボタンでつかんで投げる · ホイール ↑ 戻る · ↓ 進む',
                    2 => 'ホイール ↑ 好評 · ↓ 見送り · 左クリック 超好評 · 右クリック 戻る · ホイールを押す 見送り · サイドボタン 戻る アーカイブ',
                    3 => '右クリックでフリック開始 / 終了（Esc でも終了） · はじいて評価 · 左クリック 戻る',
                    _ => 'Version 1 で作った割り当て',
                  }, style: TextStyle(color: muted, fontSize: 12)),
                ],
              ),
            if (Platform.isWindows)
              _Section(
                title: 'キーボード',
                children: [
                  SegmentedButton<int>(
                    segments: [
                      ButtonSegment(value: 1, label: Text(narrow ? 'A' : 'A  ← → ↑')),
                      ButtonSegment(value: 2, label: Text(narrow ? 'B' : 'B  ← → 長押し')),
                      ButtonSegment(value: 3, label: Text(narrow ? 'C' : 'C  W A D')),
                    ],
                    selected: {s.keyMode.clamp(1, 3)},
                    onSelectionChanged: (v) => s.setConfig({'key_mode': '${v.first}'}),
                  ),
                  const SizedBox(height: 6),
                  Text(switch (s.keyMode) {
                    1 => '→ 好評 · ← 見送り · ↑ 超好評 · ↓ 戻る · Enter / Delete アーカイブ',
                    2 => '→ 好評（長押しで超好評） · ← 見送り · ↓ 戻る · ↑ / Delete アーカイブ',
                    _ => 'D 好評 · A 見送り · W 超好評 · 左 Shift 戻る · E / Delete アーカイブ',
                  }, style: TextStyle(color: muted, fontSize: 12)),
                ],
              ),
            _Section(
              title: '再評価の割合',
              children: [
                DropdownButton<double>(
                  value: _nearestShare(s.reviewShare),
                  items: const [
                    DropdownMenuItem(value: 0, child: Text('なし — 新しい画像だけ')),
                    DropdownMenuItem(value: .05, child: Text('20 枚に 1 枚')),
                    DropdownMenuItem(value: .067, child: Text('15 枚に 1 枚')),
                    DropdownMenuItem(value: .1, child: Text('10 枚に 1 枚（標準）')),
                    DropdownMenuItem(value: .2, child: Text('5 枚に 1 枚')),
                    DropdownMenuItem(value: .35, child: Text('3 枚に 1 枚')),
                    DropdownMenuItem(value: .5, child: Text('2 枚に 1 枚')),
                    DropdownMenuItem(value: 1, child: Text('再評価のみ')),
                  ],
                  onChanged: (v) => s.setConfig({'review_share': '$v'}),
                ),
              ],
            ),
            _Section(
              title: '作品ごとにまとめる',
              children: [
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('Pixiv の複数ページ作品を 1 枚のカードにまとめる'),
                  value: s.state['groupWorks'] == true,
                  onChanged: (v) => s.setConfig({'group_works': v ? '1' : '0'}),
                ),
              ],
            ),
            _exportSection(muted),
          ],
        ),
      ),
    );
  }

  Widget _exportSection(Color muted) {
    final job = _exportJob;
    final running = job?['running'] == true;
    final done = job?['done'] as int? ?? 0;
    final failed = job?['failed'] as int? ?? 0;
    final total = job?['total'] as int? ?? 0;
    final folder = job?['folder'] as String? ?? '';
    return _Section(
      title: '書き出し',
      children: [
        Wrap(
          spacing: 12,
          runSpacing: 8,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: [
            DropdownButton<String>(
              value: _exportWhat,
              items: const [
                DropdownMenuItem(value: 'archived', child: Text('アーカイブ')),
                DropdownMenuItem(value: 'passed', child: Text('見送り')),
                DropdownMenuItem(value: 'liked', child: Text('好評と超好評')),
                DropdownMenuItem(value: 'likeOnly', child: Text('好評')),
                DropdownMenuItem(value: 'super', child: Text('超好評')),
              ],
              onChanged: running
                  ? null
                  : (v) => setState(() {
                      _exportWhat = v ?? _exportWhat;
                      if (!_canMove) _exportMove = false;
                    }),
            ),
            SegmentedButton<bool>(
              segments: const [
                ButtonSegment(value: false, icon: Icon(Icons.content_copy), label: Text('コピー')),
                ButtonSegment(value: true, icon: Icon(Icons.drive_file_move_outline), label: Text('移動')),
              ].map((s) => s.value && !_canMove ? ButtonSegment(value: true, icon: s.icon, label: s.label, enabled: false) : s).toList(),
              selected: {_exportMove && _canMove},
              onSelectionChanged: running ? null : (v) => setState(() => _exportMove = v.first),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            OutlinedButton.icon(
              onPressed: running ? null : _pickExportFolder,
              icon: const Icon(Icons.folder_open),
              label: const Text('保存先'),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: Text(_exportTo ?? '', style: const TextStyle(fontSize: 13), overflow: TextOverflow.ellipsis),
            ),
            const SizedBox(width: 10),
            FilledButton.icon(
              onPressed: running || _exportBusy || _exportTo == null ? null : _startExport,
              icon: const Icon(Icons.output),
              label: const Text('書き出す'),
            ),
          ],
        ),
        if (job != null && running) ...[
          const SizedBox(height: 10),
          LinearProgressIndicator(value: total > 0 ? (done + failed) / total : null),
          const SizedBox(height: 4),
          Text('${done + failed} / $total', style: TextStyle(color: muted, fontSize: 12)),
        ],
        if (job != null && !running) ...[
          const SizedBox(height: 6),
          Row(
            children: [
              Icon(failed > 0 ? Icons.error_outline : Icons.check_circle_outline, size: 20, color: muted),
              const SizedBox(width: 8),
              Expanded(child: Text(failed > 0 ? '$done 枚を書き出しました（失敗 $failed）' : '$done 枚を書き出しました')),
              if (Platform.isWindows && folder.isNotEmpty)
                TextButton.icon(
                  onPressed: () => Process.run('explorer.exe', [folder]),
                  icon: const Icon(Icons.folder_open),
                  label: const Text('フォルダを開く'),
                ),
            ],
          ),
        ],
      ],
    );
  }

  static double _nearestShare(double v) =>
      const [0.0, .05, .067, .1, .2, .35, .5, 1.0].reduce((a, b) => (b - v).abs() < (a - v).abs() ? b : a);
}

class _Section extends StatelessWidget {
  const _Section({required this.title, required this.children});
  final String title;
  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 22),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
          const SizedBox(height: 8),
          ...children,
        ],
      ),
    );
  }
}

/// Chooses a folder by walking the disk (Windows: the drives; Android: the phone's storage and SD
/// cards). Any folder can be chosen: a photo folder (all its subfolders are read) or where to export.
class FolderBrowser extends StatefulWidget {
  const FolderBrowser({super.key, this.title = '写真フォルダを選ぶ'});
  final String title;

  static Future<String?> show(BuildContext context, {String title = '写真フォルダを選ぶ'}) => showModalBottomSheet<String>(
    context: context,
    isScrollControlled: true,
    useSafeArea: true,
    builder: (_) => FolderBrowser(title: title),
  );

  @override
  State<FolderBrowser> createState() => _FolderBrowserState();
}

class _FolderBrowserState extends State<FolderBrowser> {
  String _path = '';
  List<(String, String)> _dirs = [];

  @override
  void initState() {
    super.initState();
    _open('');
  }

  List<(String, String)> _starts() {
    if (Platform.isWindows) {
      return [
        for (var c = 67; c <= 90; c++)
          if (Directory('${String.fromCharCode(c)}:\\').existsSync())
            ('${String.fromCharCode(c)}: ドライブ', '${String.fromCharCode(c)}:\\'),
      ];
    }
    return [
      ('内部ストレージ', '/storage/emulated/0'),
      for (final v in Directory('/storage').listSync().whereType<Directory>())
        if (RegExp(r'^[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}$').hasMatch(v.path.split('/').last)) ('SD カード', v.path),
    ];
  }

  void _open(String path) {
    List<(String, String)> dirs;
    if (path.isEmpty) {
      dirs = _starts();
    } else {
      try {
        dirs = [
          for (final d in Directory(path).listSync(followLinks: false).whereType<Directory>())
            if (!d.path.split(RegExp(r'[\\/]')).last.startsWith('.') &&
                !d.path.split(RegExp(r'[\\/]')).last.startsWith(r'$') &&
                !(Platform.isAndroid && d.path == '/storage/emulated/0/Android'))
              (d.path.split(RegExp(r'[\\/]')).last, d.path),
        ]..sort((a, b) => a.$1.toLowerCase().compareTo(b.$1.toLowerCase()));
      } catch (_) {
        dirs = [];
      }
    }
    setState(() {
      _path = path;
      _dirs = dirs;
    });
  }

  String _parent(String p) {
    final i = p.replaceAll('\\', '/').lastIndexOf('/');
    if (i <= 0) return '';
    final parent = p.substring(0, i);
    if (Platform.isWindows && parent.length == 2) return '$parent\\';
    return _starts().any((s) => s.$2 == p) ? '' : parent;
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: MediaQuery.sizeOf(context).height * .8,
      child: Column(
        children: [
          ListTile(
            leading: _path.isEmpty
                ? null
                : IconButton(
                    icon: const Icon(Icons.arrow_upward),
                    onPressed: () => _open(_parent(_path)),
                    tooltip: '上へ',
                  ),
            title: Text(widget.title, style: const TextStyle(fontWeight: FontWeight.w700)),
            subtitle: Text(_path.isEmpty ? '場所を選んでください' : _path, maxLines: 2, overflow: TextOverflow.ellipsis),
          ),
          const Divider(height: 1),
          Expanded(
            child: _dirs.isEmpty
                ? const Center(child: Text('サブフォルダはありません'))
                : ListView.builder(
                    itemCount: _dirs.length,
                    itemBuilder: (_, i) => ListTile(
                      leading: const Icon(Icons.folder_outlined),
                      title: Text(_dirs[i].$1),
                      onTap: () => _open(_dirs[i].$2),
                    ),
                  ),
          ),
          Padding(
            padding: const EdgeInsets.all(12),
            child: Row(
              children: [
                TextButton(onPressed: () => Navigator.pop(context), child: const Text('キャンセル')),
                const Spacer(),
                FilledButton(
                  onPressed: _path.isEmpty ? null : () => Navigator.pop(context, _path),
                  child: const Text('このフォルダを使う'),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
