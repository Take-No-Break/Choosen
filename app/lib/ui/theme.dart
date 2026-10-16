import 'package:flutter/material.dart';

/// Rating colours, the same meaning as in Version 1.
class Rate {
  static const like = Color(0xFF3CC47C);
  static const superLike = Color(0xFFF4B73A);
  static const pass = Color(0xFFE0566A);
  static const archive = Color(0xFF8C919B);
}

ThemeData choosenTheme() {
  const bg = Color(0xFF0E0F11);
  final scheme = ColorScheme.fromSeed(
    seedColor: const Color(0xFF7D9BFF),
    brightness: Brightness.dark,
    surface: const Color(0xFF16181C),
  );
  return ThemeData(
    colorScheme: scheme,
    scaffoldBackgroundColor: bg,
    useMaterial3: true,
    fontFamilyFallback: const ['Yu Gothic UI', 'Meiryo', 'Noto Sans JP', 'sans-serif'],
    navigationBarTheme: NavigationBarThemeData(
      backgroundColor: const Color(0xFF141519),
      indicatorColor: scheme.primary.withValues(alpha: .18),
    ),
    navigationRailTheme: const NavigationRailThemeData(backgroundColor: Color(0xFF141519)),
    cardTheme: CardThemeData(
      color: const Color(0xFF181A1F),
      elevation: 0,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
    ),
    chipTheme: ChipThemeData(shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20))),
  );
}
