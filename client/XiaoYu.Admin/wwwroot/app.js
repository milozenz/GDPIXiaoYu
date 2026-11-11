'use strict';
// 广理职小鱼 审核后台 —— 网页逻辑（原生 JavaScript，不用框架）
//
// 页面用地址栏 # 后面的部分区分：#/overview、#/manual、#/auto、#/closed、#/case/12、#/users、#/goods、#/blocked，
// 刷新页面不会丢失位置。数据都来自 /api/...（见 Program.cs 顶部的接口一览）。
//
// 安全：用户写的内容（私信、商品标题、昵称……）一律当纯文本放进页面（textContent / append 字符串），
// 从不拼接 HTML，这样就算有人在昵称里写 <script> 也只会原样显示出来。

// ======================== 小工具 ========================

const $ = (id) => document.getElementById(id);

/** 创建元素：h('div', {class: 'card'}, '文字', 子元素...) */
function h(tag, attrs, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v === true ? '' : String(v));
  }
  for (const c of children.flat(Infinity)) {
    if (c == null || c === false) continue;
    el.append(c instanceof Node ? c : String(c));
  }
  return el;
}

const ICONS = {
  overview: ['M4 4h6v6H4z', 'M14 4h6v6h-6z', 'M4 14h6v6H4z', 'M14 14h6v6h-6z'],
  manual: ['M15 20v-1.5a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4V20', 'M8.5 11a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7z', 'M16 11l2 2 4-4'],
  auto: ['M13 2L4 14h8l-1 8 9-12h-8l1-8z'],
  closed: ['M20 8v12H4V8', 'M2 4h20v4H2z', 'M10 12h4'],
  users: ['M16 20v-1.5a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4V20', 'M9 11a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7z', 'M22 20v-1.5a4 4 0 0 0-3-3.8', 'M16 4.2a3.5 3.5 0 0 1 0 6.6'],
  goods: ['M6 3L3 7v13a1 1 0 0 0 1 1h16a1 1 0 0 0 1-1V7l-3-4z', 'M3 7h18', 'M16 11a4 4 0 0 1-8 0'],
  blocked: ['M12 21s8-4 8-10V5l-8-3-8 3v6c0 6 8 10 8 10z', 'M9.5 9.5l5 5', 'M14.5 9.5l-5 5'],
};

function icon(paths) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  for (const d of paths) {
    const p = document.createElementNS(ns, 'path');
    p.setAttribute('d', d);
    svg.append(p);
  }
  return svg;
}

// ---- 时间（一律按北京时间显示）、金额 ----
const dtf = new Intl.DateTimeFormat('zh-CN', {
  timeZone: 'Asia/Shanghai', year: 'numeric', month: '2-digit', day: '2-digit',
  hour: '2-digit', minute: '2-digit', hourCycle: 'h23',
});
function fmtTime(sec) {
  if (!sec) return '—';
  const p = Object.fromEntries(dtf.formatToParts(new Date(sec * 1000)).map((x) => [x.type, x.value]));
  return `${p.year}-${p.month}-${p.day} ${p.hour}:${p.minute}`;
}
const nowSec = () => Math.floor(Date.now() / 1000);
function ago(sec) {
  const d = nowSec() - sec;
  if (d < 60) return '刚刚';
  if (d < 3600) return `${Math.floor(d / 60)} 分钟前`;
  if (d < 86400) return `${Math.floor(d / 3600)} 小时前`;
  if (d < 86400 * 7) return `${Math.floor(d / 86400)} 天前`;
  return fmtTime(sec).slice(0, 10);
}
const money = (cents) => '¥' + (cents % 100 === 0 ? cents / 100 : (cents / 100).toFixed(2));
const pct = (x) => `${Math.round(x * 100)}%`;
const muteText = (until) => (until > nowSec() ? `禁言中，${fmtTime(until)} 解除` : '未禁言');
const who = (u) => (u ? `${u.nickname || '（无昵称）'} #${u.user_id}` : '—');

// ---- 各种状态的中文名和颜色 ----
const KIND = { report: ['举报', 'red'], chat: ['私信', 'blue'], goods: ['商品', 'yellow'], profile: ['资料', 'violet'], appeal: ['申诉', 'green'] };
const AREA = { auto: ['已自动处理', 'orange'], manual: ['人工待审', 'red'] };
const CATEGORY = {
  abuse: '人身攻击', porn: '色情低俗', political: '政治敏感', illegal: '违法违禁', threat: '骚扰威胁', scam: '诈骗引流',
  cheating: '代课代写', contraband: '违禁品', identity: '证件账号', other: '其他', none: '没有违规', ok: '合规',
};
const GOODS_STATUS = { on_sale: ['在售', 'green'], reviewing: ['审核中', 'orange'], rejected: ['已驳回', 'red'], off_shelf: ['已下架', ''], sold: ['已售出', 'blue'] };
const REPORT_STATUS = { pending: ['等 AI 判断', 'orange'], upheld: ['举报成立', 'red'], rejected: ['举报不成立', ''], manual: ['等人工判断', 'orange'] };
const APPEAL_STATUS = { open: ['待处理', 'orange'], accepted: ['已同意', 'green'], rejected: ['已驳回', ''] };
const BLOCK_KIND = { chat: ['私信', 'blue'], goods: ['商品', 'yellow'], profile: ['资料', 'violet'] };

const catLabel = (code) => CATEGORY[code] || code || '—';
const tag = ([text, color]) => h('span', { class: `tag ${color || ''}` }, text);
const tagOf = (map, key) => tag(map[key] || [key || '未知', '']);

// ======================== 和后台通信 ========================

class LoginRequired extends Error {}

async function api(path, { method = 'GET', body } = {}) {
  const init = { method, headers: {}, credentials: 'same-origin' };
  if (method !== 'GET') init.headers['X-XiaoYu-Admin'] = '1';  // 后台要求改数据的请求带上它（防跨站伪造）
  if (body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }
  let res;
  try {
    res = await fetch(`/api${path}`, init);
  } catch {
    throw new Error('网络出错，连不上审核后台');
  }
  let data = null;
  try { data = await res.json(); } catch { /* 不是 JSON */ }
  if (res.status === 401 && path !== '/login') {
    showLogin(data?.msg);
    throw new LoginRequired(data?.msg || '请先登录');
  }
  if (!res.ok) throw new Error(data?.msg || `请求失败（${res.status}）`);
  return data;
}

// ======================== 通用界面部件 ========================

let toastTimer = 0;
function toast(msg, bad = false) {
  const t = $('toast');
  t.textContent = msg;
  t.className = bad ? 'toast bad' : 'toast';
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 3500);
}

/** 弹出确认框，content 是要放进去的元素数组。点"确定"返回 true */
function confirmDialog(title, content, okClass = 'btn-primary', okText = '确定') {
  const dlg = $('dialog');
  $('dialog-title').textContent = title;
  $('dialog-content').replaceChildren(...content);
  const ok = $('dialog-ok');
  ok.className = `btn ${okClass}`;
  ok.textContent = okText;
  dlg.returnValue = '';
  dlg.showModal();
  return new Promise((resolve) => dlg.addEventListener('close', () => resolve(dlg.returnValue === 'ok'), { once: true }));
}

function hoursSelect(def = 12) {
  return h('select', { class: 'input' },
    [1, 12, 24, 72, 168, 720].map((n) => h('option', { value: n, selected: n === def }, n < 24 ? `${n} 小时` : `${n / 24} 天`)));
}

function head(title, sub, extra) {
  return h('div', { class: 'page-head' }, h('div', null, h('h1', null, title), sub ? h('p', null, sub) : null), extra);
}

const empty = (text) => h('div', { class: 'card empty' }, text);

function kv(rows) {
  return h('dl', { class: 'kv' }, rows.filter(Boolean).map(([k, v]) => [h('dt', null, k), h('dd', null, v)]));
}

function table(headers, rows) {
  return h('div', { class: 'table-wrap' },
    h('table', null, h('thead', null, h('tr', null, headers.map((t) => h('th', null, t)))), h('tbody', null, rows)));
}

function tabs(items, current, onPick) {
  return h('div', { class: 'tabs', role: 'tablist' },
    items.map(([value, text]) => h('button', {
      type: 'button', role: 'tab', class: value === current ? 'on' : '', 'aria-selected': value === current ? 'true' : 'false',
      onclick: () => onPick(value),
    }, text)));
}

function pager(data, goPage) {
  const pages = Math.max(1, Math.ceil(data.total / data.page_size));
  if (pages <= 1) return null;
  return h('div', { class: 'pager' },
    h('button', { class: 'btn btn-ghost btn-sm', type: 'button', disabled: data.page <= 1, onclick: () => goPage(data.page - 1) }, '上一页'),
    h('span', null, `第 ${data.page} / ${pages} 页 · 共 ${data.total} 条`),
    h('button', { class: 'btn btn-ghost btn-sm', type: 'button', disabled: data.page >= pages, onclick: () => goPage(data.page + 1) }, '下一页'));
}

/** 概率条：≥90% 红（AI 有把握违规），≤10% 绿（有把握没问题），中间橙（没把握） */
function meter(name, v, note) {
  const has = typeof v === 'number' && v >= 0;
  const fill = h('div', { class: `fill${has && v >= 0.9 ? ' high' : has && v <= 0.1 ? ' low' : ''}` });
  if (has) requestAnimationFrame(() => requestAnimationFrame(() => { fill.style.width = pct(v); }));
  return h('div', { class: 'meter' },
    h('div', { class: 'row' }, h('span', null, name), h('b', null, has ? pct(v) : '还没判断')),
    h('div', { class: 'bar' }, fill),
    note ? h('div', { class: 'note' }, note) : null);
}

/** 在内容里把命中的词标红（找不到就原样显示） */
function highlight(text, word) {
  const i = word ? text.toLowerCase().indexOf(word.toLowerCase()) : -1;
  if (i < 0) return text;
  return [text.slice(0, i), h('mark', null, text.slice(i, i + word.length)), text.slice(i + word.length)];
}

// ======================== 页面切换 ========================

const NAV = [
  ['overview', '概览', ICONS.overview],
  ['manual', '人工待审', ICONS.manual, 'b-manual'],
  ['auto', '已自动处理', ICONS.auto, 'b-auto'],
  ['closed', '已结案', ICONS.closed],
  null,
  ['users', '用户', ICONS.users],
  ['goods', '商品', ICONS.goods],
  ['blocked', '拦截记录', ICONS.blocked],
];

function buildNav() {
  $('nav').replaceChildren(...NAV.map((item) => {
    if (!item) return h('div', { class: 'sep' });
    const [key, text, paths, badge] = item;
    return h('a', { href: `#/${key}`, 'data-key': key },
      icon(paths), h('span', { class: 'label' }, text), badge ? h('span', { id: badge, class: key === 'auto' ? 'badge soft' : 'badge' }) : null);
  }));
}

/** 跳到某个页面，params 会放进地址栏（空值不放） */
function go(name, params = {}) {
  const q = new URLSearchParams();
  for (const [k, v] of Object.entries(params)) if (v !== '' && v != null && !(k === 'page' && +v === 1)) q.set(k, v);
  const hash = `#/${name}${q.toString() ? `?${q}` : ''}`;
  if (location.hash === hash) route();
  else location.hash = hash;
}

let routeSeq = 0;
let lastList = '#/overview';  // 从案件详情"返回"时回到哪个页面

function setNav(key) {
  for (const a of $('nav').querySelectorAll('a')) a.classList.toggle('active', a.dataset.key === key);
}

async function route({ keepScroll = false } = {}) {
  const seq = ++routeSeq;
  const [path, qs] = (location.hash.replace(/^#\/?/, '') || 'overview').split('?');
  const params = new URLSearchParams(qs || '');
  const [name, arg] = path.split('/');
  const page = PAGES[name] ? name : 'overview';
  if (['overview', 'manual', 'auto', 'closed'].includes(page)) lastList = location.hash || '#/overview';
  if (page !== 'case') setNav(page);  // 案件详情加载完再按案件状态高亮

  const main = $('main');
  if (!keepScroll) main.replaceChildren(h('div', { class: 'loading' }, '加载中…'));
  try {
    const view = await PAGES[page](params, arg);
    if (seq !== routeSeq) return;  // 加载期间又切到别的页面了
    main.replaceChildren(view);
    if (!keepScroll) window.scrollTo(0, 0);
  } catch (e) {
    if (seq !== routeSeq || e instanceof LoginRequired) return;
    main.replaceChildren(h('div', { class: 'error-box' }, e.message));
  }
  refreshBadges();
}

const rerender = () => route({ keepScroll: true });

async function refreshBadges() {
  try {
    const s = await api('/stats');
    $('b-manual').textContent = s.manual_open || '';
    $('b-auto').textContent = s.auto_open || '';
    document.title = `${s.manual_open ? `(${s.manual_open}) ` : ''}小鱼审核后台`;
  } catch { /* 角标刷新失败不要紧 */ }
}

// ======================== 概览 ========================

async function pageOverview() {
  const [s, recent] = await Promise.all([api('/stats'), api('/cases?area=manual&status=open')]);
  const stat = (num, name, href, cls = '') =>
    h(href ? 'a' : 'div', { class: `stat ${cls}`, href }, h('div', { class: 'num' }, num), h('div', { class: 'name' }, name));
  return h('div', null,
    head('概览', 'AI 先审，有把握的自动处理；拿不准的交给人。'),
    h('div', { class: 'stats' },
      stat(s.manual_open, '人工待审', '#/manual', s.manual_open ? 'hot alert' : ''),
      stat(s.auto_open, '自动处理待复核', '#/auto', s.auto_open ? 'hot' : ''),
      stat(s.reviewing_goods, '审核中的商品', '#/goods?status=reviewing'),
      stat(s.rejected_goods, '被驳回的商品', '#/goods?status=rejected'),
      stat(s.muted_users, '禁言中的用户', '#/users?muted=1'),
      stat(s.reports_total, '累计举报'),
      stat(s.blocked_total, '一级词拦截', '#/blocked'),
      stat(s.pending_jobs, '排队等 AI 的任务')),
    h('div', { class: 'grid-2' },
      h('div', { class: 'card' },
        h('h2', null, '最新的人工待审', h('small', null, `共 ${recent.total} 件`)),
        recent.items.length ? caseList(recent.items.slice(0, 5), true) : h('div', { class: 'empty' }, '没有待审的案件')),
      h('div', { class: 'card' },
        h('h2', null, '审核规则',
          h('span', { class: `pill ${s.ai_enabled ? 'on' : 'off'}` }, s.ai_enabled ? 'AI 审核已开启' : 'AI 审核未开启')),
        h('ul', { class: 'rules' },
          h('li', null, h('b', null, '私信：'), '一级敏感词直接拦下；二级词（骚扰威胁、诈骗引流）照常发出，接收方会看到提醒，同时交给 AI。AI 判违规概率 ≥ 90% 就屏蔽消息、禁言 12 小时。'),
          h('li', null, h('b', null, '举报：'), 'AI 分两次独立判断：言论本身是否明确违规（严）、举报理由是否成立（宽）。言论违规 ≥ 90% 自动屏蔽并禁言；两项都 ≤ 10% 自动驳回举报；其余交人工。'),
          h('li', null, h('b', null, '商品：'), '含禁售词的不让发布；其余先"审核中"。AI 有把握合规就上架，有把握违规就驳回下架；没把握或 AI 出错先上架，再交人工复核。'),
          h('li', null, h('b', null, '昵称和简介：'), 'AI 有把握判违规就重置昵称、清空简介，24 小时内不能改昵称。'),
          h('li', null, h('b', null, '复核：'), '所有自动处理都进"已自动处理"，人工可以确认或撤销；用户的申诉直接进"人工待审"。')))));
}

// ======================== 案件列表 ========================

function caseList(items, compact = false) {
  return h('div', { class: `case-list${compact ? ' compact' : ''}` }, items.map((c) => {
    const ai = c.ai_score >= 0
      ? h('span', null, `AI：${catLabel(c.ai_label)}（违规概率 ${pct(c.ai_score)}）`)
      : null;
    return h('a', { class: 'case-row', href: `#/case/${c.case_id}` },
      h('div', { class: 'tags' },
        tagOf(KIND, c.kind),
        c.status === 'closed' ? tag(['已结案', 'green']) : c.area === 'auto' ? tag(['自动', 'orange']) : null),
      h('div', { class: 'summary' }, c.summary),
      h('div', { class: 'when', title: fmtTime(c.created_at) }, ago(c.created_at)),
      h('div', { class: 'meta' },
        h('span', null, `当事人：${who(c.user)}`),
        ai,
        c.status === 'closed' && c.result ? h('span', null, c.result) : null));
  }));
}

function casesPage(which) {
  const titles = {
    manual: ['人工待审', 'AI 没把握、AI 调用失败的内容，以及用户的申诉。处理后结案。', '没有待审的案件，休息一下吧'],
    auto: ['已自动处理', 'AI 有把握时已经自动处理了（屏蔽禁言、驳回商品、重置昵称、驳回举报）。请复核：没问题就结案，判错了可以撤销。', '没有待复核的自动处理'],
    closed: ['已结案', '处理过的案件和处理结果。', '还没有结案的案件'],
  };
  return async (params) => {
    const page = +params.get('page') || 1;
    const filter = which === 'closed' ? 'status=closed' : `area=${which}&status=open`;
    const data = await api(`/cases?${filter}&page=${page}`);
    const [title, sub, none] = titles[which];
    return h('div', null,
      head(title, sub),
      data.items.length ? caseList(data.items) : empty(none),
      pager(data, (p) => go(which, { page: p })));
  };
}

// ======================== 案件详情 ========================

async function pageCase(params, id) {
  if (!/^\d+$/.test(id || '')) throw new Error('案件编号不对');
  const d = await api(`/cases/${id}`);
  const c = d.case;
  setNav(c.status === 'closed' ? 'closed' : c.area);
  const cards = [];
  if (c.kind === 'report' && d.report) cards.push(reportCard(d.report), reportAiCard(d.report));
  if ((c.kind === 'report' || c.kind === 'chat')) cards.push(conversationCard(d));
  if (c.kind === 'appeal' && d.appeal) cards.push(appealCard(d.appeal, c.user, d.user_muted_until));
  if (c.kind === 'profile' && d.profile) cards.push(profileCard(d.profile));
  if (d.goods) cards.push(goodsCard(d.goods));
  if (c.kind !== 'report' && c.ai_score >= 0) {
    cards.push(h('div', { class: 'card' }, h('h2', null, 'AI 判断'),
      meter('违规概率', c.ai_score, `最可能：${catLabel(c.ai_label)} · ≥ 90% 自动处理，≤ 10% 直接放行`)));
  }

  return h('div', null,
    h('a', { class: 'back', href: lastList }, '‹ 返回'),
    h('div', { class: 'detail-head' },
      h('h1', null, `案件 #${c.case_id}`), tagOf(KIND, c.kind), tagOf(AREA, c.area),
      c.status === 'closed' ? tag(['已结案', 'green']) : tag(['待处理', 'orange'])),
    h('div', { class: 'summary-box' }, c.summary),
    h('div', { class: 'layout-detail' },
      h('div', { class: 'stack' }, cards),
      h('div', { class: 'side-panel' },
        actionPanel(d),
        h('div', { class: 'card' }, h('h2', null, '案件信息'),
          kv([
            ['当事人', who(c.user)],
            ['来源', c.area === 'auto' ? 'AI 自动处理' : '人工待审'],
            ['创建时间', fmtTime(c.created_at)],
            c.status === 'closed' ? ['结案时间', fmtTime(c.handled_at)] : null,
          ])))));
}

function reportCard(r) {
  return h('div', { class: 'card' },
    h('h2', null, '举报', tagOf(REPORT_STATUS, r.status)),
    kv([
      ['举报人', who(r.reporter)],
      ['类型', r.type_label || catLabel(r.type)],
      ['理由', r.reason || h('span', { class: 'faint' }, '（没写）')],
      ['举报时间', fmtTime(r.created_at)],
    ]));
}

function reportAiCard(r) {
  return h('div', { class: 'card' },
    h('h2', null, 'AI 判断', h('small', null, '两次独立提问')),
    meter('言论本身明确违规（严）', r.ai_violation,
      `${r.ai_category ? `最可能：${catLabel(r.ai_category)} · ` : ''}≥ 90% 自动屏蔽并禁言`),
    meter('举报理由成立（宽）', r.ai_reason, '两项都 ≤ 10% 自动驳回举报，其余交人工'));
}

function conversationCard(d) {
  if (!d.message) return h('div', { class: 'card' }, h('h2', null, '对话'), h('div', { class: 'empty' }, '消息已不存在'));
  const m = d.message;
  const names = { [d.sender.user_id]: d.sender.nickname, [d.receiver.user_id]: d.receiver.nickname };
  return h('div', { class: 'card' },
    h('h2', null, '对话上下文', h('small', null, `${d.sender.nickname} → ${d.receiver.nickname}`)),
    h('div', { class: 'chat' }, d.context.map((x) => {
      const flags = [
        x.is_target ? tag(['这一条', 'red']) : null,
        x.is_target && m.flag === 'warn' ? tag([`二级词：${catLabel(m.flag_kind)}`, 'orange']) : null,
        x.masked ? tag(['已屏蔽', '']) : null,
      ].filter(Boolean);
      return h('div', { class: `msg${x.from_id === m.to_id ? ' right' : ''}${x.is_target ? ' target' : ''}` },
        h('div', { class: 'who' }, `${names[x.from_id] || `#${x.from_id}`} · ${fmtTime(x.created_at)}`),
        h('div', { class: 'bubble' }, x.content),
        flags.length ? h('div', { class: 'flags' }, flags) : null);
    })),
    h('p', { class: 'muted' }, `发送者 ${who(d.sender)}：${muteText(d.sender_muted_until)}`));
}

function goodsCard(g) {
  const img = (name) => `/api/image?name=${encodeURIComponent(name)}`;
  return h('div', { class: 'card' },
    h('h2', null, '商品', tagOf(GOODS_STATUS, g.status), h('small', null, `#${g.goods_id}`)),
    kv([
      ['标题', g.title],
      ['价格', h('span', { class: 'price' }, money(g.price))],
      ['分类', g.category_name],
      ['卖家', who(g.seller)],
      ['发布时间', fmtTime(g.created_at)],
      g.reject_reason ? ['驳回原因', g.reject_reason] : null,
      ['描述', g.description || h('span', { class: 'faint' }, '（没写）')],
    ]),
    g.images?.length
      ? h('div', { class: 'images' }, g.images.map((name) =>
        h('a', { href: img(name), target: '_blank', rel: 'noopener' }, h('img', { src: img(name), alt: '商品图片', loading: 'lazy' }))))
      : null);
}

function profileCard(p) {
  return h('div', { class: 'card' },
    h('h2', null, '用户资料（当前）', h('small', null, `#${p.user_id}`)),
    kv([
      ['昵称', p.nickname],
      ['简介', p.bio || h('span', { class: 'faint' }, '（空）')],
      ['用户名', p.username],
      ['邮箱', p.email || '—'],
      ['改昵称', p.nickname_locked_until > nowSec() ? `锁定中，${fmtTime(p.nickname_locked_until)} 解除` : '正常'],
      ['禁言', muteText(p.muted_until)],
      ['注册时间', fmtTime(p.created_at)],
    ]));
}

function appealCard(a, user, mutedUntil) {
  return h('div', { class: 'card' },
    h('h2', null, a.kind === 'mute' ? '禁言申诉' : '商品申诉', tagOf(APPEAL_STATUS, a.status)),
    kv([
      ['申诉人', who(user)],
      ['申诉内容', h('div', { class: 'quote' }, a.content)],
      ['申诉时间', fmtTime(a.created_at)],
      a.kind === 'mute' ? ['现在', muteText(mutedUntil)] : null,
    ]));
}

/** 不同案件能做的处理（action 对应服务器 admin.resolve 的 action） */
function actionsFor(d) {
  const c = d.case;
  const auto = c.area === 'auto';
  switch (c.kind) {
    case 'report':
    case 'chat': {
      if (!d.message) return [{ action: 'close', text: '结案', cls: 'btn-primary', confirm: '消息已不存在，直接结案。' }];
      const sender = d.sender.nickname;
      if (d.message.flag === 'masked') {
        return [
          { action: 'close', text: auto ? '确认无误，结案' : '维持屏蔽，结案', cls: 'btn-primary', confirm: '维持现状：消息保持屏蔽，禁言照旧。' },
          { action: 'revert', text: '撤销处理', hint: '恢复消息、解除禁言', cls: 'btn-ghost', confirm: `判定这条消息不违规：恢复消息，并解除「${sender}」的禁言。` },
        ];
      }
      return [
        { action: 'punish', text: '屏蔽并禁言', hint: '可选时长', cls: 'btn-danger', confirm: `屏蔽这条消息，并禁言「${sender}」。对方会收到通知${c.kind === 'report' ? '，举报人会收到"举报成立"' : ''}。` },
        c.kind === 'report'
          ? { action: 'close', text: auto ? '确认驳回举报，结案' : '不违规，驳回举报', cls: auto ? 'btn-primary' : 'btn-ghost', confirm: '判定不违规：举报不成立，消息保持原样。' }
          : { action: 'close', text: '不违规，结案', cls: 'btn-ghost', confirm: '判定不违规，消息保持原样。' },
      ];
    }
    case 'goods': {
      const g = d.goods || {};
      const t = g.title ? `《${g.title}》` : '';
      const keep = { action: 'close', text: '没问题，结案', cls: 'btn-primary', confirm: `商品${t}保持现状。` };
      if (g.status === 'sold') return [keep];
      if (g.status === 'rejected') {
        return [
          { action: 'close', text: auto ? '确认驳回，结案' : '维持驳回，结案', cls: 'btn-primary', confirm: `商品${t}保持驳回下架。` },
          { action: 'restore', text: '恢复上架', hint: '撤销驳回', cls: 'btn-ghost', confirm: `把商品${t}恢复上架，清除驳回原因。` },
        ];
      }
      const list = [
        { action: 'reject', text: '驳回下架', hint: '备注 = 驳回原因', cls: 'btn-danger', confirm: `驳回商品${t}并下架。卖家会在「我的发布」里看到驳回原因（备注；没写就是"违反平台规定"）。` },
        keep,
      ];
      if (g.status === 'reviewing') list.splice(1, 0, { action: 'restore', text: '审核通过，上架', cls: 'btn-success', confirm: `商品${t}审核通过，立即上架。` });
      return list;
    }
    case 'profile':
      if (auto) return [{ action: 'close', text: '确认无误，结案', cls: 'btn-primary', confirm: '确认 AI 的处理（昵称已重置、简介已清空）。' }];
      return [
        { action: 'reset', text: '重置昵称和简介', hint: '锁 24 小时', cls: 'btn-danger', confirm: `把昵称改成「小鱼用户${c.ref_id}」、清空简介，24 小时内不能改昵称。对方会收到通知。` },
        { action: 'close', text: '没问题，结案', cls: 'btn-primary', confirm: '昵称和简介保持现状。' },
      ];
    case 'appeal': {
      const mute = d.appeal?.kind === 'mute';
      return [
        { action: 'accept', text: '同意申诉', hint: mute ? '解除禁言' : '商品恢复上架', cls: 'btn-success', confirm: mute ? '同意申诉：立即解除禁言。' : '同意申诉：商品恢复上架。' },
        { action: 'reject', text: '驳回申诉', hint: '维持原处理', cls: 'btn-danger', confirm: '驳回申诉，维持原来的处理。' },
      ];
    }
    default:
      return [{ action: 'close', text: '结案', cls: 'btn-primary', confirm: '结案。' }];
  }
}

function actionPanel(d) {
  const c = d.case;
  if (c.status === 'closed') {
    return h('div', { class: 'card' }, h('h2', null, '处理结果'), h('div', { class: 'result-box' }, c.result || '已结案'));
  }
  const note = h('textarea', { class: 'input', maxlength: 200, rows: 3, placeholder: '可选，最多 200 字，会记在处理结果里' });
  return h('div', { class: 'card' },
    h('h2', null, '处理'),
    h('label', { class: 'field' }, h('span', null, c.kind === 'goods' ? '备注 / 驳回原因' : '备注'), note),
    h('div', { class: 'actions' }, actionsFor(d).map((o) =>
      h('button', { class: `btn ${o.cls}`, type: 'button', onclick: () => resolveCase(c, o, note.value) },
        o.text, o.hint ? h('small', null, o.hint) : null))));
}

async function resolveCase(c, o, note) {
  const content = [h('p', null, o.confirm)];
  let hours = null;
  if (o.action === 'punish') {
    hours = hoursSelect(12);
    content.push(h('label', { class: 'field' }, h('span', null, '禁言时长'), hours));
  }
  if (note.trim()) content.push(h('p', null, `备注：${note.trim()}`));
  const danger = o.cls.includes('danger');
  if (!await confirmDialog(o.text, content, danger ? 'btn-danger' : 'btn-primary')) return;
  const body = { action: o.action };
  if (note.trim()) body.note = note.trim();
  if (hours) body.hours = +hours.value;
  try {
    const r = await api(`/cases/${c.case_id}/resolve`, { method: 'POST', body });
    toast(`已处理：${r.result}`);
    rerender();
  } catch (e) {
    if (!(e instanceof LoginRequired)) toast(e.message, true);
  }
}

// ======================== 用户 ========================

async function pageUsers(params) {
  const page = +params.get('page') || 1;
  const keyword = params.get('keyword') || '';
  const muted = params.get('muted') === '1';
  const q = new URLSearchParams({ page });
  if (keyword) q.set('keyword', keyword);
  if (muted) q.set('muted', 'true');
  const data = await api(`/users?${q}`);

  const input = h('input', { class: 'input', type: 'search', placeholder: '搜用户名或昵称', value: keyword, maxlength: 30 });
  const box = h('input', { type: 'checkbox', checked: muted });
  const apply = () => go('users', { keyword: input.value.trim(), muted: box.checked ? '1' : '' });
  input.addEventListener('keydown', (e) => { if (e.key === 'Enter') apply(); });
  box.addEventListener('change', apply);

  return h('div', null,
    head('用户', `共 ${data.total} 人${muted ? '正在禁言' : ''}`),
    h('div', { class: 'toolbar' },
      input,
      h('button', { class: 'btn btn-primary', type: 'button', onclick: apply }, '搜索'),
      h('label', { class: 'check' }, box, '只看禁言中')),
    data.items.length
      ? table(['编号', '用户', '邮箱', '状态', '私信案件', '注册时间', '操作'], data.items.map(userRow))
      : empty('没有找到用户'),
    pager(data, (p) => go('users', { keyword, muted: muted ? '1' : '', page: p })));
}

function userRow(u) {
  const mutedNow = u.muted_until > nowSec();
  return h('tr', null,
    h('td', { class: 'num' }, u.user_id),
    h('td', null,
      h('div', null, u.nickname, ' ', u.is_admin ? tag(['管理员', 'yellow']) : null),
      h('div', { class: 'sub' }, `@${u.username}`)),
    h('td', null, u.email || h('span', { class: 'faint' }, '—')),
    h('td', { class: 'nowrap' }, mutedNow
      ? [tag(['禁言中', 'red']), h('div', { class: 'sub' }, `${fmtTime(u.muted_until)} 解除`)]
      : h('span', { class: 'faint' }, '正常')),
    h('td', { class: 'num' }, u.violations || 0),
    h('td', { class: 'nowrap' }, fmtTime(u.created_at)),
    h('td', null, h('div', { class: 'row-actions' },
      h('button', { class: 'btn btn-ghost btn-sm', type: 'button', onclick: () => muteUser(u, mutedNow) }, mutedNow ? '加时' : '禁言'),
      mutedNow ? h('button', { class: 'btn btn-ghost btn-sm', type: 'button', onclick: () => unmuteUser(u) }, '解除') : null)));
}

async function muteUser(u, mutedNow) {
  const sel = hoursSelect(24);
  const ok = await confirmDialog(mutedNow ? '禁言加时' : '禁言', [
    h('p', null, mutedNow
      ? `「${u.nickname}」正在禁言，在原来的结束时间上再加：`
      : `禁言「${u.nickname}」，禁言期间不能发私信：`),
    h('label', { class: 'field' }, h('span', null, '时长'), sel),
  ], 'btn-danger');
  if (!ok) return;
  try {
    const r = await api(`/users/${u.user_id}/mute`, { method: 'POST', body: { hours: +sel.value } });
    toast(`已禁言，${fmtTime(r.muted_until)} 解除`);
    rerender();
  } catch (e) {
    if (!(e instanceof LoginRequired)) toast(e.message, true);
  }
}

async function unmuteUser(u) {
  if (!await confirmDialog('解除禁言', [h('p', null, `立即解除「${u.nickname}」的禁言？`)])) return;
  try {
    await api(`/users/${u.user_id}/unmute`, { method: 'POST' });
    toast('已解除禁言');
    rerender();
  } catch (e) {
    if (!(e instanceof LoginRequired)) toast(e.message, true);
  }
}

// ======================== 商品 ========================

const GOODS_TABS = [['', '全部'], ['reviewing', '审核中'], ['on_sale', '在售'], ['rejected', '已驳回'], ['off_shelf', '已下架'], ['sold', '已售出']];

async function pageGoods(params) {
  const status = params.get('status') || '';
  const page = +params.get('page') || 1;
  const q = new URLSearchParams({ page });
  if (status) q.set('status', status);
  const data = await api(`/goods?${q}`);
  return h('div', null,
    head('商品', '所有商品，包括审核中和被驳回的（商品不会被删除）。'),
    h('div', { class: 'toolbar' }, tabs(GOODS_TABS, status, (s) => go('goods', { status: s }))),
    data.items.length
      ? table(['编号', '标题', '价格', '状态', '卖家', '发布时间'], data.items.map((g) => h('tr', null,
        h('td', { class: 'num' }, g.goods_id),
        h('td', { class: 'content' }, g.title, g.reject_reason ? h('div', { class: 'sub' }, `驳回原因：${g.reject_reason}`) : null),
        h('td', { class: 'num price' }, money(g.price)),
        h('td', null, tagOf(GOODS_STATUS, g.status)),
        h('td', null, who(g.seller)),
        h('td', { class: 'nowrap' }, fmtTime(g.created_at)))))
      : empty('没有商品'),
    pager(data, (p) => go('goods', { status, page: p })));
}

// ======================== 拦截记录 ========================

const BLOCK_TABS = [['', '全部'], ['chat', '私信'], ['goods', '商品'], ['profile', '资料']];

async function pageBlocked(params) {
  const kind = params.get('kind') || '';
  const page = +params.get('page') || 1;
  const q = new URLSearchParams({ page });
  if (kind) q.set('kind', kind);
  const data = await api(`/blocked?${q}`);
  return h('div', null,
    head('拦截记录', '命中一级敏感词、没有发出去的内容都留了底。'),
    h('div', { class: 'toolbar' }, tabs(BLOCK_TABS, kind, (k) => go('blocked', { kind: k }))),
    data.items.length
      ? table(['时间', '位置', '用户', '类别', '命中词', '内容'], data.items.map((b) => h('tr', null,
        h('td', { class: 'nowrap' }, fmtTime(b.created_at)),
        h('td', null, tagOf(BLOCK_KIND, b.kind)),
        h('td', null, who(b.user)),
        h('td', { class: 'nowrap' }, b.category_label),
        h('td', { class: 'nowrap' }, h('mark', null, b.word)),
        h('td', { class: 'content' }, highlight(b.content, b.word)))))
      : empty('还没有拦截记录'),
    pager(data, (p) => go('blocked', { kind, page: p })));
}

// ======================== 登录 / 启动 ========================

const PAGES = {
  overview: pageOverview,
  manual: casesPage('manual'),
  auto: casesPage('auto'),
  closed: casesPage('closed'),
  case: pageCase,
  users: pageUsers,
  goods: pageGoods,
  blocked: pageBlocked,
};

function showLogin(msg) {
  $('app').hidden = true;
  $('login').hidden = false;
  $('login-error').textContent = msg && msg !== '请先登录' ? msg : '';
  $('login-user').focus();
}

function enterApp(me) {
  $('login').hidden = true;
  $('app').hidden = false;
  $('me-name').textContent = `${me.nickname}（${me.username}）`;
  route();
}

$('login-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const btn = $('login-btn');
  btn.disabled = true;
  btn.textContent = '登录中…';
  $('login-error').textContent = '';
  try {
    const me = await api('/login', {
      method: 'POST',
      body: { username: $('login-user').value.trim(), password: $('login-pass').value },
    });
    $('login-pass').value = '';
    enterApp(me);
  } catch (err) {
    $('login-error').textContent = err.message;
  } finally {
    btn.disabled = false;
    btn.textContent = '登录';
  }
});

$('logout').addEventListener('click', async () => {
  try { await api('/logout', { method: 'POST' }); } catch { /* 反正要回登录页 */ }
  showLogin();
});

window.addEventListener('hashchange', () => { if (!$('app').hidden) route(); });

// 每 30 秒刷新一下待办数字（页面在后台时不刷）
setInterval(() => { if (!$('app').hidden && !document.hidden) refreshBadges(); }, 30000);

(async function start() {
  buildNav();
  try {
    enterApp(await api('/me'));
  } catch (e) {
    if (!(e instanceof LoginRequired)) showLogin(e.message);
  }
})();
