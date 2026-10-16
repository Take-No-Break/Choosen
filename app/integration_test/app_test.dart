// Runs the real app (with the core DLL) on a copy of the test library and swipes through it.
// Start with CHOOSEN_DATA pointing to a throwaway copy of dev\run\data (see app\README.md).
import 'dart:typed_data';

import 'package:choosen/core/api.dart';
import 'package:choosen/main.dart' as app;
import 'package:choosen/screens/folder_page.dart';
import 'package:choosen/screens/gallery_screen.dart';
import 'package:choosen/screens/ranking_screen.dart';
import 'package:choosen/screens/swipe_screen.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:zxing2/qrcode.dart';

Future<int> rated() async => ((await Api.get('/api/state')) as Map)['rated'] as int;
Future<int> total(String filter) async => ((await Api.get('/api/items', {'filter': filter, 'limit': '1'})) as Map)['total'] as int;

/// The test window is shown without the focus (on another monitor). Before keys and clicks the
/// app is told that its window is in front: keys then reach the focused screen, and a click is not
/// taken for the one that brings the window forward.
Future<void> front(WidgetTester t) async {
  await t.binding.defaultBinaryMessenger.handlePlatformMessage(
    'flutter/lifecycle',
    const StringCodec().encodeMessage('AppLifecycleState.resumed'),
    (_) {},
  );
  await settle(t, 400);
}

/// The verdict of the previous rating as the top of the swipe screen shows it ("好評", "見送り", ...):
/// "再評価：見送り → 好評 · 72点" gives "好評". Counting images would not do: a review card that had
/// the same verdict before leaves the counts as they were.
String lastVerdict(WidgetTester t) {
  final texts = t.widgetList<Text>(find.descendant(of: find.byKey(const ValueKey('lastRate')), matching: find.byType(Text)));
  final label = texts.map((w) => w.data ?? '').firstWhere((s) => s.isNotEmpty, orElse: () => '').split(' · ').first;
  return label.split('→').last.trim();
}

/// Draws the core's QR rows as a picture (4 px per module, 4-module quiet zone) and reads it with
/// ZXing, a decoder that shares no code with ours.
String readQr(List<String> rows) {
  const scale = 4, quiet = 4;
  final n = rows.length, size = (n + 2 * quiet) * scale;
  final px = Int32List(size * size)..fillRange(0, size * size, 0xFFFFFFFF);
  for (var y = 0; y < n; y++) {
    for (var x = 0; x < n; x++) {
      if (rows[y][x] != '1') continue;
      for (var dy = 0; dy < scale; dy++) {
        final at = ((y + quiet) * scale + dy) * size + (x + quiet) * scale;
        px.fillRange(at, at + scale, 0xFF000000);
      }
    }
  }
  return QRCodeReader().decode(BinaryBitmap(HybridBinarizer(RGBLuminanceSource(size, size, px)))).text;
}

Future<void> settle(WidgetTester t, [int ms = 1500]) async {
  for (var i = 0; i < ms ~/ 100; i++) {
    await t.pump(const Duration(milliseconds: 100));
  }
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('swipe, rate, go back and correct, then browse', (t) async {
    await app.main();
    await settle(t, 3000);
    await t.runAsync(() => Future<void>.delayed(const Duration(seconds: 1)));
    await settle(t);
    final r0 = await t.runAsync(rated) ?? 0;

    await t.tap(find.byTooltip('好評'));
    await settle(t);
    await t.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 600)));
    expect(await t.runAsync(rated), r0 + 1, reason: 'the like button rates the card');
    await settle(t, 500);
    expect(
      find.descendant(of: find.byKey(const ValueKey('lastRate')), matching: find.textContaining('好評')),
      findsOneWidget,
      reason: 'the top shows the previous image\'s rating',
    );

    await t.tap(find.byTooltip('見送り'));
    await settle(t);
    await t.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 600)));
    expect(await t.runAsync(rated), r0 + 2, reason: 'the pass button rates the next card');

    await t.tap(find.byTooltip('前の画像に戻る'));
    await settle(t, 500);
    expect(find.text('前の画像を評価し直す'), findsOneWidget, reason: 'back shows the previous card again');

    await front(t);
    await t.sendKeyEvent(LogicalKeyboardKey.delete);  // archive it instead (every layout)
    await settle(t);
    await t.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 600)));
    expect(find.text('前の画像を評価し直す'), findsNothing, reason: 'the correction is done');
    final r2 = await t.runAsync(rated) ?? 0;
    expect(r2, lessThanOrEqualTo(r0 + 2), reason: 'a correction replaces, it never adds a rating');

    // ---- back and forth follow the order the cards were seen in (the owner's steps, 2026-10-08):
    // like 3, back 3 (the same cards in reverse), like 3 again (on through the same cards, then the
    // fourth), back again (the same order once more)
    dynamic shown() => (t.state(find.byType(SwipeScreen)) as dynamic).shownId;
    Future<void> like() async {
      await t.tap(find.byTooltip('好評'));
      await settle(t);
      await t.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 400)));
      await settle(t, 300);
    }
    Future<void> back() async {
      await t.tap(find.byTooltip('前の画像に戻る'));
      await settle(t, 500);
    }
    final seen = <dynamic>[];
    for (var i = 0; i < 3; i++) {
      seen.add(shown());
      await like();
    }
    final fourth = shown();
    final backs = <dynamic>[];
    for (var i = 0; i < 3; i++) {
      await back();
      backs.add(shown());
    }
    expect(backs, seen.reversed.toList(), reason: 'back shows the cards just seen, newest first');
    final again = <dynamic>[];
    for (var i = 0; i < 3; i++) {
      await like();
      again.add(shown());
    }
    expect(again, [seen[1], seen[2], fourth], reason: 'after a correction the next card is the one seen after it');
    final backs2 = <dynamic>[];
    for (var i = 0; i < 3; i++) {
      await back();
      backs2.add(shown());
    }
    expect(backs2, seen.reversed.toList(), reason: 'the order stays the same after corrections');
    for (var i = 0; i < 3; i++) {
      await like(); // back to the newest card for the rest of the test
    }

    // ---- the mouse modes (as in Version 1 0.7.2)
    final s = t.widget<SwipeScreen>(find.byType(SwipeScreen)).state;
    final center = t.getCenter(find.byType(SwipeScreen));
    await front(t);

    await t.runAsync(() => s.setConfig({'mouse_mode': '2'}));
    await settle(t, 500);
    final rated0 = await t.runAsync(rated) ?? 0;
    await t.sendEventToBinding(PointerScrollEvent(position: center, scrollDelta: const Offset(0, 100)));
    await settle(t);
    await t.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 600)));
    expect(lastVerdict(t), '見送り', reason: 'mode 2: one wheel step down passes');
    expect(await t.runAsync(rated), greaterThanOrEqualTo(rated0), reason: 'and it is saved');

    await t.runAsync(() => s.setConfig({'mouse_mode': '3'}));
    await settle(t, 500);
    final mouse = await t.createGesture(kind: PointerDeviceKind.mouse, buttons: kSecondaryButton);
    await front(t);
    await mouse.down(center);
    await mouse.up();
    await settle(t, 300);
    expect(find.textContaining('フリック中'), findsOneWidget, reason: 'mode 3: a right click starts flicking');
    await mouse.moveBy(const Offset(100, 0)); // a quick flick to the right, no button held
    await settle(t);
    await t.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 600)));
    expect(lastVerdict(t), '好評', reason: 'mode 3: a flick to the right likes');
    final left = await t.createGesture(kind: PointerDeviceKind.mouse, buttons: kPrimaryButton);
    // Windows does not always report the window as active: a click must work in any lifecycle state
    await t.binding.defaultBinaryMessenger.handlePlatformMessage(
      'flutter/lifecycle',
      const StringCodec().encodeMessage('AppLifecycleState.inactive'),
      (_) {},
    );
    await settle(t, 400);
    await left.down(center);
    await left.up();
    await settle(t, 500);
    expect(find.text('前の画像を評価し直す'), findsOneWidget, reason: 'mode 3: a left click goes back');
    await front(t);
    await mouse.down(center);
    await mouse.up();
    await settle(t, 300);
    expect(find.textContaining('フリック中'), findsNothing, reason: 'mode 3: another right click ends flicking');
    await t.runAsync(() => s.setConfig({'mouse_mode': '1'}));
    await settle(t, 300);

    // ---- 範囲, 対象 and the card buttons (0.7.4)
    await t.tap(find.byKey(const ValueKey('scope')));
    await settle(t, 1500);
    final firstFolder = t.widget<CheckboxListTile>(find.byType(CheckboxListTile).first);
    final folderName = (firstFolder.title as Text).data!;
    await t.tap(find.byType(CheckboxListTile).first);
    await t.tap(find.text('決定'));
    await settle(t, 2000);
    expect(
      find.descendant(of: find.byKey(const ValueKey('scope')), matching: find.text(folderName)),
      findsOneWidget,
      reason: 'the chosen folder is the range',
    );
    expect(find.textContaining(folderName), findsAtLeastNWidgets(2), reason: 'and the card comes from that folder');
    await t.tap(find.byKey(const ValueKey('target')));
    await settle(t, 500);
    await t.tap(find.widgetWithText(CheckedPopupMenuItem<String>, '未評価だけ'));
    await settle(t, 1500);
    expect(
      find.descendant(of: find.byKey(const ValueKey('target')), matching: find.text('未評価だけ')),
      findsOneWidget,
      reason: 'the target is shown',
    );
    await t.runAsync(() => s.setConfig({'swipe_buttons': '0'}));
    await settle(t, 500);
    expect(find.byTooltip('好評'), findsNothing, reason: 'the card buttons can be hidden');
    await t.runAsync(() => s.setConfig({'swipe_buttons': '1'}));
    await settle(t, 500);
    expect(find.byTooltip('好評'), findsWidgets, reason: 'and shown again');
    // the chevron on the card does the same, and is not a mouse command
    await t.tap(find.byKey(const ValueKey('buttonsToggle')));
    await settle(t, 700);
    expect(find.byTooltip('好評'), findsNothing, reason: 'the chevron hides the card buttons');
    await t.tap(find.byKey(const ValueKey('buttonsToggle')));
    await settle(t, 1200);
    expect(find.byTooltip('好評'), findsWidgets, reason: 'and brings them back');

    await t.tap(find.text('ギャラリー'));
    await settle(t, 2500);
    expect(find.byType(GridView), findsOneWidget, reason: 'the gallery shows a grid of liked images');
    await t.tap(find.text('フォルダ別'));
    await settle(t, 2000);
    expect(find.byType(FolderTile), findsWidgets, reason: 'the gallery shows one tile per folder');
    await t.tap(find.byType(FolderTile).first);
    await settle(t, 2000);
    expect(find.byType(FolderImagesPage), findsOneWidget, reason: 'a folder tile opens the folder');
    expect(find.byType(GalleryTile), findsWidgets, reason: 'with its images');
    await t.tap(find.byTooltip('戻る'));
    await settle(t, 800);

    await t.tap(find.text('ランキング'));
    await settle(t, 2000);
    expect(find.byType(FolderRankRow), findsWidgets, reason: 'the ranking lists folders');
    await t.tap(find.byType(FolderRankRow).first);
    await settle(t, 2000);
    expect(find.byType(GalleryTile), findsWidgets, reason: 'a ranking row opens the folder with its images');
    await t.tap(find.byTooltip('戻る'));
    await settle(t, 800);

    await t.tap(find.text('設定'));
    await settle(t, 1000);
    await t.scrollUntilVisible(find.text('書き出す'), 300, scrollable: find.byType(Scrollable).last);
    expect(find.text('書き出し'), findsOneWidget, reason: 'the settings offer the export');
  });

  // The core is running since the first test.
  testWidgets('QR codes for pairing a phone read back with ZXing', (t) async {
    const texts = [
      'http://192.168.15.2:47801/#pair=123456',
      'http://10.0.0.5:47801/#pair=000042',
      'Choosen',
      'http://192.168.100.200:47801/#pair=987654&x=0123456789abcdef0123456789abcdef',
    ];
    for (final text in texts) {
      final r = await t.runAsync(() => Api.get('/api/qr', {'text': text})) as Map;
      final rows = [for (final row in r['rows'] as List) row as String];
      expect(rows, isNotEmpty, reason: 'a symbol for "$text"');
      expect(readQr(rows), text, reason: 'ZXing reads "$text"');
    }
  });
}
