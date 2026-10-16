// The folder view's data and the ranking's image previews: which requests go to the core, and when.
import 'dart:async';

import 'package:choosen/core/models.dart';
import 'package:choosen/screens/ranking_screen.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('a folder stands for itself with its subfolders', () {
    expect(folderScope('Artist', 1), 'Artist');
    expect(folderScope('Artist/sub', 2), 'Artist/sub');
  });

  test('a photo folder itself stands for the images directly in it', () {
    expect(folderScope('', 0), '/', reason: '"" alone would mean everything');
    expect(folderScope(r'D:\More', 0), r'D:\More/');
  });

  test('a folder tile reads /api/folderCards', () {
    final f = FolderCard({
      'path': 'A/B',
      'name': 'B',
      'depth': 2,
      'rank': 3,
      'score': 51.2,
      'liked': 4,
      'rated': 6,
      'total': 9,
      'images': 11,
      'count': 4,
      'cover': {'id': 7, 'fp': '0123456789abcdef'},
    });
    expect((f.path, f.name, f.depth, f.rank, f.count), ('A/B', 'B', 2, 3, 4));
    expect((f.coverId, f.coverFp, f.scope), (7, '0123456789abcdef', 'A/B'));
  });

  group('ranking previews', () {
    final asked = <String>[];
    final answers = <String, Completer<List<String>>>{};
    FolderPreviews make() => FolderPreviews(
      fetch: (scope) {
        asked.add(scope);
        return (answers[scope] = Completer<List<String>>()).future;
      },
    );
    const settled = Duration(milliseconds: 200);

    setUp(() {
      asked.clear();
      answers.clear();
    });

    testWidgets('a row that stays loads once and gets its images', (t) async {
      final p = make();
      var changes = 0;
      void row() => changes++;
      p.watch('A', row);
      await t.pump(const Duration(milliseconds: 100));
      expect(asked, isEmpty, reason: 'still settling');
      await t.pump(settled);
      expect(asked, ['A']);
      answers['A']!.complete(['a1', 'a2']);
      await t.pump();
      expect(p.of('A'), ['a1', 'a2']);
      expect(changes, 1);
      p.unwatch('A', row);
      p.watch('A', row); // scrolled back
      await t.pump(settled);
      expect(asked, ['A'], reason: 'kept until the ranking reloads');
      p.dispose();
    });

    testWidgets('rows that only fly past ask for nothing', (t) async {
      final p = make();
      void row() {}
      for (var k = 0; k < 50; k++) {
        p.watch('F$k', row);
        await t.pump(const Duration(milliseconds: 10));
        p.unwatch('F$k', row);
      }
      await t.pump(const Duration(seconds: 1));
      expect(asked, isEmpty);
      p.dispose();
    });

    testWidgets('two at a time, top row first', (t) async {
      final p = make();
      void row() {}
      for (var k = 0; k < 5; k++) {
        p.watch('F$k', row);
      }
      await t.pump(settled);
      expect(asked, ['F0', 'F1']);
      answers['F0']!.complete([]);
      await t.pump();
      expect(asked, ['F0', 'F1', 'F2']);
      answers['F1']!.completeError(Exception('busy'));
      answers['F2']!.complete([]);
      await t.pump();
      expect(asked, ['F0', 'F1', 'F2', 'F3', 'F4']);
      expect(p.of('F1'), isNull, reason: 'a failed row stays grey, not retried at once');
      p.dispose();
    });

    testWidgets('a reload asks again for rows on screen only, keeping their images meanwhile', (t) async {
      final p = make();
      void a() {}
      void b() {}
      p.watch('A', a);
      p.watch('B', b);
      await t.pump(settled);
      answers['A']!.complete(['a1']);
      answers['B']!.complete(['b1']);
      await t.pump();
      p.unwatch('B', b);
      p.refresh();
      await t.pump(settled);
      expect(asked, ['A', 'B', 'A']);
      expect(p.of('A'), ['a1']);
      answers['A']!.complete(['a2']);
      await t.pump();
      expect(p.of('A'), ['a2']);
      p.watch('B', b); // built again after the reload
      await t.pump(settled);
      expect(asked, ['A', 'B', 'A', 'B']);
      p.dispose();
    });

    testWidgets('a reload during a load asks again once it is done', (t) async {
      final p = make();
      void a() {}
      p.watch('A', a);
      await t.pump(settled);
      p.refresh();
      answers['A']!.complete(['old']);
      await t.pump(settled);
      expect(asked, ['A', 'A']);
      p.dispose();
    });
  });
}
