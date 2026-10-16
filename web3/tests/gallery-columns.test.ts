import { describe, expect, it } from 'vitest';
import { galleryMinTile, galleryTilePx, gallerySizeOf, PHONE_GALLERY_COLUMNS, SQUARE_TILE_SCALE } from '../src/lib/gallery-size';
import { squareGrid, shapesGrid } from '../src/lib/vgrid';

const widths = [320, 343, 360, 390, 430, 800];
const firstRowCount = (grid: ReturnType<typeof squareGrid>) => {
  let n = 0;
  while (n < grid.count && grid.at(n).y === 0) n++;
  return n;
};

describe('phone gallery six-column sizing', () => {
  it.each(widths)('reaches exactly six columns at the smallest size at %ipx', (width) => {
    const min = galleryMinTile(width, true);
    const tile = Math.round(galleryTilePx(0, min) * SQUARE_TILE_SCALE);
    const grid = squareGrid(60, width, tile, 3, PHONE_GALLERY_COLUMNS);
    expect(firstRowCount(grid)).toBe(6);
    expect(grid.at(5).x + grid.at(5).w).toBeCloseTo(width);
    expect(grid.at(6).y).toBeGreaterThan(0);
    const step = grid.at(6).y;
    expect(grid.band(step + 0.1, step + grid.at(6).h - 0.1)).toEqual([6, 11]);
    expect(grid.height).toBeCloseTo(step * 10 - 3);
  });

  it('keeps the saved 0–100 scale and the pinch limits in the same range', () => {
    for (const width of widths) {
      const min = galleryMinTile(width, true);
      expect(gallerySizeOf(min / 2, min)).toBe(0);
      expect(gallerySizeOf(640, min)).toBe(100);
      for (const size of [0, 40, 100]) {
        const px = galleryTilePx(size, min);
        expect(gallerySizeOf(px, min)).toBeCloseTo(size, 0);
        const grid = squareGrid(60, width, Math.round(px * SQUARE_TILE_SCALE), 3, PHONE_GALLERY_COLUMNS);
        expect(firstRowCount(grid)).toBeLessThanOrEqual(6);
      }
    }
  });

  it('keeps the PC size range and uncapped column count', () => {
    expect(galleryMinTile(800, false)).toBe(72);
    for (const size of [0, 40, 100]) {
      expect(galleryTilePx(size, 72)).toBe(Math.round(72 + size / 100 * 248));
    }
    const grid = squareGrid(60, 800, Math.round(72 * SQUARE_TILE_SCALE), 3);
    expect(firstRowCount(grid)).toBe(12);
  });

  it('caps narrow original-shape rows at six only when requested', () => {
    const ratios = new Array(30).fill(9 / 16);
    const grid = shapesGrid(ratios, 360, galleryTilePx(0, galleryMinTile(360, true)) * 0.8, 3, PHONE_GALLERY_COLUMNS);
    const rows = new Map<number, number>();
    for (let i = 0; i < grid.count; i++) {
      const p = grid.at(i);
      rows.set(p.y, (rows.get(p.y) ?? 0) + 1);
      expect(p.w / p.h).toBeCloseTo(9 / 16, 2);
    }
    expect([...rows.values()]).toEqual([6, 6, 6, 6, 6]);
    expect(firstRowCount(shapesGrid(ratios, 360, 40, 3))).toBeGreaterThan(6);
    const step = grid.at(6).y;
    expect(grid.band(step + 0.1, step + grid.at(6).h - 0.1)).toEqual([6, 11]);
  });
});
