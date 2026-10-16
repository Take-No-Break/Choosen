import 'dart:io';
import 'dart:ui';

import 'package:flutter/material.dart';

/// A photo from the library. The whole picture is always visible (never cropped); with [backdrop]
/// the space around it is filled with a blurred copy, so a card looks full like in Tinder.
class Photo extends StatelessWidget {
  const Photo({super.key, required this.path, this.backdrop = false, this.fit = BoxFit.contain, this.cacheWidth});
  final String path;
  final bool backdrop;
  final BoxFit fit;
  final int? cacheWidth;

  @override
  Widget build(BuildContext context) {
    if (path.isEmpty) return const _Missing();
    final file = File(path);
    final image = Image.file(
      file,
      fit: fit,
      cacheWidth: cacheWidth,
      gaplessPlayback: true,
      filterQuality: FilterQuality.medium,
      errorBuilder: (_, _, _) => const _Missing(),
    );
    if (!backdrop) return image;
    return Stack(
      fit: StackFit.expand,
      children: [
        ColoredBox(
          color: const Color(0xFF101114),
          child: ImageFiltered(
            imageFilter: ImageFilter.blur(sigmaX: 28, sigmaY: 28),
            child: Opacity(
              opacity: .55,
              child: Image.file(
                file,
                fit: BoxFit.cover,
                cacheWidth: 96,
                gaplessPlayback: true,
                errorBuilder: (_, _, _) => const SizedBox(),
              ),
            ),
          ),
        ),
        image,
      ],
    );
  }
}

class _Missing extends StatelessWidget {
  const _Missing();

  @override
  Widget build(BuildContext context) {
    return Container(
      color: const Color(0xFF15171B),
      alignment: Alignment.center,
      child: Icon(Icons.broken_image_outlined, color: Theme.of(context).colorScheme.onSurfaceVariant),
    );
  }
}
