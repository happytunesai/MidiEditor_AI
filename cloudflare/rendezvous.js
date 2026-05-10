/*
 * MidiEditor AI — WebRTC signaling rendezvous (Plan §11.10, Phase 9.6 + 9.8).
 *
 * Tiny stateless service that maps a 4-char pairing code to a session
 * marker plus N independent offer/answer pairs (one per joiner). After
 * the WebRTC data channel opens, neither peer talks to this service
 * again — it's only a bootstrap.
 *
 * --------------------------------------------------------------
 *  Phase 9.8 multi-peer protocol (joiner-initiated)
 * --------------------------------------------------------------
 *
 * The earlier 1:1 protocol had the HOST as WebRTC initiator (one
 * offer SDP shared by everyone, single answer slot). That doesn't
 * scale to N joiners — every joiner needs its own offer/answer pair
 * because every PeerConnection has its own DTLS credentials and
 * mid-line allocation.  Phase 9.8 flips the roles: each JOINER is
 * the WebRTC initiator (creates its own offer), the host is the
 * responder (creates an answer per incoming offer).
 *
 *   POST /session
 *     body: { sessionId, displayName }
 *     resp: { code: "M7P3", expiresInSec: 300 }
 *
 *   GET /code/<code>
 *     resp: { sessionId, displayName }       (404 if not found / expired)
 *     (Joiner uses this to verify the code before initiating WebRTC.)
 *
 *   POST /code/<code>/joiner-offer
 *     body: { joinerId, sdp }
 *     resp: { ok: true }                     (409 if joinerId reused;
 *                                              404 if session expired;
 *                                              503 if session is full)
 *
 *   GET /code/<code>/joiner-offers
 *     resp: { offers: [ { joinerId, sdp, ts }, ... ] }
 *     (Host polls. Returns ALL pending joiner offers; host filters
 *     locally against the joiners it already paired with.)
 *
 *   POST /code/<code>/host-answer
 *     body: { joinerId, sdp }
 *     resp: { ok: true }                     (404 if no session)
 *
 *   GET /code/<code>/host-answer/<joinerId>
 *     resp: { sdp }                          (404 if not yet posted)
 *     (Joiner polls.)
 *
 *   GET /health
 *     resp: { service, version, docs }
 *
 * Storage: Cloudflare KV namespace bound as `RDV` with 300-second TTL.
 *   session:CODE                  → { sessionId, displayName, ts }
 *   joiner-index:CODE             → [ { joinerId, ts }, ... ]   (ordered)
 *   joiner-offer:CODE:<joinerId>  → { sdp, ts }
 *   host-answer:CODE:<joinerId>   → { sdp, ts }
 *
 * The `joiner-index:CODE` key is the source of truth for "which joiners
 * have offers waiting"; the host reads this single key instead of doing
 * a `list({prefix: ...})`. KV's `list()` operation has unbounded
 * eventual-consistency lag (observed ~14+ seconds before a freshly
 * `put()`'d key appears in `list()` results from the same PoP),
 * whereas direct `get()` of a known key has read-your-writes
 * consistency within a single PoP. v3 of the protocol switched to
 * the index approach to make the in-process Connection Test pass and
 * to avoid 30+ second startup delays in real WAN sessions.
 *
 * Codes use the same alphabet as LAN pairing codes (no I/L/O/0/1).
 *
 * Privacy: SDPs contain public IP + port info, expire after 5 minutes,
 * and are never logged. The encrypted MIDI traffic itself flows
 * peer-to-peer over DTLS, NOT through this service.
 *
 * Free-tier budget: 100k requests/day. Per-joiner cost: ~6 requests
 * (verify code, post offer, poll for answer × ~3, retrieve answer).
 * Per host-poll cost: 1 list + N gets (KV list returns up to 1000
 * keys per call). At a 2-second poll interval and 5 joiners, that's
 * ~22k KV reads/hour — comfortably inside the free tier for hobby
 * use.
 *
 * Phase 9.8 also enforces a soft per-session slot cap (default 8) to
 * keep host bandwidth bounded. Beyond the cap, joiner-offer returns
 * 503 with a clear message; client surfaces "session is full".
 */

const ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"; // no I,L,O,0,1
const TTL_SECONDS = 300;
const MAX_PEERS_PER_SESSION = 8;        // soft cap; can be raised later
const ALLOW_ORIGIN = "*";

function corsHeaders() {
    return {
        "Access-Control-Allow-Origin": ALLOW_ORIGIN,
        "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
        "Access-Control-Allow-Headers": "Content-Type",
    };
}

function json(status, obj) {
    return new Response(JSON.stringify(obj), {
        status,
        headers: { "Content-Type": "application/json", ...corsHeaders() },
    });
}

function generateCode() {
    let s = "";
    const len = ALPHABET.length;
    for (let i = 0; i < 4; i++) {
        s += ALPHABET[Math.floor(Math.random() * len)];
    }
    return s;
}

async function pickFreshCode(env) {
    for (let i = 0; i < 5; i++) {
        const c = generateCode();
        const exists = await env.RDV.get(`session:${c}`);
        if (!exists) return c;
    }
    throw new Error("Code collision storm — try again");
}

// Index of joinerIds for a given session code. Maintained as a single
// JSON-encoded array under `joiner-index:CODE` because KV's `list()`
// operation has unbounded eventual consistency lag — observed taking
// 14+ seconds to surface a just-written key, which is past the
// in-process Connection Test's 30 s timeout. A single key, by contrast,
// has read-your-writes consistency within a single Cloudflare PoP, so
// the host can see new joiners within ~100 ms.
async function readJoinerIndex(env, code) {
    const raw = await env.RDV.get(`joiner-index:${code}`);
    if (!raw) return [];
    try {
        const arr = JSON.parse(raw);
        return Array.isArray(arr) ? arr : [];
    } catch (_) {
        return [];
    }
}

async function writeJoinerIndex(env, code, ids) {
    await env.RDV.put(
        `joiner-index:${code}`,
        JSON.stringify(ids),
        { expirationTtl: TTL_SECONDS }
    );
}

// Soft cap on joiners per session — protects host bandwidth.
// Concurrent POSTs racing to append could lose entries (KV has no
// transactions), but at the casual-collab traffic levels we're aiming
// for (joiners arrive seconds apart in human time, max 8 per session),
// the practical risk is near zero.
async function countActiveJoiners(env, code) {
    const idx = await readJoinerIndex(env, code);
    return idx.length;
}

// JoinerId sanity check. Joiners pick their own; we just guard against
// path-injection / oversized values. Lower/upper-case and digits and
// dash, 8..64 chars.
function isValidJoinerId(s) {
    return typeof s === "string"
        && s.length >= 8 && s.length <= 64
        && /^[A-Za-z0-9-]+$/.test(s);
}

export default {
    async fetch(request, env) {
        const url = new URL(request.url);
        const path = url.pathname;

        if (request.method === "OPTIONS") {
            return new Response(null, { status: 204, headers: corsHeaders() });
        }

        try {
            // ---- POST /session  (host announces a new session) -----------
            if (path === "/session" && request.method === "POST") {
                const body = await request.json();
                const sessionId = (body && body.sessionId) || "";
                const displayName = (body && body.displayName) || "";
                if (!sessionId || sessionId.length > 256) {
                    return json(400, { error: "missing or oversized sessionId" });
                }
                const code = await pickFreshCode(env);
                await env.RDV.put(
                    `session:${code}`,
                    JSON.stringify({
                        sessionId,
                        displayName: displayName.slice(0, 64),
                        ts: Date.now(),
                    }),
                    { expirationTtl: TTL_SECONDS }
                );
                return json(200, { code, expiresInSec: TTL_SECONDS });
            }

            // ---- GET /code/<code>  (joiner verifies code) ----------------
            const codeOnly = path.match(/^\/code\/([A-Z0-9]{4})$/);
            if (codeOnly && request.method === "GET") {
                const code = codeOnly[1];
                const raw = await env.RDV.get(`session:${code}`);
                if (!raw) return json(404, { error: "not found or expired" });
                const data = JSON.parse(raw);
                return json(200, {
                    sessionId: data.sessionId,
                    displayName: data.displayName,
                });
            }

            // ---- POST /code/<code>/joiner-offer  (joiner publishes offer) -
            const joPostMatch = path.match(/^\/code\/([A-Z0-9]{4})\/joiner-offer$/);
            if (joPostMatch && request.method === "POST") {
                const code = joPostMatch[1];
                const session = await env.RDV.get(`session:${code}`);
                if (!session) return json(404, { error: "no session for code" });
                const body = await request.json();
                const joinerId = (body && body.joinerId) || "";
                const sdp = (body && body.sdp) || "";
                if (!isValidJoinerId(joinerId)) {
                    return json(400, { error: "invalid joinerId" });
                }
                if (!sdp || sdp.length > 64 * 1024) {
                    return json(400, { error: "missing or oversized sdp" });
                }
                // BUG-COLLAB-019 carry-over: refuse to overwrite an
                // existing offer for this joinerId. New joiners must
                // pick a fresh ID.
                const existing = await env.RDV.get(`joiner-offer:${code}:${joinerId}`);
                if (existing) {
                    return json(409, {
                        error: "joinerId already in use; pick a fresh one"
                    });
                }
                // Soft slot cap — protects host bandwidth.
                const active = await countActiveJoiners(env, code);
                if (active >= MAX_PEERS_PER_SESSION) {
                    return json(503, {
                        error: `session is full (${MAX_PEERS_PER_SESSION} peers)`,
                    });
                }
                // Write the offer first, then append to the index. If
                // the index write fails the offer key still exists but
                // is unreachable; the 5-minute TTL cleans it up.
                const ts = Date.now();
                await env.RDV.put(
                    `joiner-offer:${code}:${joinerId}`,
                    JSON.stringify({ sdp, ts }),
                    { expirationTtl: TTL_SECONDS }
                );
                const idx = await readJoinerIndex(env, code);
                if (!idx.find(e => e.joinerId === joinerId)) {
                    idx.push({ joinerId, ts });
                    await writeJoinerIndex(env, code, idx);
                }
                return json(200, { ok: true });
            }

            // ---- GET /code/<code>/joiner-offers  (host polls list) -------
            const joListMatch = path.match(/^\/code\/([A-Z0-9]{4})\/joiner-offers$/);
            if (joListMatch && request.method === "GET") {
                const code = joListMatch[1];
                const session = await env.RDV.get(`session:${code}`);
                if (!session) return json(404, { error: "no session for code" });
                // Read the explicit index, then fetch each offer by
                // direct key. Both operations are read-your-writes
                // consistent within the same PoP, unlike `list()`.
                const idx = await readJoinerIndex(env, code);
                const offers = [];
                for (const entry of idx) {
                    const joinerId = entry.joinerId;
                    if (!joinerId) continue;
                    const raw = await env.RDV.get(`joiner-offer:${code}:${joinerId}`);
                    if (!raw) continue;  // TTL expired
                    const data = JSON.parse(raw);
                    offers.push({ joinerId, sdp: data.sdp, ts: data.ts });
                }
                return json(200, { offers });
            }

            // ---- POST /code/<code>/host-answer  (host posts answer) ------
            const haPostMatch = path.match(/^\/code\/([A-Z0-9]{4})\/host-answer$/);
            if (haPostMatch && request.method === "POST") {
                const code = haPostMatch[1];
                const session = await env.RDV.get(`session:${code}`);
                if (!session) return json(404, { error: "no session for code" });
                const body = await request.json();
                const joinerId = (body && body.joinerId) || "";
                const sdp = (body && body.sdp) || "";
                if (!isValidJoinerId(joinerId)) {
                    return json(400, { error: "invalid joinerId" });
                }
                if (!sdp || sdp.length > 64 * 1024) {
                    return json(400, { error: "missing or oversized sdp" });
                }
                // BUG-COLLAB-032: refuse to overwrite an existing answer
                // for this joinerId. Without this an attacker who guessed
                // the 4-char code could scrape joinerIds via the public
                // `/joiner-offers` GET and then overwrite the host's
                // answer SDP with their own (poisoned DTLS fingerprint),
                // turning the joiner's connection into a MITM channel.
                // Same defence pattern as the joiner-offer path above
                // (BUG-COLLAB-019).
                const existingAnswer =
                    await env.RDV.get(`host-answer:${code}:${joinerId}`);
                if (existingAnswer) {
                    return json(409, {
                        error: "answer already posted for this joinerId"
                    });
                }
                await env.RDV.put(
                    `host-answer:${code}:${joinerId}`,
                    JSON.stringify({ sdp, ts: Date.now() }),
                    { expirationTtl: TTL_SECONDS }
                );
                return json(200, { ok: true });
            }

            // ---- GET /code/<code>/host-answer/<joinerId>  (joiner polls) -
            const haGetMatch = path.match(
                /^\/code\/([A-Z0-9]{4})\/host-answer\/([A-Za-z0-9-]{8,64})$/);
            if (haGetMatch && request.method === "GET") {
                const code = haGetMatch[1];
                const joinerId = haGetMatch[2];
                const raw = await env.RDV.get(`host-answer:${code}:${joinerId}`);
                if (!raw) return json(404, { error: "not yet" });
                const data = JSON.parse(raw);
                return json(200, { sdp: data.sdp });
            }

            // ---- Health check ----------------------------------------------
            if (path === "/" || path === "/health") {
                return json(200, {
                    service: "midi-rdv",
                    version: 3,                   // bumped from 2 (KV-list eventual-consistency fix)
                    protocol: "joiner-initiated-multi-peer",
                    maxPeersPerSession: MAX_PEERS_PER_SESSION,
                    docs: "https://github.com/happytunesai/MidiEditor_AI/tree/main/cloudflare",
                });
            }

            return json(404, { error: "not found" });
        } catch (e) {
            return json(500, { error: String(e && e.message || e) });
        }
    },
};
