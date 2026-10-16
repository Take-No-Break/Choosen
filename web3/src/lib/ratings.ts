// The page's one outbox (lib/outbox.ts): the swipe screen and the gallery's full view rate through
// it, so their ratings leave in one order and share one stored queue.
import { postJ } from './api';
import { Outbox, type Rating, type Saved } from './outbox';

export const outbox = new Outbox((r: Rating) =>
  r.a === 'X' // a withdrawal: `replace` names the rating that is taken back
    ? postJ<Saved>('/api/revoke', { cid: r.replace })
    : postJ<Saved>('/api/rate', { id: r.id, fp: r.fp, a: r.a, cid: r.cid, ...(r.replace ? { replace: r.replace } : {}) }),
);
