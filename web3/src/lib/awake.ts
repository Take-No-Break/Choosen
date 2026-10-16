// Keeps the screen on (Screen Wake Lock) while something plays. The browser lets go of the lock
// whenever the page is hidden, so it is asked for again when the page comes back.

type Lock = { release: () => Promise<void> };
type WakeLock = { request: (type: 'screen') => Promise<Lock> };

/** Keeps the screen on until the returned function is called. Does nothing where unsupported. */
export function keepAwake(): () => void {
  const wl = (navigator as Navigator & { wakeLock?: WakeLock }).wakeLock;
  if (!wl) return () => {};
  let on = true;
  let lock: Lock | null = null;
  const ask = () => {
    if (!on || document.hidden) return;
    wl.request('screen')
      .then((l) => (on ? (lock = l) : void l.release().catch(() => {})))
      .catch(() => {});
  };
  document.addEventListener('visibilitychange', ask);
  ask();
  return () => {
    on = false;
    document.removeEventListener('visibilitychange', ask);
    void lock?.release().catch(() => {});
    lock = null;
  };
}
