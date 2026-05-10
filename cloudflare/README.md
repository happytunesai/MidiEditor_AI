# MidiEditor AI - Rendezvous Service

Tiny Cloudflare Worker that bootstraps a WebRTC live session between
**one host** and **up to 8 joiners** by swapping their SDP blobs over a
4-character pairing code. Each joiner is the WebRTC initiator and gets
its own offer / answer pair (joiner-initiated, multi-peer protocol).

After every peer establishes its DTLS data channel, this service is no
longer touched - it's only a bootstrap. Encrypted MIDI traffic flows
peer-to-peer over DTLS, **not** through Cloudflare.

> Service version: **v3** (joiner-initiated multi-peer; rolled out with
> MidiEditor AI 1.7.0). The earlier 1:1 host-as-initiator protocol is
> retired - clients pinned to the old endpoints will get 404s. Self-host
> deployments need to ship the v3 endpoints to interoperate.

## Deploy via the Cloudflare web UI (no Node.js needed)

1. **Sign in** at <https://dash.cloudflare.com>.
2. Left sidebar -> **Compute** -> **Workers & Pages**.
3. Click **Create application** -> **Create Worker**.
4. Name it `midi-rdv` (or anything you want - note the URL Cloudflare
   gives you, e.g. `https://midi-rdv.<your-subdomain>.workers.dev`).
5. Click **Deploy** to confirm the placeholder code, then **Edit code**.
6. Replace the editor contents with [`rendezvous.js`](rendezvous.js)
   (paste). Click **Save and deploy**.
7. **Bind the KV namespace.** Back in the worker overview -> **Settings**
   -> **Variables and Secrets** -> **KV Namespace Bindings** ->
   **Add binding**. Variable name: `RDV`. Click **+ Add new namespace**,
   call it `midi-rdv-kv`, save. Then redeploy the worker.
8. Test it: `https://midi-rdv.<your-subdomain>.workers.dev/health`
   should return:
   ```json
   {
     "service": "midi-rdv",
     "version": 3,
     "protocol": "joiner-initiated-multi-peer",
     "maxPeersPerSession": 8,
     "docs": "https://github.com/happytunesai/MidiEditor_AI/tree/main/cloudflare"
   }
   ```

That URL goes into MidiEditor AI's settings (`Settings -> Collaboration
-> WAN Rendezvous -> Rendezvous URL`) and becomes the default for
everyone you share your build with.

## Deploy via CLI (faster if you have Node.js)

```bash
npm install -g wrangler
cd cloudflare/
wrangler login                        # opens browser, OAuth
wrangler kv namespace create RDV      # prints an id; paste into wrangler.toml
wrangler deploy                       # done
```

Output: `https://midi-rdv.<your-subdomain>.workers.dev`.

> Older wrangler versions used `kv:namespace` (with a colon). Both
> still work today; v4+ prefers the space form.

## Free-tier capacity

The multi-peer protocol changes the per-session cost meaningfully
compared to the old 1:1 model. Worst-case figures for an active
session at the 8-peer cap:

| Metric | Free Tier / day | Per-joiner | Host-poll @ 2s |
|--------|-----------------|------------|----------------|
| Worker requests | 100 000 | ~6 | 1 list + N gets per tick |
| KV reads | 100 000 | ~3 | scales with peer count |
| KV writes | 1 000 | ~2 | 1 per joiner offer + 1 per host answer |

A typical 30-minute, 5-peer session lands around 200-300 worker
requests and 15-20 KV writes. The KV write quota (1 000/day) is the
tightest: that's roughly **50-70 multi-peer sessions/day** on the free
tier - more for smaller (1:1, 1:2) sessions, fewer for full 8-peer
ones. Casual / hobby use stays comfortably inside free; if you outgrow
it, paid plans start at $5/mo.

When you hit the cap, requests fail until midnight UTC. No surprise
bill.

## Privacy posture

- Worker logs are **off** by default - Cloudflare doesn't see SDP
  contents unless you explicitly turn on observability.
- All keys (session, joiner-offer, host-answer, joiner-index) auto-
  expire after 5 minutes (`expirationTtl: 300`). After that the
  rendezvous knows nothing about the session.
- Cloudflare DOES see metadata: peer IPs (in the SDP candidate list)
  and the ~600-byte SDP blobs during the handshake window. They do
  **not** see any MIDI edit traffic - that's DTLS-encrypted
  peer-to-peer once the data channel opens.

## Pointing MidiEditor AI at your own deployment

Once the worker is up, give its URL to the app:

1. Open MidiEditor AI -> **Settings -> Collaboration**.
2. Scroll to **WAN Rendezvous**.
3. Paste your URL (`https://midi-rdv.<your-subdomain>.workers.dev`)
   into the **Rendezvous URL** field.
4. Click **Test connection** - completes within ~6 seconds end-to-end.
   The two-stage probe runs `GET /health` against your URL first, then
   spins up an in-process two-`WebRtcTransport` DTLS loopback to
   exercise the local UDP / firewall path. A green grade means both
   stages succeeded; the failure detail panel pinpoints whether
   rendezvous, DTLS, or local UDP is the blocker.
5. Close Settings - the change persists across launches.

To revert to the bundled default, clear the field and click OK.

## Self-hosting on something other than Cloudflare

The service is small enough to run on anything HTTPS-capable that has
a key/value store with a TTL primitive. Implement the same six
endpoints (full schemas in [`rendezvous.js`](rendezvous.js)'s header
comment):

| Method | Path | Behaviour |
|---|---|---|
| `POST` | `/session` | Body `{sessionId, displayName}` -> generate 4-char code, store at `session:CODE` (TTL 300 s), return `{code, expiresInSec}`. |
| `GET` | `/code/<code>` | Return `{sessionId, displayName}` if `session:CODE` exists, else 404. (Used by joiner to verify the code before initiating WebRTC.) |
| `POST` | `/code/<code>/joiner-offer` | Body `{joinerId, sdp}` -> store at `joiner-offer:CODE:<joinerId>`, append to `joiner-index:CODE`. **409** if the joinerId is already taken, **404** if session expired, **503** if peer count >= 8. |
| `GET` | `/code/<code>/joiner-offers` | Read `joiner-index:CODE`, fetch each `joiner-offer:CODE:<id>` by direct GET, return `{offers: [{joinerId, sdp, ts}, ...]}`. (Host polls this every ~2 s.) |
| `POST` | `/code/<code>/host-answer` | Body `{joinerId, sdp}` -> store at `host-answer:CODE:<joinerId>`. **409** if an answer already exists for that joinerId (the host can only answer once per joiner; mirrors the BUG-COLLAB-019 / BUG-COLLAB-032 MITM-defence pattern). |
| `GET` | `/code/<code>/host-answer/<joinerId>` | Return `{sdp}` if posted, else 404. (Joiner polls this.) |
| `GET` | `/health` | Return `{service, version: 3, protocol: "joiner-initiated-multi-peer", maxPeersPerSession: 8, docs: "..."}` - the editor's pre-flight checks `version >= 3` and the protocol string. |

### Critical: the `joiner-index:CODE` key

If your KV backend's `list({prefix: ...})` operation has eventual
consistency (Cloudflare KV does - we observed ~14 s lag from `put()`
to a freshly written key showing up in `list()` results from the same
PoP), **do not** rely on prefix-list to enumerate joiner offers. v3
maintains an explicit ordered `joiner-index:CODE` key written + read
with `get()` / `put()` (read-your-writes consistent within a PoP) and
appended whenever a new joiner posts an offer. The host reads that
single key and then issues N direct `get()`s for the offers.

Skip this and your host will see joiner offers with multi-second
delays, breaking session start-up.

### Reasonable backends

Deno Deploy + Deno KV, Bun + SQLite, Node + Redis, plain Python +
dict-with-TTL, Cloudflare-Workers-compatible shims like Miniflare for
local testing. The whole worker is ~300 LoC; porting takes about a
day with the index trick included.

## Why deploy your own?

The bundled default URL is a single shared Cloudflare Worker on the
free tier - its 1 000/day KV-write quota caps the total at ~50-70
multi-peer sessions/day across **all** users of this build combined.
Fine for casual / occasional use, but if you run a band that does live
edits every evening, or a classroom of students, run your own. There's
no recurring cost on Cloudflare's free tier as long as you stay under
100 000 requests + 1 000 KV writes per day, which on a per-deployment
basis is several thousand sessions for typical hobby use. No credit
card required at signup.
