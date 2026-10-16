// The phone apps' launch screen (#hello in index.html: white and light blue, the icon's cards
// settle and the star pops). The app's own page shows it from the first paint; it steps aside once
// the page is up, and plays again each time the app comes back to the front (the app calls
// window.choosenHello()). A tap skips it. Not shown in a browser.
import { PHONE_APP } from './api';

const FIRST_MS = 1100; // from the page's start
const AGAIN_MS = 950;

export function initHello() {
  const el = document.getElementById('hello');
  if (!el) return;
  if (!PHONE_APP) return void el.remove();
  let timer = 0;
  const out = () => {
    clearTimeout(timer);
    el.classList.add('out');
    timer = window.setTimeout(() => (el.hidden = true), 400);
  };
  const play = (ms: number) => {
    clearTimeout(timer);
    el.hidden = false;
    el.classList.remove('out', 'go');
    void el.offsetWidth; // the animation starts over
    el.classList.add('go');
    timer = window.setTimeout(out, ms);
  };
  el.addEventListener('pointerdown', out);
  (window as unknown as { choosenHello?: () => void }).choosenHello = () => play(AGAIN_MS);
  if (document.documentElement.classList.contains('app')) {
    timer = window.setTimeout(out, Math.max(450, FIRST_MS - performance.now())); // already playing
  } else {
    document.documentElement.classList.add('app'); // a PC's page in the app: from now
    play(AGAIN_MS);
  }
}
