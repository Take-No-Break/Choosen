// Swiping, Tinder-style: the picture fills the card, the buttons float on it, a drag tilts the
// card and shows what it will mean. Keyboard layouts A-C and mouse modes 1-3 work as in Version 1
// (0.7.2); an own mouse layout saved by Version 1 is used as well.
import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';

import '../core/api.dart';
import '../core/models.dart';
import '../ui/photo.dart';
import '../ui/theme.dart';
import 'scope_picker.dart';

/// L like, S super like, D pass, A archive (the core's rating letters).
enum Verdict {
  like('L', '好評', Rate.like, Icons.favorite),
  superLike('S', '超好評', Rate.superLike, Icons.star),
  pass('D', '見送り', Rate.pass, Icons.close),
  archive('A', 'アーカイブ', Rate.archive, Icons.archive_outlined);

  const Verdict(this.code, this.label, this.color, this.icon);
  final String code, label;
  final Color color;
  final IconData icon;
}

class _Past {
  _Past(this.card, this.verdict, this.cid);
  final CardItem card;
  final Verdict verdict;
  final String cid;
}

/// The previous image's rating, shown at the top: "好評", "再評価：見送り → 好評" for an image that
/// comes back, "訂正：好評 → 超好評" after going back. Points and the folder's score follow once saved.
class _Last {
  _Last(this.label, this.verdict, this.before);
  final String label;
  final Verdict verdict;
  final num? before; // the points before, for an image rated earlier
  num? after;
  String folder = ''; // "Momo 64.2 → 65.0 ▲ · 3位"
}

const _letters = {'L': '好評', 'S': '超好評', 'D': '見送り', 'K': '保留', 'A': 'アーカイブ', 'R': '元に戻す'};

class SwipeScreen extends StatefulWidget {
  const SwipeScreen({super.key, required this.state, this.active = true});
  final AppState state;
  final bool active; // the swipe tab is the one shown

  @override
  State<SwipeScreen> createState() => _SwipeScreenState();
}

class _SwipeScreenState extends State<SwipeScreen> with TickerProviderStateMixin {
  final List<CardItem> _queue = [];
  final List<_Past> _history = [];
  _Past? _redo; // the past rating being changed (after "back")
  bool _loading = false, _exhausted = false;
  final FocusNode _focus = FocusNode();
  List<String> _scope = const []; // 範囲: folder tokens; empty = everything
  String _scopeLabel = 'すべて';
  String _filter = ''; // 対象: "" = new images and reviews as set in the settings
  int _gen = 0; // a new range or target starts the queue over
  _Last? _last;

  Offset _drag = Offset.zero;
  bool _panning = false; // the current drag moves the card
  late final AnimationController _fly = AnimationController(vsync: this, duration: const Duration(milliseconds: 220));
  Animation<Offset>? _flyAnim;
  Verdict? _flying;
  Timer? _holdTimer;
  bool _holdFired = false;

  final Stopwatch _clock = Stopwatch()..start();
  late final AppLifecycleListener _life;
  int _focusAt = -1000; // when the window last came to the front (ms on _clock)

  @override
  void initState() {
    super.initState();
    _life = AppLifecycleListener(onResume: () => _focusAt = _clock.elapsedMilliseconds);
    _flickTicker = createTicker((_) => _flickTick());
    widget.state.addListener(_onState);
    _refill();
  }

  @override
  void didUpdateWidget(SwipeScreen old) {
    super.didUpdateWidget(old);
    if (!widget.active) _flickOff(redraw: false); // another tab: build follows anyway
  }

  @override
  void dispose() {
    widget.state.removeListener(_onState);
    _life.dispose();
    _flickTicker.dispose();
    _fly.dispose();
    _focus.dispose();
    _holdTimer?.cancel();
    super.dispose();
  }

  void _onState() {
    if (_queue.isEmpty && !_loading) _refill();
    // the state has the saved chevron choice: show the core's value again (the same, so no redraw needed)
    if (!_buttonsSaving && _buttons == widget.state.swipeButtons) _buttons = null;
  }

  CardItem? get _current => _redo?.card ?? (_queue.isEmpty ? null : _queue.first);

  /// The id of the card on screen (the integration test follows the order of back and forth with it).
  @visibleForTesting
  int? get shownId => _current?.id;

  Future<void> _refill() async {
    if (_loading || !widget.state.hasRoot) return;
    _loading = true;
    final gen = _gen;
    try {
      final r = await Api.get('/api/next', {
        'n': '${8 - _queue.length}',
        'scope': _scope.join('|'),
        'filter': _filter,
        'exclude': _queue.map((c) => c.id).join(','),
        'recent': _history.reversed.take(5).map((h) => h.card.id).join(','),
      }) as Map;
      final have = _queue.map((c) => c.id).toSet();
      final fresh = [for (final j in r['items'] as List) CardItem(Map<String, dynamic>.from(j as Map))]
          .where((c) => !have.contains(c.id));
      if (!mounted || gen != _gen) return;
      setState(() {
        _queue.addAll(fresh);
        _exhausted = _queue.isEmpty;
      });
      for (final c in _queue.take(3)) {
        final p = c.path;
        if (p.isNotEmpty) precacheImage(FileImage(File(p)), context);
      }
    } catch (_) {
      // the core is still loading; the state listener tries again
    } finally {
      _loading = false;
      if (mounted && gen != _gen) _refill(); // the range or target changed meanwhile
    }
  }

  /// A new range (範囲) or target (対象): the cards start over; the history stays.
  void _choose({ScopeChoice? scope, String? filter}) {
    setState(() {
      if (scope != null) {
        _scope = scope.tokens;
        _scopeLabel = scope.label;
      }
      if (filter != null) _filter = filter;
      _gen++;
      _queue.clear();
      _redo = null;
      _exhausted = false;
      _drag = Offset.zero;
    });
    _refill();
  }

  Future<void> _rate(Verdict v) async {
    final card = _current;
    if (card == null) return;
    final replace = _redo;
    final cid = Api.newCid();
    final again = replace == null && card.rated && _letters.containsKey(card.last) && v != Verdict.archive;
    final last = _Last(
      replace != null && replace.verdict != v
          ? '訂正：${replace.verdict.label} → ${v.label}'
          : again
          ? '再評価：${_letters[card.last]} → ${v.label}'
          : v.label,
      v,
      replace == null && card.rated ? card.points : null,
    );
    setState(() {
      _last = last;
      final now = _Past(card, v, cid);
      // A correction keeps its place, and the next card is the one seen after it (as in Version 1):
      // going back and forth always follows the order the cards were seen in.
      final k = replace == null ? -1 : _history.indexOf(replace);
      if (k >= 0) {
        _history[k] = now;
        _redo = k + 1 < _history.length ? _history[k + 1] : null;
      } else {
        if (replace == null) _queue.removeAt(0);
        _history.add(now);
        _redo = null;
      }
      _drag = Offset.zero;
    });
    HapticFeedback.lightImpact();
    if (_queue.length < 4) _refill();
    try {
      final r = await Api.post('/api/rate', {
        'id': '${card.id}',
        'fp': card.fp,
        'a': v.code,
        'cid': cid,
        if (replace != null) 'replace': replace.cid,
      }) as Map;
      if (mounted && identical(_last, last)) {
        setState(() {
          last.after = (r['item'] as Map?)?['p'] as num?;
          last.folder = _folderChange(r['chain'] as List? ?? const []);
        });
      }
    } catch (_) {
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('評価を保存できませんでした')));
    }
  }

  /// The rated image's folder at the ranking level: its score before → after, and its place.
  String _folderChange(List chain) {
    final rows = [for (final c in chain) Map<String, dynamic>.from(c as Map)];
    final ad = widget.state.artistDepth;
    final f = rows.where((c) => c['artist'] == true).firstOrNull ??
        rows.where((c) => c['depth'] == ad).firstOrNull ??
        rows.firstOrNull;
    if (f == null || (f['depth'] == 0 && f['artist'] != true)) return '';
    final before = (f['before'] as num).toDouble(), after = (f['after'] as num).toDouble();
    final arrow = after - before > .005 ? ' ▲' : after - before < -.005 ? ' ▼' : '';
    final rank = f['rank'] is int && f['rank'] > 0 ? ' · ${f['rank']}位' : '';
    return '${f['name']} ${before.toStringAsFixed(1)} → ${after.toStringAsFixed(1)}$arrow$rank';
  }

  /// Shows the previous card again; rating it replaces its earlier rating.
  void _back() {
    if (_history.isEmpty) return;
    final idx = _redo == null ? _history.length - 1 : _history.indexOf(_redo!) - 1;
    if (idx < 0) return;
    setState(() {
      _redo = _history[idx];
      _drag = Offset.zero;
    });
  }

  /// After "back": the next newer card again; past the newest one, the queue.
  void _forward() {
    final r = _redo;
    if (r == null) return;
    final idx = _history.indexOf(r) + 1;
    setState(() {
      _redo = idx < _history.length ? _history[idx] : null;
      _drag = Offset.zero;
    });
  }

  Future<void> _throw(Verdict v, Size size) async {
    if (_flying != null || _current == null) return;
    final dir = switch (v) {
      Verdict.like => const Offset(1.4, 0.1),
      Verdict.pass => const Offset(-1.4, 0.1),
      Verdict.superLike => const Offset(0, -1.4),
      Verdict.archive => const Offset(0, 1.4),
    };
    _flying = v;
    _flyAnim = Tween<Offset>(
      begin: _drag,
      end: Offset(dir.dx * size.width, dir.dy * size.height),
    ).animate(CurvedAnimation(parent: _fly, curve: Curves.easeIn));
    await _fly.forward(from: 0);
    _flying = null;
    _flyAnim = null;
    await _rate(v);
    _fly.reset();
  }

  Verdict? _verdictFor(Offset d, Size size) {
    if (d.dx.abs() >= d.dy.abs()) {
      if (d.dx > size.width * .22) return Verdict.like;
      if (d.dx < -size.width * .22) return Verdict.pass;
    } else {
      if (d.dy < -size.height * .18) return Verdict.superLike;
      if (d.dy > size.height * .22) return Verdict.archive;
    }
    return null;
  }

  /// One command from a key, a mouse button or the wheel.
  void _run(String cmd, Size size) {
    switch (cmd) {
      case 'like':
        _throw(Verdict.like, size);
      case 'pass':
        _throw(Verdict.pass, size);
      case 'super':
        _throw(Verdict.superLike, size);
      case 'archive':
        _throw(Verdict.archive, size);
      case 'back':
        _back();
      case 'fwd':
        _forward();
      case 'flick':
        _flickOn ? _flickOff() : _flickStart();
    }
  }

  // ---- keyboard: layouts A (1), B (2), C (3) as in Version 1; an own layout (4) falls back to B
  static final _keyLayouts = <int, Map<LogicalKeyboardKey, String>>{
    1: {
      LogicalKeyboardKey.arrowRight: 'like',
      LogicalKeyboardKey.arrowLeft: 'pass',
      LogicalKeyboardKey.arrowUp: 'super',
      LogicalKeyboardKey.arrowDown: 'back',
      LogicalKeyboardKey.enter: 'archive',
      LogicalKeyboardKey.numpadEnter: 'archive',
      LogicalKeyboardKey.delete: 'archive',
    },
    2: {
      LogicalKeyboardKey.arrowRight: 'likeHold',
      LogicalKeyboardKey.arrowLeft: 'pass',
      LogicalKeyboardKey.arrowUp: 'archive',
      LogicalKeyboardKey.arrowDown: 'back',
      LogicalKeyboardKey.delete: 'archive',
    },
    3: {
      LogicalKeyboardKey.keyD: 'like',
      LogicalKeyboardKey.keyA: 'pass',
      LogicalKeyboardKey.keyW: 'super',
      LogicalKeyboardKey.shiftLeft: 'back',
      LogicalKeyboardKey.keyE: 'archive',
      LogicalKeyboardKey.delete: 'archive',
    },
  };

  KeyEventResult _onKey(FocusNode node, KeyEvent e, Size size) {
    if (e.logicalKey == LogicalKeyboardKey.escape && _flickOn) {
      if (e is KeyDownEvent) _flickOff();
      return KeyEventResult.handled;
    }
    final layout = _keyLayouts[widget.state.keyMode] ?? _keyLayouts[2]!;
    final cmd = layout[e.logicalKey];
    if (cmd == null) return KeyEventResult.ignored;
    if (cmd == 'likeHold') {
      // short press like, held 0.4 s super like
      if (e is KeyDownEvent) {
        _holdFired = false;
        _holdTimer = Timer(const Duration(milliseconds: 400), () {
          _holdFired = true;
          _throw(Verdict.superLike, size);
        });
      } else if (e is KeyUpEvent) {
        _holdTimer?.cancel();
        if (!_holdFired) _throw(Verdict.like, size);
      }
      return KeyEventResult.handled;
    }
    if (e is KeyDownEvent) _run(cmd, size);
    return KeyEventResult.handled;
  }

  // ---- mouse (PC): modes 1-3 as in Version 1 (0.7.2). No mode has "hold".
  static const _mousePresets = <int, Map<String, String>>{
    1: {'left': 'grab', 'right': 'grab', 'middle': 'none', 'wheelUp': 'back', 'wheelDown': 'fwd', 'side1': 'back', 'side2': 'fwd'},
    2: {'left': 'super', 'right': 'back', 'middle': 'pass', 'wheelUp': 'like', 'wheelDown': 'pass', 'side1': 'archive', 'side2': 'fwd'},
    3: {'left': 'back', 'right': 'flick', 'middle': 'none', 'wheelUp': 'back', 'wheelDown': 'fwd', 'side1': 'back', 'side2': 'fwd'},
  };
  static const _ratings = {'like', 'super', 'pass', 'archive'};
  String? _downCmd; // what the button of the current mouse press does
  int? _buttonPointer; // a press that started on one of the round buttons on the card
  int _wheelLock = 0;

  /// Mode 4 = the own layout saved by Version 1 ("left:super;right:back;..."), on top of mode 1.
  Map<String, String> get _mouseMap {
    final mode = widget.state.mouseMode;
    if (mode != 4) return _mousePresets[mode] ?? _mousePresets[1]!;
    final m = Map<String, String>.from(_mousePresets[1]!);
    for (final part in widget.state.customMouse.split(';')) {
      final i = part.indexOf(':');
      if (i <= 0) continue;
      final first = part.substring(i + 1).split(',').firstWhere((a) => a.isNotEmpty, orElse: () => '');
      if (first.isNotEmpty) m[part.substring(0, i)] = first == 'hold' ? 'none' : first; // hold was removed in 0.7.2
    }
    return m;
  }

  static String? _buttonInput(int buttons) {
    if ((buttons & kPrimaryButton) != 0) return 'left';
    if ((buttons & kSecondaryButton) != 0) return 'right';
    if ((buttons & kMiddleMouseButton) != 0) return 'middle';
    if ((buttons & kBackMouseButton) != 0) return 'side1';
    if ((buttons & kForwardMouseButton) != 0) return 'side2';
    return null;
  }

  void _onPointerDown(PointerDownEvent e, Size size) {
    _downCmd = null;
    if (e.kind != PointerDeviceKind.mouse || e.pointer == _buttonPointer) return; // touch drags; buttons tap
    final input = _buttonInput(e.buttons);
    final cmd = input == null ? null : _mouseMap[input];
    _downCmd = cmd;
    if (cmd == null || cmd == 'none' || cmd == 'grab') return;
    // the click that brings the window to the front does nothing else (mode 3: a left click goes back)
    final now = _clock.elapsedMilliseconds;
    // Only the moment it came forward counts: Windows does not always report the window as active,
    // so the lifecycle state itself must not decide (it blocked every click in 0.7.4).
    if (now - _focusAt < 250) return;
    _run(cmd, size);
  }

  void _onWheel(PointerSignalEvent e, Size size) {
    if (e is! PointerScrollEvent || e.scrollDelta.dy.abs() < 1) return;
    final cmd = _mouseMap[e.scrollDelta.dy < 0 ? 'wheelUp' : 'wheelDown'];
    if (cmd == null || cmd == 'none') return;
    final now = _clock.elapsedMilliseconds;
    if (!_ratings.contains(cmd)) {
      // going back / forward may repeat while scrolling
      if (now < _wheelLock) return;
      _wheelLock = now + 150;
      _run(cmd, size);
      return;
    }
    // One flick of the wheel = one rating: the lock ends only after the wheel has been still for a
    // moment, so a free-spinning wheel cannot rate several images by accident.
    final locked = now < _wheelLock;
    _wheelLock = now + 220;
    if (!locked) _run(cmd, size);
  }

  // ---- flick (mouse mode 3): a right click starts it and another one (or Esc) ends it. Meanwhile
  // a quick flick of the mouse (or of one finger on a touchpad) rates without holding a button.
  // Movement "leaks away" (time constant 100 ms), so slow movement never adds up to a throw; after
  // a throw the next flick waits until the pointer has been still for a moment, so the hand moving
  // back is not read as a flick the other way. Flutter has no pointer lock: the pointer is hidden
  // but can still reach the edge of the window.
  static const double _flickT = 80; // px of (leaking) movement that make a flick
  static const double _flickLeak = 100; // ms
  static const int _flickQuiet = 250; // ms after a throw before the next flick can count
  static const int _flickStill = 120; // ms without movement that re-arm it
  bool _flickOn = false;
  Offset _flick = Offset.zero;
  int _flickAt = 0, _flickQuietUntil = 0;
  late final Ticker _flickTicker;

  void _flickStart() {
    setState(() {
      _flickOn = true;
      _flick = Offset.zero;
      _flickAt = _clock.elapsedMilliseconds;
      _flickQuietUntil = 0;
    });
  }

  void _flickOff({bool redraw = true}) {
    if (!_flickOn) return;
    _flickOn = false;
    _flick = Offset.zero;
    _flickTicker.stop();
    if (redraw) setState(() {});
  }

  void _flickLeakTo(int now) {
    _flick *= math.exp(-math.max(0, now - _flickAt) / _flickLeak);
    _flickAt = now;
  }

  void _onFlickMove(PointerEvent e, Size size) {
    if (!_flickOn || e.kind != PointerDeviceKind.mouse) return;
    final now = _clock.elapsedMilliseconds;
    final d = e.delta;
    if (now < _flickQuietUntil || _flying != null) {
      if (d.dx.abs() + d.dy.abs() > 2) _flickQuietUntil = math.max(_flickQuietUntil, now + _flickStill); // still moving
      _flick = Offset.zero;
      _flickAt = now;
      return;
    }
    _flickLeakTo(now);
    _flick += d;
    final ax = _flick.dx.abs(), ay = _flick.dy.abs();
    if (math.max(ax, ay) >= _flickT && _current != null) {
      final v = ax >= ay
          ? (_flick.dx > 0 ? Verdict.like : Verdict.pass)
          : (_flick.dy < 0 ? Verdict.superLike : Verdict.archive);
      _drag = _flick * 1.6; // the card flies on from where it leans
      _flick = Offset.zero;
      _flickQuietUntil = now + math.max(_flickQuiet, _fly.duration!.inMilliseconds);
      _flickTicker.stop();
      _throw(v, size);
      return;
    }
    if (!_flickTicker.isActive) _flickTicker.start();
    setState(() {});
  }

  // The card leans the way the pointer moves and settles back as the movement leaks away.
  void _flickTick() {
    if (!_flickOn) {
      _flickTicker.stop();
      return;
    }
    _flickLeakTo(_clock.elapsedMilliseconds);
    if (_flick.distance < .5) {
      _flick = Offset.zero;
      _flickTicker.stop();
    }
    setState(() {});
  }

  // ---- dragging the card: touch always; a mouse only with a button that grabs (mode 1)
  void _panStart(DragStartDetails d) {
    _panning = !_flickOn && (d.kind != PointerDeviceKind.mouse || _downCmd == 'grab');
  }

  void _panUpdate(DragUpdateDetails d) {
    if (_panning) setState(() => _drag += d.delta);
  }

  void _panEnd(DragEndDetails d, Size size) {
    if (!_panning) return;
    _panning = false;
    final v =
        _verdictFor(_drag, size) ??
        (d.velocity.pixelsPerSecond.dx > 900
            ? Verdict.like
            : d.velocity.pixelsPerSecond.dx < -900
            ? Verdict.pass
            : d.velocity.pixelsPerSecond.dy < -900
            ? Verdict.superLike
            : null);
    if (v != null) {
      _throw(v, size);
    } else {
      setState(() => _drag = Offset.zero);
    }
  }

  // ---- the round buttons: the small chevron under them hides and shows them (setting swipe_buttons)
  bool? _buttons; // the choice, shown at once until the state has it
  bool _buttonsSaving = false;

  // One save at a time: two posts at once can reach the core in either order and leave the older press
  // in config.txt. A press during a save only changes _buttons; the running save then sends the newest.
  Future<void> _toggleButtons() async {
    setState(() => _buttons = !(_buttons ?? widget.state.swipeButtons));
    if (_buttonsSaving) return;
    _buttonsSaving = true;
    final state = widget.state; // the last press is saved even if the screen goes away meanwhile
    bool? sent;
    var saved = true;
    while (saved && _buttons != sent) {
      final v = _buttons!;
      sent = v;
      try {
        saved = await state.setConfig({'swipe_buttons': v ? '1' : '0'}) == null;
      } catch (_) {
        saved = false; // reported below
      }
    }
    _buttonsSaving = false;
    if (!mounted) return;
    if (saved && state.swipeButtons != _buttons) return; // saved; _onState lets go once the state has it
    setState(() => _buttons = null); // the core's value again (the last saved one if saving failed)
    if (!saved) ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('保存できませんでした')));
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.state;
    if (!s.ready) return const Center(child: CircularProgressIndicator());
    if (!s.hasRoot) {
      return const _Empty(icon: Icons.folder_open, title: '写真フォルダがまだありません', text: '設定で写真フォルダを選ぶと、ここで評価できます。');
    }
    final card = _current;
    return LayoutBuilder(
      builder: (context, box) {
        final size = Size(box.maxWidth, box.maxHeight);
        return Focus(
          focusNode: _focus,
          autofocus: true,
          onKeyEvent: (n, e) => _onKey(n, e, size),
          child: MouseRegion(
            cursor: _flickOn ? SystemMouseCursors.none : MouseCursor.defer,
            child: Listener(
              behavior: HitTestBehavior.opaque,
              onPointerHover: (e) => _onFlickMove(e, size),
              onPointerMove: (e) => _onFlickMove(e, size),
              child: Column(
                children: [
                  _Header(
                    state: s,
                    redo: _redo != null,
                    scopeLabel: _scopeLabel,
                    filter: _filter,
                    last: _last,
                    onScope: () async {
                      final c = await ScopePicker.show(context, _scope);
                      if (c != null) _choose(scope: c);
                    },
                    onFilter: (f) => _choose(filter: f),
                    onLast: _history.isEmpty ? null : _back,
                  ),
                  Expanded(
                    child: Padding(
                      padding: const EdgeInsets.fromLTRB(12, 4, 12, 12),
                      child: card == null
                          ? (_exhausted
                                ? const _Empty(
                                    icon: Icons.check_circle_outline,
                                    title: '今は評価する画像がありません',
                                    text: 'しばらくすると再評価の画像が戻ってきます。',
                                  )
                                : const Center(child: CircularProgressIndicator()))
                          // mouse buttons and the wheel act on the card only, never on the controls above it
                          : Listener(
                              onPointerDown: (e) => _onPointerDown(e, size),
                              onPointerSignal: (e) => _onWheel(e, size),
                              child: Stack(
                              fit: StackFit.expand,
                              children: [
                                if (_redo == null && _queue.length > 1)
                                  Transform.scale(scale: .95, child: _SwipeCard(card: _queue[1], dimmed: true)),
                                AnimatedBuilder(
                                  animation: _fly,
                                  builder: (context, child) {
                                    final leaningOnly = _flickOn && _flyAnim == null;
                                    final off = _flyAnim?.value ?? (leaningOnly ? _flick * 1.6 : _drag);
                                    final angle = leaningOnly ? _flick.dx * .05 * math.pi / 180 : off.dx / size.width * .35;
                                    return Transform.translate(
                                      offset: off,
                                      child: Transform.rotate(angle: angle, child: child),
                                    );
                                  },
                                  child: RawGestureDetector(
                                    gestures: <Type, GestureRecognizerFactory>{
                                      PanGestureRecognizer: GestureRecognizerFactoryWithHandlers<PanGestureRecognizer>(
                                        // the right button grabs as well (mouse mode 1)
                                        () => PanGestureRecognizer(
                                          allowedButtonsFilter: (b) => b == kPrimaryButton || b == kSecondaryButton,
                                        ),
                                        (r) => r
                                          ..onStart = _panStart
                                          ..onUpdate = _panUpdate
                                          ..onEnd = (d) => _panEnd(d, size),
                                      ),
                                    },
                                    child: _SwipeCard(
                                      card: card,
                                      showButtons: _buttons ?? s.swipeButtons,
                                      leaning: _flying ?? _leaning(),
                                      leanAmount: _flying != null ? 1 : _leanAmount(size),
                                      redo: _redo,
                                      onButton: (v) => _throw(v, size),
                                      onBack: _history.isEmpty ? null : _back,
                                      onToggleButtons: _toggleButtons,
                                      onButtonsDown: (pointer) => _buttonPointer = pointer,
                                    ),
                                  ),
                                ),
                                if (_flickOn) const Positioned(top: 12, left: 0, right: 0, child: Center(child: _FlickBadge())),
                              ],
                            ),
                            ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }

  Verdict? _leaning() {
    final d = _flickOn ? _flick : _drag;
    if (d == Offset.zero) return null;
    if (d.dx.abs() >= d.dy.abs()) return d.dx > 0 ? Verdict.like : Verdict.pass;
    return d.dy < 0 ? Verdict.superLike : Verdict.archive;
  }

  double _leanAmount(Size size) {
    if (_flickOn) return (math.max(_flick.dx.abs(), _flick.dy.abs()) / _flickT).clamp(0, 1).toDouble();
    final d = math.max(_drag.dx.abs() / (size.width * .22), _drag.dy.abs() / (size.height * .2));
    return d.clamp(0, 1).toDouble();
  }
}

/// The top line: title, 範囲 (folders), 対象 (which images), the previous image's rating, counts.
class _Header extends StatelessWidget {
  const _Header({
    required this.state,
    required this.redo,
    required this.scopeLabel,
    required this.filter,
    required this.last,
    required this.onScope,
    required this.onFilter,
    required this.onLast,
  });
  final AppState state;
  final bool redo;
  final String scopeLabel, filter;
  final _Last? last;
  final VoidCallback onScope;
  final void Function(String) onFilter;
  final VoidCallback? onLast;

  /// 対象: [value, menu text, chip text]. "" follows the review share of the settings.
  List<List<String>> _targets() {
    final share = state.reviewShare;
    final mixed = share >= .999
        ? '再評価だけ'
        : share <= .001
        ? '未評価（なくなったら再評価）'
        : '未評価 ＋ 再評価（${(1 / share).round()}枚に1枚）';
    return [
      ['', mixed, share >= .999 ? '再評価だけ' : '未評価＋再評価'],
      ['fresh', '未評価だけ', '未評価だけ'],
      ['liked', '好評・超好評を厳選', '好評・超好評'],
      ['super', '超好評を厳選', '超好評'],
      ['passed', '見送りを見直す', '見送り'],
    ];
  }

  @override
  Widget build(BuildContext context) {
    final st = state.state;
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    final targets = _targets();
    final target = targets.firstWhere((t) => t[0] == filter, orElse: () => targets.first);
    final l = last;
    // A phone: no title (the bottom bar names the tab) and no counts, so the controls fit one line.
    final compact = MediaQuery.sizeOf(context).width < 760;
    final pts = l == null || l.verdict == Verdict.archive || l.after == null
        ? ''
        : l.before != null && l.before!.round() != l.after!.round()
        ? '${l.before!.round()} → ${l.after!.round()}点'
        : '${l.after!.round()}点';
    return Padding(
      padding: EdgeInsets.fromLTRB(compact ? 10 : 16, compact ? 6 : 8, compact ? 10 : 16, 4),
      child: Row(
        children: [
          Expanded(
            child: Wrap(
              spacing: compact ? 6 : 8,
              runSpacing: 6,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                if (!compact || redo)
                  Text(
                    redo ? '前の画像を評価し直す' : 'スワイプ',
                    style: TextStyle(fontSize: compact ? 15 : 18, fontWeight: FontWeight.w700),
                  ),
                _Pill(
                  key: const ValueKey('scope'),
                  icon: Icons.folder_outlined,
                  text: scopeLabel,
                  active: scopeLabel != 'すべて',
                  onTap: onScope,
                ),
                PopupMenuButton<String>(
                  key: const ValueKey('target'),
                  tooltip: '対象',
                  onSelected: onFilter,
                  itemBuilder: (_) => [
                    for (final t in targets)
                      CheckedPopupMenuItem(value: t[0], checked: t[0] == filter, child: Text(t[1])),
                  ],
                  child: _Pill(icon: Icons.filter_list, text: target[2], active: filter.isNotEmpty),
                ),
                if (l != null)
                  Tooltip(
                    key: const ValueKey('lastRate'),
                    message: l.folder.isEmpty ? 'クリックで前の画像に戻る' : '${l.folder}\nクリックで前の画像に戻る',
                    child: _Pill(
                      icon: l.verdict.icon,
                      color: l.verdict.color,
                      text: pts.isEmpty ? l.label : '${l.label} · $pts',
                      onTap: onLast,
                    ),
                  ),
              ],
            ),
          ),
          const SizedBox(width: 8),
          if (state.scanning)
            Padding(
              padding: const EdgeInsets.only(right: 10),
              child: Text('読み込み中…', style: TextStyle(color: muted)),
            ),
          if (!compact)
            Text('未評価 ${st['fresh'] ?? 0} · 評価済み ${state.rated}', style: TextStyle(color: muted, fontSize: 13)),
        ],
      ),
    );
  }
}

/// A small rounded label in the header; tappable when [onTap] is set.
class _Pill extends StatelessWidget {
  const _Pill({super.key, required this.icon, required this.text, this.active = false, this.color, this.onTap});
  final IconData icon;
  final String text;
  final bool active;
  final Color? color;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final fg = color ?? (active ? scheme.primary : scheme.onSurfaceVariant);
    final pill = Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: fg.withValues(alpha: .45)),
        color: active ? scheme.primary.withValues(alpha: .12) : null,
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 15, color: fg),
          const SizedBox(width: 5),
          ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 260),
            child: Text(
              text,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(fontSize: 13, color: fg, fontWeight: FontWeight.w600),
            ),
          ),
        ],
      ),
    );
    return onTap == null ? pill : InkWell(borderRadius: BorderRadius.circular(20), onTap: onTap, child: pill);
  }
}

class _FlickBadge extends StatelessWidget {
  const _FlickBadge();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
      decoration: BoxDecoration(color: Colors.black.withValues(alpha: .6), borderRadius: BorderRadius.circular(20)),
      child: const Text('フリック中 — はじいて評価 · 右クリックか Esc で終了', style: TextStyle(fontSize: 13)),
    );
  }
}

class _SwipeCard extends StatelessWidget {
  const _SwipeCard({
    required this.card,
    this.showButtons = true,
    this.dimmed = false,
    this.leaning,
    this.leanAmount = 0,
    this.redo,
    this.onButton,
    this.onBack,
    this.onToggleButtons,
    this.onButtonsDown,
  });
  final CardItem card;
  final bool showButtons; // the round buttons (some only use the mouse or keys and hide them)
  final bool dimmed;
  final Verdict? leaning;
  final double leanAmount;
  final _Past? redo;
  final void Function(Verdict)? onButton;
  final VoidCallback? onBack;
  final VoidCallback? onToggleButtons; // the small chevron under the buttons
  final void Function(int pointer)? onButtonsDown; // a press on a button is not a mouse command

  static const _motion = Duration(milliseconds: 200);

  // The buttons slide down out of the card and fade; they come back the same way.
  static Widget _slideFade(Widget child, Animation<double> a) => FadeTransition(
    opacity: a,
    child: SlideTransition(position: Tween(begin: const Offset(0, 1.6), end: Offset.zero).animate(a), child: child),
  );

  @override
  Widget build(BuildContext context) {
    final tags = <String>[
      if (card.kind == 'new' || !card.rated) '新規' else '再評価 · ${card.timesRated + 1}回目',
      if (card.rated) '${card.points.round()}点',
      if (redo != null) '今回: ${redo!.verdict.label}',
    ];
    return ClipRRect(
      borderRadius: BorderRadius.circular(22),
      child: Stack(
        fit: StackFit.expand,
        children: [
          Photo(path: card.path, backdrop: true),
          if (dimmed) Container(color: Colors.black.withValues(alpha: .35)),
          if (!dimmed) ...[
            // bottom shade so the text and buttons stay readable on bright pictures
            const Positioned(
              left: 0,
              right: 0,
              bottom: 0,
              height: 210,
              child: DecoratedBox(
                decoration: BoxDecoration(
                  gradient: LinearGradient(
                    begin: Alignment.topCenter,
                    end: Alignment.bottomCenter,
                    colors: [Colors.transparent, Color(0xCC000000)],
                  ),
                ),
              ),
            ),
            if (leaning != null && leanAmount > .05) _Stamp(verdict: leaning!, amount: leanAmount),
            AnimatedPositioned(
              duration: _motion,
              curve: Curves.easeOut,
              left: 18,
              right: 18,
              bottom: showButtons ? 110 : 36, // above the buttons, or above the chevron alone
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    card.place.isEmpty ? '写真フォルダの直下' : card.place,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w800, shadows: [Shadow(blurRadius: 8)]),
                  ),
                  const SizedBox(height: 2),
                  Text(
                    card.name,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(color: Colors.white70, fontSize: 13),
                  ),
                  const SizedBox(height: 6),
                  Wrap(
                    spacing: 6,
                    children: [
                      for (final t in tags)
                        Container(
                          padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 2),
                          decoration: BoxDecoration(
                            color: Colors.white.withValues(alpha: .14),
                            borderRadius: BorderRadius.circular(20),
                          ),
                          child: Text(t, style: const TextStyle(fontSize: 12)),
                        ),
                    ],
                  ),
                ],
              ),
            ),
            Positioned(
              left: 0,
              right: 0,
              bottom: 0,
              // pointer events reach this Listener before the card's: it marks presses on a button
              // (the chevron included); presses beside the buttons still reach the card
              child: Listener(
                onPointerDown: (e) => onButtonsDown?.call(e.pointer),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // hidden buttons leave the tree once they have slid away; while going they take no presses
                    IgnorePointer(
                      ignoring: !showButtons,
                      child: AnimatedSwitcher(
                        duration: _motion,
                        switchInCurve: Curves.easeOut,
                        switchOutCurve: Curves.easeOut,
                        transitionBuilder: _slideFade,
                        child: !showButtons
                            ? const SizedBox.shrink()
                            : Center(
                                child: ConstrainedBox(
                                  constraints: const BoxConstraints(maxWidth: 380),
                                  child: Row(
                                    mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                                    children: [
                                      _RoundButton(
                                        icon: Icons.undo,
                                        color: Colors.white70,
                                        size: 46,
                                        onTap: onBack,
                                        tooltip: '前の画像に戻る',
                                      ),
                                      _RoundButton(
                                        icon: Icons.close,
                                        color: Rate.pass,
                                        size: 62,
                                        onTap: () => onButton?.call(Verdict.pass),
                                        tooltip: '見送り',
                                      ),
                                      _RoundButton(
                                        icon: Icons.star,
                                        color: Rate.superLike,
                                        size: 54,
                                        onTap: () => onButton?.call(Verdict.superLike),
                                        tooltip: '超好評',
                                      ),
                                      _RoundButton(
                                        icon: Icons.favorite,
                                        color: Rate.like,
                                        size: 62,
                                        onTap: () => onButton?.call(Verdict.like),
                                        tooltip: '好評',
                                      ),
                                    ],
                                  ),
                                ),
                              ),
                      ),
                    ),
                    const SizedBox(height: 2),
                    // centred, so it sits just below the gap between × and ★
                    _RoundButton(
                      key: const ValueKey('buttonsToggle'),
                      icon: showButtons ? Icons.expand_more : Icons.expand_less,
                      color: Colors.white70,
                      size: 26,
                      iconSize: 20,
                      onTap: onToggleButtons,
                      tooltip: showButtons ? 'ボタンを隠す' : 'ボタンを表示',
                    ),
                    const SizedBox(height: 4),
                  ],
                ),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _Stamp extends StatelessWidget {
  const _Stamp({required this.verdict, required this.amount});
  final Verdict verdict;
  final double amount;

  @override
  Widget build(BuildContext context) {
    final align = switch (verdict) {
      Verdict.like => const Alignment(-.8, -.75),
      Verdict.pass => const Alignment(.8, -.75),
      Verdict.superLike => const Alignment(0, .35),
      Verdict.archive => const Alignment(0, -.2),
    };
    final angle = switch (verdict) {
      Verdict.like => -.25,
      Verdict.pass => .25,
      _ => 0.0,
    };
    return Align(
      alignment: align,
      child: Opacity(
        opacity: amount,
        child: Transform.rotate(
          angle: angle,
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
            decoration: BoxDecoration(
              border: Border.all(color: verdict.color, width: 4),
              borderRadius: BorderRadius.circular(12),
              color: Colors.black.withValues(alpha: .25),
            ),
            child: Text(
              verdict.label,
              style: TextStyle(color: verdict.color, fontSize: 30, fontWeight: FontWeight.w900, letterSpacing: 2),
            ),
          ),
        ),
      ),
    );
  }
}

/// A translucent round button floating on the picture.
class _RoundButton extends StatelessWidget {
  const _RoundButton({
    super.key,
    required this.icon,
    required this.color,
    required this.size,
    this.iconSize,
    required this.onTap,
    required this.tooltip,
  });
  final IconData icon;
  final Color color;
  final double size;
  final double? iconSize; // default: about half the button
  final VoidCallback? onTap;
  final String tooltip;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: Material(
        color: Colors.black.withValues(alpha: .35),
        shape: CircleBorder(
          side: BorderSide(color: color.withValues(alpha: onTap == null ? .25 : .8), width: 2),
        ),
        child: InkWell(
          customBorder: const CircleBorder(),
          onTap: onTap,
          child: SizedBox(
            width: size,
            height: size,
            child: Icon(icon, color: onTap == null ? color.withValues(alpha: .3) : color, size: iconSize ?? size * .48),
          ),
        ),
      ),
    );
  }
}

class _Empty extends StatelessWidget {
  const _Empty({required this.icon, required this.title, required this.text});
  final IconData icon;
  final String title, text;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 56, color: Theme.of(context).colorScheme.onSurfaceVariant),
            const SizedBox(height: 12),
            Text(
              title,
              style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w700),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 6),
            Text(
              text,
              style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant),
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}
