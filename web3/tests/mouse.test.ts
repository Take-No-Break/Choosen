import { describe, expect, it } from 'vitest';
import { Flick, buttonInput, mouseMap } from '../src/lib/mouse';

describe('mouse modes', () => {
  it('match Version 1: mode 2 clicks and wheel, mode 3 flick on the right button', () => {
    expect(mouseMap(2).left).toBe('super');
    expect(mouseMap(2).right).toBe('back');
    expect(mouseMap(2).wheelDown).toBe('pass');
    expect(mouseMap(2).middle).toBe('pass');
    expect(mouseMap(3).right).toBe('flick');
    expect(mouseMap(3).left).toBe('back');
    expect(mouseMap(1).left).toBe('grab');
    expect(mouseMap(9).left).toBe('grab'); // unknown mode: mode 1
  });
  it('an own layout overrides mode 1; hold (removed) and unknown actions do nothing', () => {
    const m = mouseMap(4, 'left:super;right:back;wheelDown:hold;side1:archive,pass;middle:explode');
    expect(m.left).toBe('super');
    expect(m.right).toBe('back');
    expect(m.wheelDown).toBe('none');
    expect(m.side1).toBe('archive');
    expect(m.middle).toBe('none');
    expect(m.wheelUp).toBe('back'); // not in the layout: mode 1
  });
  it('buttons map to inputs', () => {
    expect([0, 1, 2, 3, 4, 5].map(buttonInput)).toEqual(['left', 'middle', 'right', 'side1', 'side2', null]);
  });
});

describe('flick', () => {
  it('a quick 100 px move flicks; the same move spread over a second does not', () => {
    const f = new Flick();
    f.reset(0);
    expect(f.move(100, 0, 10)).not.toBeNull();
    const slow = new Flick();
    slow.reset(0);
    let hit = null;
    for (let t = 10; t <= 1000 && !hit; t += 100) hit = slow.move(10, 0, t);
    expect(hit).toBeNull();
  });
  it('the hand moving back right after a flick is not a flick the other way', () => {
    const f = new Flick();
    f.reset(0);
    expect(f.move(100, 0, 10)).not.toBeNull();
    expect(f.move(-100, 0, 60)).toBeNull(); // within the quiet time
    expect(f.move(-100, 0, 120)).toBeNull(); // still moving: quiet extends
    expect(f.move(-100, 0, 400)).not.toBeNull(); // after a still moment it counts again
  });
});
