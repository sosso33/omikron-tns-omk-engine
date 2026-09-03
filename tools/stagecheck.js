/* Walk a whole conversation's poses through the /dialog page's OWN staging.
 *
 * Why this exists.  The player's staging in a scene dialog was wrong twice,
 * and neither miss was visible to anything in this repo: the numbers on the
 * server were right both times and the fault was in the client, one level past
 * where every test stopped.  The first attempt at the pelvis anchor was
 * reverted because it read the anchor off whichever pose meta happened to be
 * current, so the speaker floated through each spoken line and snapped back -
 * a fault that only exists ACROSS a pose change, and is invisible in any one
 * frame.  CLAUDE.md 1 has the rule: a value verified standing still is not
 * verified moving, and the invariant has to be written over the transition.
 *
 * So: run tools/omkweb.html's script under node with a DOM stub (the technique
 * CLAUDE.md 5 records for the empty-cutscene-list fault), fetch the real
 * server payloads, and call the page's own `stageMatrices` once per node of a
 * conversation - every line's .3DM pose, the scene idle, the rest pose - then
 * transform the posed body by the matrix it returns and report where the feet
 * and the crown actually land in the set.
 *
 * Nothing here re-derives the staging.  If it disagrees with the page, the
 * page is what is being measured.
 *
 *   node tools/stagecheck.js <port> <dialogId> <set>
 *
 * Prints one line per pose plus a verdict.  `verify.py: dialog staging` runs
 * the same conversations against the server side; this is the client half.
 */
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ARGS = process.argv.slice(2).filter(a => !a.startsWith('--'));
const FLOOR_MODE = process.argv.includes('--floor');   // the rule this replaced
const KEY0_MODE  = process.argv.includes('--key0');    // stage from root key 0
const DUMP = (process.argv.find(a => a.startsWith('--dump=')) || '').slice(7);
const PORT = ARGS[0] || '8752';
const DID  = parseInt(ARGS[1] || '387', 10);
const SET  = ARGS[2] || 'AResto14';
const BASE = `http://127.0.0.1:${PORT}`;

// ---------------------------------------------------------------- DOM stub
// The page touches the DOM at load: $(), addEventListener, ResizeObserver, a
// canvas for WebGL.  None of it matters to the staging, so it is all stubbed
// and `boot()` is never called - the harness sets the globals itself.
function stubEl(){
  const el = {
    style:{}, dataset:{}, classList:{add(){},remove(){},toggle(){}},
    children:[], value:"", textContent:"", innerHTML:"", className:"",
    checked:false, options:[], selectedIndex:0, width:640, height:480,
    appendChild(){}, removeChild(){}, addEventListener(){}, remove(){},
    setAttribute(){}, getAttribute(){return null;}, focus(){}, blur(){},
    querySelector(){return stubEl();}, querySelectorAll(){return [];},
    getBoundingClientRect(){return {width:640,height:480,left:0,top:0};},
    getContext(){return null;},                 // no GL: draw() is never run
    insertAdjacentHTML(){}, scrollIntoView(){}, play(){}, pause(){},
  };
  return el;
}

const sandbox = {
  console,
  document: {
    getElementById(){ return stubEl(); },
    querySelector(){ return stubEl(); },
    querySelectorAll(){ return []; },
    createElement(){ return stubEl(); },
    addEventListener(){},
    body: stubEl(), documentElement: stubEl(),
  },
  window: null,
  navigator: { userAgent: "node" },
  location: { href: BASE + "/dialog", search: "" },
  localStorage: { getItem(){return null;}, setItem(){}, removeItem(){} },
  addEventListener(){},
  requestAnimationFrame(){ return 0; },
  performance: { now: () => 0 },
  setTimeout, clearTimeout, setInterval, clearInterval,
  fetch: (...a) => fetch(...a),
  Image: function(){ return stubEl(); },
  Audio: function(){ return stubEl(); },
  ResizeObserver: function(){ return { observe(){}, disconnect(){} }; },
  Float32Array, Uint8Array, DataView, TextDecoder, Math, JSON, Set, Map,
  URL, URLSearchParams, devicePixelRatio: 1,
};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;

// ------------------------------------------------------- load the page's JS
const html = fs.readFileSync(path.join(__dirname, 'omkweb.html'), 'utf8');
const js = html.split('<script>')[1].split('</script>')[0]
               // boot() paints; the harness drives the globals instead
               .replace(/\nboot\(\);\s*$/, '\n');
vm.createContext(sandbox);
try {
  vm.runInContext(js, sandbox, { filename: 'omkweb.html' });
} catch (e) {
  console.error('the page script threw at load:', e.message);
  process.exit(2);
}
/* The page declares almost everything with `let`/`const`, and a top-level
 * lexical declaration in a vm script does NOT become a property of the context
 * object - it lives in the context's own lexical scope. So the harness reaches
 * the page's state by evaluating IN that scope rather than by poking the
 * sandbox object, and hands its inputs over through the one plain property it
 * does own. Reading `sandbox.stageMatrices` instead just reports it undefined,
 * which says nothing about the page. */
sandbox.__in = {};
const ev = (code) => vm.runInContext(code, sandbox, { filename: 'harness' });
for (const name of ['stageMatrices', 'mul', 'trans', 'RY', 'faceDir', 'MFLIP'])
  if (ev(`typeof ${name}`) === 'undefined') {
    console.error('the page does not define ' + name); process.exit(2);
  }

// ------------------------------------------------------------------ helpers
const get  = (u) => fetch(BASE + u).then(r => r.ok ? r.json() : null);
async function poseStream(u){
  const r = await fetch(BASE + u);
  if (!r.ok) return null;
  const b  = Buffer.from(await r.arrayBuffer());
  const hl = b.readUInt32LE(0);
  const meta = JSON.parse(b.slice(4, 4 + hl).toString('utf8'));
  const f = new Float32Array(b.buffer.slice(b.byteOffset + 4 + hl,
                                           b.byteOffset + b.length));
  return { meta, data: f };
}
function qrot(q, v){
  const w=q[0],x=q[1],y=q[2],z=q[3];
  const tx=2*(y*v[2]-z*v[1]), ty=2*(z*v[0]-x*v[2]), tz=2*(x*v[1]-y*v[0]);
  return [v[0]+w*tx+(y*tz-z*ty), v[1]+w*ty+(z*tx-x*tz), v[2]+w*tz+(x*ty-y*tx)];
}
// the posed body in MODEL space, exactly as cornerPos()/place() build it
let CENTRE = [0,0,0];        // mirrors the page's `centre`, set by recentre()
function posedPoints(g, pose, frame, centred){
  const out = [];
  const n = g.staticLocal ? g.staticLocal.length : 0;
  for (let i = 0; i < n; i++){
    const L = g.staticLocal[i], mi = g.staticMesh[i];
    let p;
    if (pose && L && mi >= 0){
      const f = Math.min(pose.meta.frames - 1, Math.max(0, frame));
      const b = (f * pose.meta.meshes + mi) * 7;
      const r = qrot(pose.data.slice(b, b+4), L);
      p = [r[0]+pose.data[b+4], r[1]+pose.data[b+5], r[2]+pose.data[b+6]];
    } else {
      const v = g.static[i]; p = [v[0], v[1], v[2]];
    }
    // the npc's buffers are uploaded minus `centre`; the player's are raw
    if (centred) p = [p[0]-CENTRE[0], p[1]-CENTRE[1], p[2]-CENTRE[2]];
    out.push(p);
  }
  return out;
}
const sub = (a, b) => [a[0]-b[0], a[1]-b[1], a[2]-b[2]];
const apply = (M, p) => [
  M[0]*p[0] + M[4]*p[1] + M[8] *p[2] + M[12],
  M[1]*p[0] + M[5]*p[1] + M[9] *p[2] + M[13],
  M[2]*p[0] + M[6]*p[1] + M[10]*p[2] + M[14]];
// the viewer's Y is up and the game's is down, so a viewer Y maps back with -
const gameY = (y) => -y;

// -------------------------------------------------------------------- main
(async () => {
  const conv  = await get(`/api/dialog/${DID}`);
  const place = await get(`/api/place/${DID}/${encodeURIComponent(SET)}`);
  if (!conv || !place || !place.npc){
    console.error('no conversation or no placement'); process.exit(2);
  }
  /* `--floor`: drop the anchors and let the page fall back to standing each
   * model on the floor under its position, which is what it did before. Not a
   * mode anyone wants - it is here so the checks below can be shown to have
   * teeth, since a test that also passes on the broken version tests nothing. */
  /* `--key0`: stage from the clip root's key 0 instead of the settled root -
   * the reading this replaced. Kept because the two are right for different
   * clips and telling them apart is an open question. */
  if (KEY0_MODE) for (const who of ['npc','player'])
    if (place[who + 'RootKey0']) place[who] = place[who + 'RootKey0'].slice();
  if (FLOOR_MODE) for (const who of ['npc','player']){
    delete place[who + 'Anchor'];
    if (place[who + 'FloorY'] != null) place[who][1] = place[who + 'FloorY'];
  }

  const npcModel = conv.actor && conv.actor.model;
  const geom  = await get(`/api/fullmodel/${npcModel}`);
  const pgeom = await get(`/api/fullmodel/HO1_FNM`);

  // the globals draw() would have set, assigned inside the page's own scope
  sandbox.__in = { geom, pgeom, place, conv };
  ev(`geom = __in.geom; modelAxis = geom.bodyAxis;
      pgeom = __in.pgeom; pbufs = [];        /* truthy: he has his own model */
      placement = __in.place; manual = null; useManual = false;
      conv = __in.conv;`);

  const si = conv.actor && conv.actor.sceneIdle;
  const pi = conv.actor && conv.actor.playerSceneIdle;
  const idle = si ? await poseStream(
      `/api/anipose/${npcModel}/${encodeURIComponent(si.scx)}/${si.anim}`) : null;
  const pidle = pi ? await poseStream(
      `/api/anipose/HO1_FNM/${encodeURIComponent(pi.scx)}/${pi.anim}`) : null;
  sandbox.__in.pfeet = pidle && pidle.meta.feet != null ? pidle.meta.feet : null;
  ev(`pfeet = __in.pfeet;`);

  /* The room's GROUND, not `FloorY`. The two differ the moment a speaker is
   * seated: the nearest surface below a seated pelvis is the stool (15.9 in
   * AResto14) where the ground is 31.7, and using the first reads a correctly
   * seated character as having sunk through the floor. */
  const floor = Math.max(place.npcGroundY == null ? -1e9 : place.npcGroundY,
                         place.playerGroundY == null ? -1e9 : place.playerGroundY);

  // every pose the conversation puts on the npc, in order: the scene idle it
  // opens on, then each node's own .3DM line, then back to the idle
  const poses = [['scene idle', idle]];
  for (const n of conv.nodes)
    if (n.asset) poses.push([n.asset, await poseStream(`/api/pose/${npcModel}/${n.asset}`)]);
  poses.push(['scene idle (again)', idle]);

  // `recentre()`: the client re-fits `centre` and `modelFeet` on every pose
  function recentre(pose){
    const pts = posedPoints(geom, pose, 0, false);
    let mn = [1e9,1e9,1e9], mx = [-1e9,-1e9,-1e9];
    for (const p of pts) for (let k=0;k<3;k++){
      if (p[k]<mn[k]) mn[k]=p[k]; if (p[k]>mx[k]) mx[k]=p[k]; }
    CENTRE = [(mn[0]+mx[0])/2,(mn[1]+mx[1])/2,(mn[2]+mx[2])/2];
    sandbox.__in.centre = CENTRE;
    sandbox.__in.modelHeight = Math.max(1, mx[1]-mn[1]);
    sandbox.__in.modelFeet = mx[1];
    ev(`centre = __in.centre; modelHeight = __in.modelHeight;
        modelFeet = __in.modelFeet;`);
  }

  const RAD = Math.PI/180;
  const ws  = ev(`worldSpeakers()`);
  const d   = [ws.player[0]-ws.npc[0], ws.player[2]-ws.npc[2]];
  const siYaw = si && si.rootYaw != null ? si.rootYaw*RAD : null;
  const pYaw  = pi && pi.rootYaw  != null ? pi.rootYaw *RAD : null;
  const fd = (x, z) => { sandbox.__in.fd = [x, z];
                         return ev(`faceDir(__in.fd[0], __in.fd[1])`); };
  const toPlayer = siYaw!=null ? fd(Math.sin(siYaw), -Math.cos(siYaw)) : fd(d[0], d[1]);
  const toNpc    = pYaw !=null ? fd(Math.sin(pYaw),  -Math.cos(pYaw))  : fd(-d[0], -d[1]);

  console.log(`dialog ${DID} "${conv.name}" in ${SET}  ` +
              `npc ${npcModel} (${place.npcAnchor}) / player HO1_FNM (${place.playerAnchor})`);
  console.log(`the room's floor under the pair: ${floor.toFixed(1)}\n`);
  console.log('  pose                        npc feet   npc crown | ' +
              'player feet  player crown |  npc centre');

  const npcFeet = [], plyFeet = [], npcCrown = [], plyCrown = [], pelvisAt = [];
  for (const [name, pose] of poses){
    sandbox.__in.poseData = pose ? pose.data : null;
    sandbox.__in.poseMeta = pose ? pose.meta : null;
    ev(`poseData = __in.poseData; poseMeta = __in.poseMeta;`);
    recentre(pose);
    sandbox.__in.ws = ws;
    sandbox.__in.toPlayer = toPlayer; sandbox.__in.toNpc = toNpc;
    const st = ev(`stageMatrices(__in.ws, __in.toPlayer, __in.toNpc)`);

    const nPts = posedPoints(geom,  pose,  0, true ).map(p => apply(st.M, p));
    const pPts = posedPoints(pgeom, pidle, Math.max(1, (pidle ? pidle.meta.frames : 2) >> 1),
                             false).map(p => apply(st.Mplayer, p));
    const lo = (a) => gameY(Math.min(...a.map(p => p[1])));   // feet: lowest
    const hi = (a) => gameY(Math.max(...a.map(p => p[1])));   // crown: highest
    npcFeet.push(lo(nPts)); plyFeet.push(lo(pPts));
    npcCrown.push(hi(nPts)); plyCrown.push(hi(pPts));
    /* Where the PELVIS itself landed. It is the hierarchy root, so its posed
     * position is its rest position in every pose, and under the pelvis anchor
     * the matrix has to drop it exactly on the placement - that is what the
     * anchor MEANS, and it is exact rather than a tolerance on a body part. */
    pelvisAt.push([
      apply(st.M, sub(geom.pelvis, CENTRE)),
      apply(st.Mplayer, pgeom.pelvis)]);
    /* `npc centre` is `centre[1]`, the bounding-box height recentre() re-fits
     * on every pose. It is printed because it used to LEAK INTO THE PLAYER:
     * his matrix was built from the npc's onAxis, so he was displaced by this
     * number - 18.1 to 24.0 units down into the floor here - and moved by up
     * to 5.9 of them whenever it changed. Under the fix his column is flat
     * while this one is not, which is the second half of the staging bug shown
     * rather than claimed. */
    console.log(`  ${name.padEnd(24)} ${lo(nPts).toFixed(1).padStart(9)} ` +
                `${hi(nPts).toFixed(1).padStart(11)} | ` +
                `${lo(pPts).toFixed(1).padStart(11)} ${hi(pPts).toFixed(1).padStart(13)} | ` +
                `${CENTRE[1].toFixed(1).padStart(10)}`);
  }

  const span = (a) => Math.max(...a) - Math.min(...a);
  const fail = [];
  /* 0. THE ANCHOR ITSELF. Where the placement says "pelvis", the staged pelvis
   *    must land on that point exactly, in every one of the conversation's
   *    poses. Everything else here is a consequence; this is the definition,
   *    and it is the check that fails the moment the lift starts following the
   *    pose again - which is how the first attempt at this went wrong. */
  for (const [i, [np, pp]] of pelvisAt.entries())
    for (const [who, got, want, anchor] of
         [['npc', np, ws.npc, place.npcAnchor],
          ['player', pp, ws.player, place.playerAnchor]]){
      if (anchor !== 'pelvis') continue;
      const off = Math.max(Math.abs(got[0]-want[0]), Math.abs(got[1]+want[1]),
                           Math.abs(got[2]-want[2]));
      if (off > 0.01){ fail.push(
          `${poses[i][0]}: the ${who}'s pelvis staged ${off.toFixed(1)} units ` +
          `off its placement`); break; }
    }
  // 1. the whole point: nothing about the PLAYER may move when the npc's pose
  //    changes.  This is the invariant the reverted attempt broke.
  if (span(plyFeet) > 0.01) fail.push(
      `the player moved ${span(plyFeet).toFixed(1)} units across the ` +
      `conversation's ${poses.length} poses - the anchor is following the pose again`);
  // 2. the npc must not jump between her scene idle and a spoken line either
  const idleFeet = npcFeet[0], lastIdle = npcFeet[npcFeet.length-1];
  if (Math.abs(idleFeet - lastIdle) > 0.01) fail.push(
      `the npc's idle staged at ${idleFeet.toFixed(1)} at the start and ` +
      `${lastIdle.toFixed(1)} at the end`);
  // 3. neither body may sink through the room's floor (game Y grows downward,
  //    so "below the floor" is a LARGER y)
  /* Three units, matching `verify.py: dialog staging sweep`, which measured
   * the whole corpus at a median 0.5 off the ground with 27 of 50 inside 3.
   * A posed foot is not a flat plane and the sole geometry hangs below the
   * mesh origin, so a correctly placed body reads a unit or two under. */
  for (const [who, f] of [['npc', npcFeet[0]], ['player', plyFeet[0]]])
    if (f > floor + 3) fail.push(
        `the ${who}'s feet land at ${f.toFixed(1)}, through a floor of ${floor.toFixed(1)}`);
  /* 4. the two heads.  Both speakers are staged by the same authored scene -
   *    in 387 they are eating at one table - so their crowns have to agree to
   *    about the lean of a body.  This is the check the FLOOR rule fails: it
   *    stands each of them on whatever surface its own x/z happens to sit over,
   *    and in AResto14 that is the bench (15.9) for Telis and the floor (31.7)
   *    for Kay'l, so it seats one of them 16 units - most of a head and neck -
   *    below the other. */
  /* Five units, not one: the two are DIFFERENT models with different
   * proportions, and the anchor is the pelvis, so a crown gap of a few units
   * is the models themselves. In Aapkayl TEL_FNM's crown sits 26.0 above her
   * pelvis and HO1_FNM's 29.0 above his, and the scene puts her pelvis 3 units
   * higher than his - so 3.1 units of head gap there is right, and 15.2 in
   * AResto14 under the floor rule is not. */
  const heads = Math.abs(npcCrown[0] - plyCrown[0]);
  if (heads > 5) fail.push(
      `the two speakers' heads are ${heads.toFixed(1)} units apart at the ` +
      `scene idle (npc ${npcCrown[0].toFixed(1)}, player ${plyCrown[0].toFixed(1)})`);

  /* `--dump=<file>`: hand the page's own matrices to a renderer, so the
   * picture that gets looked at is drawn with what the viewer computes rather
   * than with a second copy of the recipe. tools/stagerender.py reads it. */
  if (DUMP){
    const st = ev(`stageMatrices(__in.ws, __in.toPlayer, __in.toNpc)`);
    fs.writeFileSync(DUMP, JSON.stringify({
      dialog: DID, set: SET, npcModel, ws, place,
      centre: CENTRE, floorMode: FLOOR_MODE,
      M: st.M, Mplayer: st.Mplayer,
      npcLift: st.npcLift, plyLift: st.plyLift,
      npcClip: si && {scx: si.scx, anim: si.anim, clip: si.clip},
      plyClip: pi && {scx: pi.scx, anim: pi.anim, clip: pi.clip},
      plyFrame: Math.max(1, (pidle ? pidle.meta.frames : 2) >> 1),
    }, null, 1));
    console.log('wrote ' + DUMP);
  }

  console.log('');
  if (fail.length){ for (const f of fail) console.log('FAIL  ' + f); process.exit(1); }
  console.log(`ok - the player is fixed to ${plyFeet[0].toFixed(1)} through all ` +
              `${poses.length} poses, the npc to ${npcFeet[0].toFixed(1)}, ` +
              `on a floor of ${floor.toFixed(1)}`);
  process.exit(0);
})();
