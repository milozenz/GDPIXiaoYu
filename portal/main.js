/* 广理职小鱼 门户页的动画和交互。不依赖任何外部库；数据来自 data.js（window.XY）。 */
(function () {
  'use strict';

  var D = window.XY;
  var REDUCE = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var PI = Math.PI;

  function $(s, r) { return (r || document).querySelector(s); }
  function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }
  function seg(t, a, b) { return clamp((t - a) / (b - a), 0, 1); }
  function ease(t) { return t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2; }
  function easeOut(t) { return 1 - Math.pow(1 - t, 3); }
  function easeBack(t) { var c = 1.70158; return 1 + (c + 1) * Math.pow(t - 1, 3) + c * Math.pow(t - 1, 2); }
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }
  function onView(node, fn, ratio) {
    if (!('IntersectionObserver' in window)) { fn(); return; }
    var io = new IntersectionObserver(function (es) {
      if (es[0].isIntersecting) { io.disconnect(); fn(); }
    }, { threshold: ratio || 0.35 });
    io.observe(node);
  }
  function restart(node, cls) {  // 重新播放一个 CSS 动画
    node.classList.remove(cls);
    void node.offsetWidth;
    node.classList.add(cls);
  }

  /* ================= 折纸鱼的模型（就是标志的 6 块三角形，原点在身体中间的十字交点） ================= */
  var P = { A: [-22, 0], B: [0, -20.5], D: [0, 17.5], C: [15.5, -1], O: [0, 0], T1: [29, -16], M: [23.5, -1], T2: [29, 14] };
  var TRI = [['A', 'B', 'O'], ['A', 'O', 'D'], ['B', 'C', 'O'], ['O', 'C', 'D'], ['C', 'T1', 'M'], ['C', 'M', 'T2']];
  var MX = 3.5, MY = -1.5;  // 鱼的外框中心
  var EYE = [-12.2, -3];
  var PAL = {
    logo: [[255, 232, 92], [255, 210, 31], [255, 194, 26], [255, 168, 0], [255, 138, 31], [255, 106, 20]],
    cream: [[255, 253, 240], [247, 243, 224], [239, 234, 210], [229, 223, 194], [255, 138, 31], [255, 106, 20]],
    ink: [[62, 62, 62], [31, 31, 31], [46, 46, 46], [18, 18, 18], [255, 138, 31], [255, 106, 20]],
    orange: [[255, 164, 76], [255, 138, 31], [255, 122, 26], [255, 106, 20], [31, 31, 31], [62, 62, 62]]
  };
  var BACK = [[255, 251, 234], [249, 243, 219], [243, 236, 207], [237, 229, 196], [249, 243, 219], [243, 236, 207]];  // 纸的背面
  function rgb(c, k) { return 'rgb(' + Math.round(clamp(c[0] * k, 0, 255)) + ',' + Math.round(clamp(c[1] * k, 0, 255)) + ',' + Math.round(clamp(c[2] * k, 0, 255)) + ')'; }

  /* ================= 首屏 ================= */
  function hero() {
    var sec = $('#top'), cv = $('#sea'), ctx = cv.getContext('2d');
    var W = 0, H = 0, dpr = 1, L = {};
    var ptr = { x: 0, y: 0, on: false };
    var school = [], start = performance.now(), last = start, raf = 0, live = true;

    function layout() {
      var r = sec.getBoundingClientRect();
      W = Math.round(r.width); H = Math.round(r.height);
      dpr = Math.min(window.devicePixelRatio || 1, 2);
      cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr);
      var wide = W > 860, t = $('.hero-in').getBoundingClientRect();
      if (wide) { L.cx = W * 0.735; L.cy = H * 0.52; L.rd = Math.min(W * 0.265, H * 0.44); }
      else {                              // 窄屏：黑纸放在顶栏和标题之间，不压到字
        var room = t.top - r.top - 64;
        L.cx = W * 0.5; L.cy = 64 + room / 2; L.rd = Math.max(60, Math.min(W * 0.44, room / 2 - 10));
      }
      L.s = L.rd * 1.16 / 51;            // 每个模型单位多少像素
      L.text = { x0: t.left - r.left - 20, y0: t.top - r.top - 20, x1: t.right - r.left + 30, y1: t.bottom - r.top + 20 };
      L.k = clamp(W / 1440, 0.72, 1.25);
      var n = Math.round(clamp(W * H / 62000, 8, 24));
      while (school.length < n) school.push(spawn(school.length));
      school.length = n;
    }

    function spawn(i) {
      var kinds = ['cream', 'cream', 'ink', 'cream', 'orange', 'cream', 'ink'];
      var size = 0.55 + ((i * 37) % 10) / 10 * 0.95;   // 0.55 – 1.5
      return {
        x: -60 - Math.random() * Math.max(W, 600) * 0.9, y: i % 2 ? 90 + Math.random() * H * 0.16 : H * (0.76 + Math.random() * 0.16),
        vx: 60 + Math.random() * 40, vy: (Math.random() - 0.5) * 20,
        a: 0, ph: Math.random() * 6, s: size, v0: 46 + size * 26 + Math.random() * 18,
        pal: PAL[kinds[i % kinds.length]], inkEye: kinds[i % kinds.length] !== 'ink', w: Math.random() * 6
      };
    }

    function step(dt) {
      var i, j, f, g, dx, dy, d, n = school.length;
      for (i = 0; i < n; i++) {
        f = school[i];
        var ax = 0, ay = 0, cxs = 0, cys = 0, vxs = 0, vys = 0, c = 0;
        for (j = 0; j < n; j++) {
          if (i === j) continue;
          g = school[j]; dx = g.x - f.x; dy = g.y - f.y; d = dx * dx + dy * dy;
          if (d < 170 * 170) {
            cxs += g.x; cys += g.y; vxs += g.vx; vys += g.vy; c++;
            if (d < 84 * 84 && d > 0.01) { d = Math.sqrt(d); ax -= dx / d * (84 - d) * 4.5; ay -= dy / d * (84 - d) * 4.5; }
          }
        }
        if (c) {
          ax += (cxs / c - f.x) * 0.05 + (vxs / c - f.vx) * 0.5;
          ay += (cys / c - f.y) * 0.05 + (vys / c - f.vy) * 0.5;
        }
        var t = L.text, overText = ptr.x > t.x0 && ptr.x < t.x1 && ptr.y > t.y0 && ptr.y < t.y1;
        if (ptr.on && !overText) {          // 跟着鼠标，但不贴上去；鼠标在标题上时不跟
          dx = ptr.x - f.x; dy = ptr.y - f.y; d = Math.sqrt(dx * dx + dy * dy) || 1;
          if (d < 460) { var k = d > 130 ? 70 : -150; ax += dx / d * k; ay += dy / d * k; }
        }
        var lx = f.x + f.vx * 0.9, ly = f.y + f.vy * 0.9;   // 绕开标题：现在在里面，或者马上要游进去
        if ((f.x > t.x0 && f.x < t.x1 && f.y > t.y0 && f.y < t.y1) || (lx > t.x0 && lx < t.x1 && ly > t.y0 && ly < t.y1))
          ay += (f.y < (t.y0 + t.y1) / 2 ? -1 : 1) * 420;
        f.w += (Math.random() - 0.5) * dt * 3;
        ax += Math.cos(f.w) * 16; ay += Math.sin(f.w) * 16;
        if (f.y < 90) ay += (90 - f.y) * 2.2;
        if (f.y > H - 70) ay -= (f.y - (H - 70)) * 2.2;
        ax += (f.vx >= 0 ? 1 : -1) * 10;     // 更愿意横着游
        f.vx += ax * dt; f.vy += ay * dt;
        f.vy *= 1 - 0.6 * dt;
        var sp = Math.sqrt(f.vx * f.vx + f.vy * f.vy) || 1, lim = clamp(sp, f.v0 * 0.7, f.v0 * 2.4);
        f.vx = f.vx / sp * lim; f.vy = f.vy / sp * lim;
        f.x += f.vx * dt; f.y += f.vy * dt;
        if (f.x > W + 90) f.x = -80; else if (f.x < -90) f.x = W + 80;
        var want = Math.atan2(f.vy, f.vx), da = want - f.a;
        while (da > PI) da -= 2 * PI;
        while (da < -PI) da += 2 * PI;
        f.a += da * Math.min(1, dt * 6);
        f.ph += dt * (5 + lim * 0.085);
      }
    }

    function small(f) {
      var sc = (0.5 + f.s * 0.72) * L.k, co = Math.cos(f.a), si = Math.sin(f.a), fl = co < 0 ? -1 : 1;
      var ta = Math.sin(f.ph) * 0.85, tc = Math.cos(ta), bend = Math.cos(Math.sin(f.ph - 0.9) * 0.4);
      function map(p, tail, ox, oy) {
        var x = p[0] > 0 ? p[0] * bend : p[0];
        if (tail) x = P.C[0] * bend + (p[0] - P.C[0]) * tc;
        x = -(x - MX) * sc; var y = (p[1] - MY) * sc * fl;
        return [f.x + x * co - y * si + ox, f.y + x * si + y * co + oy];
      }
      var pass, i, k, q, t;
      for (pass = 0; pass < 2; pass++) {    // 先画影子，再画鱼
        for (i = 0; i < 6; i++) {
          t = TRI[i];
          ctx.beginPath();
          for (k = 0; k < 3; k++) {
            q = map(P[t[k]], i >= 4 && t[k] !== 'C', pass ? 0 : sc * 1.6, pass ? 0 : sc * 2.2);
            if (k) ctx.lineTo(q[0], q[1]); else ctx.moveTo(q[0], q[1]);
          }
          ctx.closePath();
          ctx.fillStyle = pass ? rgb(f.pal[i], i >= 4 ? 1 + 0.16 * Math.sin(ta) : 1) : 'rgba(31,31,31,.13)';
          ctx.fill();
        }
      }
      q = map(EYE, false, 0, 0);
      ctx.beginPath(); ctx.arc(q[0], q[1], 2.3 * sc, 0, 2 * PI);
      ctx.fillStyle = f.inkEye ? '#1F1F1F' : '#FFE033'; ctx.fill();
    }

    /* ---- 大鱼：一张叠好的纸一步步展开 ---- */
    function norm(v) { var n = Math.sqrt(v[0] * v[0] + v[1] * v[1]); return [v[0] / n, v[1] / n]; }
    var FOLD = [null, { a: P.A, u: [1, 0] }, { a: P.O, u: [0, -1] }, { a: P.O, u: norm(P.C) }, { a: P.C, u: [0, -1] }, { a: P.C, u: [1, 0] }];
    var CHAIN = [[], [1], [2], [3, 2], [4, 2], [5, 4, 2]];
    function rot(p, f, phi) {
      if (!phi) return p;
      var a = f.a, u = f.u, vx = p[0] - a[0], vy = p[1] - a[1], vz = p[2];
      var c = Math.cos(phi), s = Math.sin(phi), d = u[0] * vx + u[1] * vy;
      return [a[0] + vx * c + u[1] * vz * s + u[0] * d * (1 - c), a[1] + vy * c - u[0] * vz * s + u[1] * d * (1 - c), vz * c + (u[0] * vy - u[1] * vx) * s];
    }

    function big(T, now) {
      var sd = easeBack(seg(T, 0, 0.55)), cx = L.cx, cy = L.cy, rd = L.rd * sd, i, k;
      var px = ptr.on ? clamp((ptr.x - cx) / W, -0.5, 0.5) : 0, py = ptr.on ? clamp((ptr.y - cy) / H, -0.5, 0.5) : 0;
      // 黑色的纸（带硬阴影和折痕）
      ctx.save();
      ctx.translate(cx, cy); ctx.rotate((1 - sd) * -0.7);
      ctx.fillStyle = '#E2BA00';
      ctx.beginPath(); ctx.moveTo(14, -rd + 16); ctx.lineTo(rd + 14, 16); ctx.lineTo(14, rd + 16); ctx.lineTo(-rd + 14, 16); ctx.closePath(); ctx.fill();
      ctx.fillStyle = '#1F1F1F';
      ctx.beginPath(); ctx.moveTo(0, -rd); ctx.lineTo(rd, 0); ctx.lineTo(0, rd); ctx.lineTo(-rd, 0); ctx.closePath(); ctx.fill();
      ctx.strokeStyle = 'rgba(255,224,51,.16)'; ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.moveTo(0, -rd); ctx.lineTo(0, rd); ctx.moveTo(-rd, 0); ctx.lineTo(rd, 0);
      ctx.moveTo(-rd / 2, -rd / 2); ctx.lineTo(rd / 2, rd / 2); ctx.moveTo(-rd / 2, rd / 2); ctx.lineTo(rd / 2, -rd / 2); ctx.stroke();
      ctx.restore();
      if (T < 0.25) return;

      var pk = easeOut(seg(T, 0.25, 0.62)), idle = seg(T, 2.35, 3.1), tt = now / 1000;
      var ph = [0,
        PI * (1 - ease(seg(T, 0.62, 1.02))),
        PI * (1 - ease(seg(T, 0.97, 1.42))) + idle * 0.11 * Math.sin(tt * 3 - 1),
        PI * (1 - ease(seg(T, 1.37, 1.72))),
        PI * (1 - ease(seg(T, 1.67, 2.02))) + idle * 0.46 * Math.sin(tt * 3),
        PI * (1 - ease(seg(T, 1.97, 2.27)))];
      var S = L.s * (1 + (1 - pk) * 0.9);
      var th = (1 - pk) * -0.55 + idle * (0.035 * Math.sin(tt * 1.1) + py * 0.16);
      var ox = cx + idle * px * 26, oy = cy + idle * (Math.sin(tt * 1.3) * 5 + py * 14);
      var ct = Math.cos(th), st = Math.sin(th), F = 240;
      function proj(p) {
        var kk = F / (F - p[2]), x = (p[0] - MX) * kk, y = (p[1] - MY) * kk;
        return [ox + (x * ct - y * st) * S, oy + (x * st + y * ct) * S];
      }
      var tris = [];
      for (i = 0; i < 6; i++) {
        var v = [], q = [];
        for (k = 0; k < 3; k++) {
          var p = [P[TRI[i][k]][0], P[TRI[i][k]][1], 0], ch = CHAIN[i], c;
          for (c = 0; c < ch.length; c++) p = rot(p, FOLD[ch[c]], ph[ch[c]]);
          v.push(p); q.push(proj(p));
        }
        var ux = v[1][0] - v[0][0], uy = v[1][1] - v[0][1], uz = v[1][2] - v[0][2];
        var wx = v[2][0] - v[0][0], wy = v[2][1] - v[0][1], wz = v[2][2] - v[0][2];
        var nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
        var o0 = P[TRI[i][0]], o1 = P[TRI[i][1]], o2 = P[TRI[i][2]];
        var orig = (o1[0] - o0[0]) * (o2[1] - o0[1]) - (o1[1] - o0[1]) * (o2[0] - o0[0]);
        tris.push({ q: q, front: nz * orig >= 0, lit: Math.abs(nz) / (Math.sqrt(nx * nx + ny * ny + nz * nz) || 1) });
      }
      var tail = ph[4] > PI / 2 ? [5, 4] : [4, 5];
      var R = ph[2] > PI / 2 ? [3].concat(tail.slice().reverse(), [2]) : [2].concat(tail, [3]);
      var order = ph[2] > PI - 0.001 ? [0].concat(R, [1]) : [0, 1].concat(R);
      ctx.globalAlpha = pk;
      ctx.lineJoin = 'round';
      for (k = 0; k < 6; k++) {
        i = order[k];
        var t = tris[i];
        ctx.beginPath(); ctx.moveTo(t.q[0][0], t.q[0][1]); ctx.lineTo(t.q[1][0], t.q[1][1]); ctx.lineTo(t.q[2][0], t.q[2][1]); ctx.closePath();
        ctx.fillStyle = t.front ? rgb(PAL.logo[i], 0.74 + 0.26 * t.lit) : rgb(BACK[i], 0.8 + 0.2 * t.lit);
        ctx.fill();
        ctx.strokeStyle = t.front ? ctx.fillStyle : 'rgba(31,31,31,.22)';
        ctx.lineWidth = t.front ? 0.8 : 1;
        ctx.stroke();
      }
      ctx.globalAlpha = 1;
      var ey = easeBack(seg(T, 2.22, 2.52));
      if (ey > 0) {
        var blink = (tt % 4.6) > 4.48 ? 0.15 : 1;
        var e = proj([EYE[0] + idle * px * 1.6, EYE[1] + idle * py * 1.6, 0]);
        ctx.save(); ctx.translate(e[0], e[1]); ctx.rotate(th); ctx.scale(1, blink);
        ctx.beginPath(); ctx.arc(0, 0, 2.5 * S * ey, 0, 2 * PI); ctx.fillStyle = '#1F1F1F'; ctx.fill();
        ctx.restore();
      }
    }

    function draw(now) {
      var T = REDUCE ? 99 : (now - start) / 1000;
      var px = ptr.on ? (ptr.x / W - 0.5) * 16 : 0, py = ptr.on ? (ptr.y / H - 0.5) * 12 : 0, i;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.fillStyle = '#FFE033'; ctx.fillRect(0, 0, W, H);
      // 纸面的明暗：几块大三角
      function facet(c, a, b, d) { ctx.fillStyle = c; ctx.beginPath(); ctx.moveTo(a[0] + px, a[1] + py); ctx.lineTo(b[0] + px, b[1] + py); ctx.lineTo(d[0] + px, d[1] + py); ctx.closePath(); ctx.fill(); }
      facet('#FFE85C', [-40, -40], [W * 0.36, -40], [-40, H * 0.56]);
      facet('#FFD21F', [-40, H + 40], [W * 0.5, H + 40], [-40, H * 0.66]);
      facet('#FFD21F', [W + 40, -40], [W + 40, H * 0.42], [W * 0.74, -40]);
      facet('#FFE85C', [W + 40, H + 40], [W * 0.62, H + 40], [W + 40, H * 0.72]);
      if (T > 0.9 || REDUCE) for (i = 0; i < school.length; i++) small(school[i]);
      big(T, now);
    }

    function frame(now) {
      raf = 0;
      var dt = Math.min(0.05, (now - last) / 1000); last = now;
      if ((now - start) / 1000 > 0.9) step(dt);
      draw(now);
      if (live) raf = requestAnimationFrame(frame);
    }
    function wake() { if (!raf && live && !REDUCE) { last = performance.now(); raf = requestAnimationFrame(frame); } }

    function at(e) { var r = cv.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }
    sec.addEventListener('pointermove', function (e) { var p = at(e); ptr.x = p[0]; ptr.y = p[1]; ptr.on = e.pointerType !== 'touch'; });
    sec.addEventListener('pointerleave', function () { ptr.on = false; });
    cv.addEventListener('click', function (e) {
      var p = at(e), i, f, dx, dy, d;
      if (Math.abs(p[0] - L.cx) + Math.abs(p[1] - L.cy) < L.rd) { start = performance.now() - 250; return; }   // 点黑纸：重新折一遍
      for (i = 0; i < school.length; i++) {           // 点别处：鱼群散开
        f = school[i]; dx = f.x - p[0]; dy = f.y - p[1]; d = Math.sqrt(dx * dx + dy * dy) || 1;
        if (d < 320) { f.vx += dx / d * 520; f.vy += dy / d * 520; }
      }
    });
    window.addEventListener('resize', function () { layout(); if (REDUCE) draw(performance.now()); });
    if ('IntersectionObserver' in window) {
      new IntersectionObserver(function (es) { live = es[0].isIntersecting; wake(); }).observe(sec);
    }
    document.addEventListener('visibilitychange', function () { live = !document.hidden; wake(); });

    layout();
    if (REDUCE) {
      school.forEach(function (f, i) { f.x = W * ((i * 0.37) % 1); f.vx = i % 2 ? 60 : -60; f.a = i % 2 ? 0 : PI; });
      draw(performance.now());
    } else wake();
  }

  /* ================= 顶栏和侧边的小鱼 ================= */
  function chrome() {
    var bar = $('#topbar'), heroEl = $('#top'), fish = $('#swimmer'), lastY = 0, dir = 1, tick = false;
    function upd() {
      tick = false;
      var y = window.scrollY, hh = heroEl.offsetHeight, max = document.documentElement.scrollHeight - window.innerHeight;
      bar.classList.toggle('solid', y > hh - 70);
      if (Math.abs(y - lastY) > 2) dir = y > lastY ? 1 : -1;
      lastY = y;
      fish.classList.toggle('on', y > hh * 0.7);
      var p = clamp((y - hh * 0.7) / Math.max(1, max - hh * 0.7), 0, 1);
      fish.style.transform = 'translateY(' + Math.round(84 + p * (window.innerHeight - 150)) + 'px) rotate(' + (dir > 0 ? -90 : 90) + 'deg)';
    }
    window.addEventListener('scroll', function () { if (!tick) { tick = true; requestAnimationFrame(upd); } }, { passive: true });
    upd();
  }

  /* ================= 01 防超卖：21 轮实测记录 ================= */
  function race() {
    var lanes = $('#raceLanes'), axis = $('#raceAxis'), go = $('#raceGo'), cnt = $('#raceCount'), dots = $('#raceRounds');
    var layers = $('#raceLayers').children, total = D.rounds.length, idx = -1, raf = 0, i;
    for (i = 0; i < total; i++) dots.appendChild(el('i'));

    function build(round) {
      var max = 0, k;
      round.forEach(function (r) { if (r[2] > max) max = r[2]; });
      var stepMs = max <= 5 ? 1 : max <= 10 ? 2 : 5, n = Math.max(4, Math.ceil(max / stepMs)), span = n * stepMs;
      lanes.textContent = ''; axis.textContent = '';
      lanes.style.setProperty('--n', n);
      var rows = round.map(function (r) {
        var lane = el('div', 'lane' + (r[3] ? ' won' : '')), track = el('div', 'track'), run = el('div', 'run');
        var bar = el('div', 'bar'), tag = el('div', 'tag', r[3] ? '成交' : '409');
        bar.style.left = r[1] / span * 100 + '%';
        tag.style.left = r[2] / span * 100 + '%';
        run.appendChild(bar); run.appendChild(tag); track.appendChild(run);
        lane.appendChild(el('b', '', D.names[r[0]])); lane.appendChild(track); lanes.appendChild(lane);
        return { lane: lane, bar: bar, sent: r[1], recv: r[2] };
      });
      var box = el('div');
      for (k = 0; k <= n; k++) { var s = el('span', '', k * stepMs + ' ms'); s.style.left = k / n * 100 + '%'; box.appendChild(s); }
      axis.appendChild(box);
      return { rows: rows, span: span, max: max };
    }

    function play() {
      cancelAnimationFrame(raf);
      idx = (idx + 1) % total;
      var b = build(D.rounds[idx]), t0 = performance.now(), dur = REDUCE ? 1 : 2400, end = Math.min(b.span, b.max * 1.08);
      cnt.textContent = (idx + 1) + ' / ' + total;
      go.textContent = '再来一轮';
      for (var j = 0; j < total; j++) dots.children[j].classList.toggle('cur', j === idx);
      (function tickFn(now) {
        var p = clamp((now - t0) / dur, 0, 1), ms = p * end;
        b.rows.forEach(function (r) {
          r.bar.style.width = (clamp(ms, r.sent, r.recv) - r.sent) / b.span * 100 + '%';
          if (ms >= r.recv) r.lane.classList.add('done');
        });
        for (var q = 0; q < 3; q++) layers[q].classList.toggle('on', p < 1 && Math.min(2, Math.floor(p * 3)) === q);
        if (p < 1) raf = requestAnimationFrame(tickFn);
        else { dots.children[idx].classList.remove('cur'); dots.children[idx].classList.add('on'); }
      })(t0);
    }
    go.addEventListener('click', play);
    build(D.rounds[0]);
    onView($('#race .panel'), play);
  }

  /* ================= 02 担保交易：和服务器一样的状态规则 ================= */
  function escrow() {
    var PRICE = 3800, st, bal, shown = { buyer: 5000, hold: 0, seller: 0 }, raf = 0;
    var accts = $('#escAccounts'), bill = $('#escBill'), acts = $('#escActs'), tagEl = $('#escGoods em'), states = $('#escStates').children;
    var NAME = { on_sale: '在售', paid: '已付款', shipped: '已发货', completed: '已完成', cancelled: '已取消' };
    var FLOW = ['on_sale', 'paid', 'shipped', 'completed'];
    var ACTS = {
      on_sale: [['下单', 'pay']],
      paid: [['卖家发货', 'ship'], ['取消订单', 'cancel', 1]],
      shipped: [['确认收货', 'done']],
      completed: [['再来一次', 'reset', 1]],
      cancelled: [['再来一次', 'reset', 1]]
    };
    function reset() { st = 'on_sale'; bal = { buyer: 5000, hold: 0, seller: 0 }; }
    function act(a) {
      if (a === 'pay' && st === 'on_sale') { bal.buyer -= PRICE; bal.hold += PRICE; st = 'paid'; }
      else if (a === 'ship' && st === 'paid') st = 'shipped';
      else if (a === 'done' && st === 'shipped') { bal.hold -= PRICE; bal.seller += PRICE; st = 'completed'; }
      else if (a === 'cancel' && st === 'paid') { bal.hold -= PRICE; bal.buyer += PRICE; st = 'cancelled'; }
      else if (a === 'reset') reset();
      render();
    }
    function render() {
      var i, at = FLOW.indexOf(st);
      for (i = 0; i < states.length; i++) {
        var s = states[i].getAttribute('data-s'), fi = FLOW.indexOf(s);
        states[i].classList.toggle('on', s === st);
        states[i].classList.toggle('past', st === 'cancelled' ? fi === 0 || fi === 1 : fi >= 0 && fi < at);
      }
      tagEl.textContent = NAME[st]; tagEl.className = st;
      bill.style.left = (bal.hold ? 50 : bal.seller ? 83.333 : 16.667) + '%';
      acts.textContent = '';
      ACTS[st].forEach(function (a) {
        var b = el('button', 'btn' + (a[2] ? ' ghost' : ''), a[0]);
        b.type = 'button';
        b.addEventListener('click', function () { act(a[1]); });
        acts.appendChild(b);
      });
      cancelAnimationFrame(raf);
      var from = { buyer: shown.buyer, hold: shown.hold, seller: shown.seller }, t0 = performance.now();
      (function tw(now) {
        var p = REDUCE ? 1 : ease(clamp((now - t0) / 700, 0, 1));
        ['buyer', 'hold', 'seller'].forEach(function (k) {
          shown[k] = from[k] + (bal[k] - from[k]) * p;
          $('[data-k="' + k + '"] b', accts).textContent = '¥' + Math.round(shown[k]);
        });
        if (p < 1) raf = requestAnimationFrame(tw);
      })(t0);
    }
    reset(); render();
  }

  /* ================= 03 实时推送：把一句话装成一个包 ================= */
  function push() {
    var form = $('#pushForm'), input = $('#pushText'), bytes = $('#pushBytes').children, lenEl = $('#pushLen');
    var pre = $('#pushJson'), pkt = $('#pushPkt'), inbox = $('#pushInbox');
    var tpl = JSON.parse(D.frame), first = tpl.data.content, enc = window.TextEncoder ? new TextEncoder() : null;
    function size(s) { return enc ? enc.encode(s).length : unescape(encodeURIComponent(s)).length; }
    function show(flip) {
      var text = input.value.trim() || first, i;
      tpl.data.content = text;
      var body = JSON.stringify(tpl), n = size(body), hex = ('00000000' + n.toString(16).toUpperCase()).slice(-8);
      for (i = 0; i < 4; i++) {
        var h = hex.substr(i * 2, 2);
        if (bytes[i].textContent !== h) { bytes[i].textContent = h; if (flip) restart(bytes[i], 'flip'); }
      }
      lenEl.textContent = '= ' + n + ' 字节';
      var key = '"content":', a = body.indexOf(key) + key.length, q = JSON.stringify(text);
      pre.textContent = '';
      pre.appendChild(document.createTextNode(body.slice(0, a)));
      pre.appendChild(el('mark', '', q));
      pre.appendChild(document.createTextNode(body.slice(a + q.length)));
      return text;
    }
    function send() {
      var text = show(true), i;
      for (i = 0; i < 4; i++) (function (b, k) { setTimeout(function () { restart(b, 'flip'); }, k * 70); })(bytes[i], i);
      restart(pkt, 'go');
      setTimeout(function () {
        inbox.insertBefore(el('div', 'msg', text), inbox.firstChild);
        while (inbox.children.length > 3) inbox.removeChild(inbox.lastChild);
      }, REDUCE ? 0 : 760);
    }
    input.value = first;
    input.addEventListener('input', function () { show(true); });
    form.addEventListener('submit', function (e) { e.preventDefault(); send(); });
    show(false);
    onView($('#push .panel'), send);
  }

  /* ================= 04 内容安全：真实调用的结果 ================= */
  function safety() {
    var list = $('#aiCases'), bars = $('#aiBars'), stamp = $('#aiStamp'), btns = [];
    D.cases.forEach(function (c, i) {
      var b = el('button', 'case');
      b.type = 'button';
      b.appendChild(el('i', '', c.kind)); b.appendChild(el('span', '', c.text));
      b.addEventListener('click', function () { pick(i); });
      list.appendChild(b); btns.push(b);
    });
    function pick(i) {
      var c = D.cases[i], max = 0;
      btns.forEach(function (b, k) { b.classList.toggle('on', k === i); });
      c.opts.forEach(function (o) { if (o[1] > max) max = o[1]; });
      bars.textContent = '';
      var fills = c.opts.map(function (o, k) {
        var row = el('div', 'bar-row' + (k > 0 && o[1] >= 0.9 ? ' hit' : '') + (o[1] === max ? ' max' : ''));
        var t = el('div', 't'), f = el('div', 'f');
        t.appendChild(f);
        row.appendChild(el('span', '', o[0])); row.appendChild(t); row.appendChild(el('div', 'v', o[1].toFixed(2)));
        bars.appendChild(row);
        return [f, o[1]];
      });
      requestAnimationFrame(function () { requestAnimationFrame(function () { fills.forEach(function (f) { f[0].style.width = f[1] * 100 + '%'; }); }); });
      stamp.textContent = c.res[0];
      stamp.className = 'stamp ' + c.res[1];
      setTimeout(function () { restart(stamp, 'show'); }, REDUCE ? 0 : 420);
    }
    pick(0);
    onView($('#safety .panel'), function () { pick(0); });
  }

  /* ================= 界面截图 ================= */
  function shots() {
    var win = $('#shotWin'), img = $('#shotImg'), tabs = $('#shotTabs'), box = $('#shots-in');
    tabs.addEventListener('click', function (e) {
      var b = e.target.closest('button');
      if (!b) return;
      [].forEach.call(tabs.children, function (x) { x.classList.toggle('on', x === b); });
      img.src = b.getAttribute('data-src'); img.alt = '客户端' + b.textContent;
    });
    if (REDUCE) return;
    box.addEventListener('pointermove', function (e) {
      if (e.pointerType === 'touch') return;
      var r = win.getBoundingClientRect(), x = (e.clientX - r.left) / r.width - 0.5, y = (e.clientY - r.top) / r.height - 0.5;
      win.style.transform = 'rotateY(' + clamp(x, -0.8, 0.8) * 7 + 'deg) rotateX(' + clamp(-y, -0.8, 0.8) * 5 + 'deg)';
    });
    box.addEventListener('pointerleave', function () { win.style.transform = ''; });
    onView(win, function () { ['detail', 'chat', 'order'].forEach(function (n) { new Image().src = 'img/' + n + '.webp'; }); }, 0.1);
  }

  /* ================= 滚动到时出场 ================= */
  function reveals() {
    var nodes = [].slice.call(document.querySelectorAll('[data-rv]'));
    if (!('IntersectionObserver' in window)) return;
    document.documentElement.classList.add('js');
    var io = new IntersectionObserver(function (es) {
      es.forEach(function (e) { if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); } });
    }, { threshold: 0.18 });
    nodes.forEach(function (n) { io.observe(n); });
  }

  /* ================= 常见问题：一次只开一条，高度有过渡 ================= */
  function faq() {
    var items = [].slice.call(document.querySelectorAll('.faq details'));
    function slide(a, from, to, done) {
      if (a._an) a._an.cancel();
      if (REDUCE || !a.animate) { if (done) done(); return; }
      a.style.overflow = 'hidden';
      a._an = a.animate([{ height: from + 'px', paddingTop: from ? '14px' : '0px' }, { height: to + 'px', paddingTop: to ? '14px' : '0px' }],
        { duration: 300, easing: 'cubic-bezier(.3,.7,.2,1)' });
      a._an.onfinish = a._an.oncancel = function () { a.style.overflow = ''; a._an = null; if (done) done(); };
    }
    function shut(d) {
      var a = $('.a', d);
      d.classList.remove('open');
      slide(a, a.offsetHeight, 0, function () { if (!d.classList.contains('open')) d.open = false; });
    }
    items.forEach(function (d) {
      $('summary', d).addEventListener('click', function (e) {
        e.preventDefault();
        if (d.classList.contains('open')) { shut(d); return; }
        items.forEach(function (o) { if (o !== d && o.classList.contains('open')) shut(o); });
        var a = $('.a', d);
        d.open = true; d.classList.add('open');
        slide(a, 0, a.scrollHeight);
      });
    });
  }

  [hero, chrome, reveals, race, escrow, push, safety, shots, faq].forEach(function (f) {
    try { f(); } catch (err) { if (window.console) console.error(err); }
  });
})();
