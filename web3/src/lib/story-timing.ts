/** A picture stays for at least the selected time, with room to stagger several replacements. */
export function storyTiming(seconds: number, staggered: boolean) {
  const minimum = seconds * 1000;
  const spread = staggered ? Math.min(2000, minimum * 0.25) : 0;
  return { minimum, spread, round: minimum + spread };
}
