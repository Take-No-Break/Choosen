// One size range for the gallery's layout, pinch preview and saved-size conversion.
export const GALLERY_MAX_TILE = 320;
export const PHONE_GALLERY_COLUMNS = 6;
export const SQUARE_TILE_SCALE = 0.88;

export function galleryMinTile(width: number, phoneApp: boolean, gap = 3): number {
  if (!phoneApp || width <= 0) return 72;
  // Round the six-column cell down first: rounding the scaled tile back up must still fit it.
  const cell = Math.floor((width - gap * (PHONE_GALLERY_COLUMNS - 1)) / PHONE_GALLERY_COLUMNS);
  return Math.min(GALLERY_MAX_TILE, Math.max(1, Math.floor(cell / SQUARE_TILE_SCALE)));
}

export function galleryTilePx(size: number, minimum: number): number {
  return Math.round(minimum + (Math.min(100, Math.max(0, size)) / 100) * (GALLERY_MAX_TILE - minimum));
}

export function gallerySizeOf(px: number, minimum: number): number {
  if (minimum >= GALLERY_MAX_TILE) return 0;
  return Math.min(100, Math.max(0, ((px - minimum) / (GALLERY_MAX_TILE - minimum)) * 100));
}
