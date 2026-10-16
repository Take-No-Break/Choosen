// The core's API as Futures: each call runs in a short-lived isolate, so sorting 50,000 images
// never blocks an animation. Same routes and JSON as the Windows page (src/api_routes.cpp).
import 'dart:convert';
import 'dart:isolate';
import 'dart:math';

import 'native.dart';

class ApiException implements Exception {
  ApiException(this.status, this.body);
  final int status;
  final String body;
  @override
  String toString() => 'API $status: $body';
}

class Api {
  Api._();

  static Future<dynamic> get(String path, [Map<String, String>? query]) => _call('GET', path, query, '');

  static Future<dynamic> post(String path, [Map<String, String>? query, String body = '']) =>
      _call('POST', path, query, body);

  static Future<dynamic> _call(String method, String path, Map<String, String>? query, String body) async {
    final q = query == null || query.isEmpty ? '' : Uri(queryParameters: query).query;
    final res = await Isolate.run(() => Native.api(method, path, q, body));
    final data = jsonDecode(res.body);
    if (res.status >= 400) throw ApiException(res.status, res.body);
    return data;
  }

  static final Random _rng = Random.secure();

  /// A client id for a rating: makes retries idempotent and lets a correction name it.
  static String newCid() => List.generate(16, (_) => _rng.nextInt(36).toRadixString(36)).join();
}
