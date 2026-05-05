(function() {
  'use strict';

  const STORAGE_KEY = 'crush-lang';
  let lang = localStorage.getItem(STORAGE_KEY) || 'zh';
  let i18nSources = { en: null, zh: null };

  async function loadI18n() {
    try {
      const [en, zh] = await Promise.all([
        fetch('i18n/en.json?v=4').then(r => r.json()),
        fetch('i18n/zh.json?v=4').then(r => r.json()),
      ]);
      i18nSources.en = en;
      i18nSources.zh = zh;
    } catch(e) {
      console.warn('i18n load failed', e);
    }
  }

  function t(key) {
    const keys = key.split('.');
    let obj = i18nSources[lang];
    for (const k of keys) {
      if (!obj || !obj[k]) return key;
      obj = obj[k];
    }
    return obj || key;
  }

  function applyI18n() {
    document.querySelectorAll('[data-i18n]').forEach(el => {
      const key = el.dataset.i18n;
      el.textContent = t(key);
    });
    document.documentElement.lang = lang;
    document.getElementById('langBtn').textContent = lang === 'zh' ? 'EN' : '\u4e2d\u6587';
  }

  function render(tab) {
    const container = document.getElementById('content');
    container.innerHTML = pages[tab] ? pages[tab]() : pages.overview();

    document.querySelectorAll('.nav-links li').forEach(el => {
      el.classList.toggle('active', el.dataset.tab === tab);
    });

    const navLinks = document.getElementById('navLinks');
    navLinks.classList.remove('open');

    window.scrollTo({ top: 0 });
    applyI18n();
    updateTaskbarTime();
    loadLobsterSvg();
    if (tab === 'overview' || !tab) startChatSim();
  }

  function mdRender(text) {
    const esc = text.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
    return esc
      .replace(/```(\w*)\n([\s\S]*?)```/g, '<pre><code>$2</code></pre>')
      .replace(/`([^`]+)`/g, '<code>$1</code>')
      .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
      .replace(/^- (.+)$/gm, '<span class="md-li">&bull; $1</span>')
      .replace(/\|(.+)\|/g, function(m){ return m.replace(/\|/g,'').trim(); })
      .replace(/\n/g, '<br>');
  }

  function startChatSim() {
    const body = document.getElementById('chatBody');
    if (!body) return;
    let timer;
    let playing = false;

    const msgs = [
      { from: 'bot', text: 'ESP-Claw is working on it...' },
      { from: 'bot', text: '\ud83e\udd9e [Round 1] Snap: luastopasyncjob({"name":"snakegame.lua"}), read_file(...)' },
      { from: 'bot', text: '\ud83e\udd9e [Round 2] Snap: lualistscripts({"keyword":"snake"})' },
      { from: 'bot', text: '\ud83e\udd9e [Round 3] Snap: readfile({"path":"snakegame.lua"})' },
      { from: 'bot', text: '\ud83e\udd9e [Round 4] Snap: lualistscripts({})' },
      { from: 'bot', text: '\ud83e\udd9e [Round 5] Snap: luawritescript({"path":"parkour_game.lua","overwrite":true})' },
      { from: 'bot', text: '\ud83e\udd9e [Round 6] Snap: luarunscriptasync({"path":"parkourgame.lua","timeout_ms":120000})' },
      { from: 'bot', text: '\n\ud83c\udfc3 **\u8dd1\u9177\u6e38\u620f\u5df2\u542f\u52a8** \ud83c\udfae\n\n**\u73a9\u6cd5**\n\ud83d\udfe8 \u63a7\u5236\u9ec4\u8272\u5c0f\u4eba\u5de6\u53f3\u5954\u8dd1\n\ud83d\udfe5 \u907f\u5f00\u7ea2\u8272\u969c\u788d\u7269\uff08\u8d8a\u6765\u8d8a\u5feb\u3001\u8d8a\u6765\u8d8a\u591a\uff09\n\ud83d\udcc8 \u6bcf\u8fc7\u969c\u788d\u7269 +10 \u5206\n\u26a1 \u901f\u5ea6\u9010\u6e10\u63d0\u5347\n\n**\u64cd\u4f5c**\n\u7a7a\u683c / \u2191 / \u9f20\u6807\u5de6\u952e = **\u6309\u4f4f\u84c4\u529b\u8df3\u8dc3**\nEnter = \u5f00\u59cb / \u91cd\u65b0\u5f00\u59cb\nESC = \u9000\u51fa\n\n**\u8df3\u8dc3\u8bf4\u660e**\n- \u6309\u4f4f\u4e0d\u653e\uff1a\u89d2\u8272\u6301\u7eed\u5411\u4e0a\u98de\u5347\n- \u677e\u5f00\u6309\u94ae\uff1a\u505c\u6b62\u84c4\u529b\uff0c\u81ea\u7531\u4e0b\u843d\n- \u843d\u5730\u540e\u81ea\u52a8\u590d\u4f4d\n\n\u5feb\u8bd5\u8bd5\u6309\u4f4f\u4e0d\u653e\uff0c\u770b\u770b\u80fd\u98de\u591a\u9ad8\u5427\uff01\ud83d\ude0a' },
    ];
    const delays = [1200, 1800, 1400, 1600, 2000, 2200, 2400, 3000];

    function addBotMsg(i) {
      const m = msgs[i];
      const div = document.createElement('div');
      div.className = 'chat-msg-row bot';
      const bubble = document.createElement('div');
      bubble.className = 'chat-msg';
      bubble.innerHTML = mdRender(m.text);
      div.appendChild(bubble);
      body.appendChild(div);
      body.scrollTop = body.scrollHeight;
    }

    function playAll(callback) {
      let i = 0;
      function next() {
        if (i >= msgs.length) { if (callback) callback(); return; }
        addBotMsg(i);
        i++;
        timer = setTimeout(next, delays[i - 1]);
      }
      timer = setTimeout(next, 400);
    }

    function loop() {
      playing = true;
      const bots = body.querySelectorAll('.chat-msg-row.bot');
      bots.forEach(el => el.remove());
      playAll(function() {
        timer = setTimeout(loop, 4000);
      });
    }

    if (body.dataset.simStarted) {
      loop();
      return;
    }
    body.dataset.simStarted = '1';
    playAll(function() {
      timer = setTimeout(loop, 4000);
    });
  }

  /* ---- pages ---- */

  const pages = {
    overview() {
      const badges = [
        { icon: '\ud83d\udee1\ufe0f', key: 'overview.badge_sandbox' },
        { icon: '\ud83d\udd10', key: 'overview.badge_perm' },
        { icon: '\ud83d\udcbb', key: 'overview.badge_cross' },
      ];

      return `
        <section class="hero">
          <div class="hero-text">
            <h1 class="hero-title">${t('overview.title')}</h1>
            <p class="hero-subtitle">${t('overview.subtitle')}</p>
            <div class="hero-badges">
              ${badges.map(b => `<span class="hero-badge">${b.icon} ${t(b.key)}</span>`).join('')}
            </div>
            <div class="hero-actions">
              <a class="btn" data-tab="guide">${t('overview.btn_start')}</a>
              <a class="btn btn-outline" data-tab="security">${t('overview.btn_security')}</a>
            </div>
          </div>
          <div class="hero-visual">
            <div class="mock-desktop">
              <div class="mock-screen">
                <div class="mock-window">
                  <div class="mock-window-dots"><span></span><span></span><span></span></div>
                  <div class="mock-window-body">
                    <div class="mock-lobster" id="lobsterSvg"></div>
                    <div class="mock-window-label">Crush Claw</div>
                  </div>
                </div>
                <div class="mock-taskbar">
                  <div class="mock-taskbar-item active">\ud83d\udc1a Crush Claw</div>
                  <div class="mock-taskbar-time"></div>
                </div>
              </div>
            </div>
          </div>
        </section>

        <section class="security-strip">
          <div class="security-header">
            <h2>${t('overview.security_title')}</h2>
            <p>${t('overview.security_subtitle')}</p>
          </div>
          <div class="security-hub">
            <div class="sec-side">
              <div class="sec-item">
                <span class="sec-icon-small">\ud83d\udd12</span>
                <div>
                  <h4>${t('overview.sec_key_title')}</h4>
                  <p>${t('overview.sec_key_desc')}</p>
                </div>
              </div>
            </div>
            <div class="sec-center">
              <span class="sec-icon-large">\ud83d\udee1\ufe0f</span>
              <h3>${t('overview.sec_box_title')}</h3>
              <p>${t('overview.sec_box_desc')}</p>
            </div>
            <div class="sec-side">
              <div class="sec-item">
                <span class="sec-icon-small">\u2705</span>
                <div>
                  <h4>${t('overview.sec_audit_title')}</h4>
                  <p>${t('overview.sec_audit_desc')}</p>
                </div>
              </div>
            </div>
          </div>
          <div class="sec-details">
            <div class="sec-detail-item">
              <span class="sec-detail-num">01</span>
              <div>
                <h5>${t('overview.sec_detail_1_title')}</h5>
                <p>${t('overview.sec_detail_1_desc')}</p>
              </div>
            </div>
            <div class="sec-detail-item">
              <span class="sec-detail-num">02</span>
              <div>
                <h5>${t('overview.sec_detail_2_title')}</h5>
                <p>${t('overview.sec_detail_2_desc')}</p>
              </div>
            </div>
            <div class="sec-detail-item">
              <span class="sec-detail-num">03</span>
              <div>
                <h5>${t('overview.sec_detail_3_title')}</h5>
                <p>${t('overview.sec_detail_3_desc')}</p>
              </div>
            </div>
          </div>
        </section>

        <section class="core-alt">
          <div class="core-alt-outer">
          <div class="core-block">
            <div class="core-block-inner">
              <div class="core-text">
                <h3>${t('overview.feature1')}</h3>
                <p>${t('overview.feature1_desc')}</p>
              </div>
              <div class="core-visual">
                <img class="core-gif" src="images/emote_interaction.gif" alt="Emote interaction demo">
              </div>
            </div>
          </div>
          <div class="core-block alt">
            <div class="core-block-inner">
              <div class="core-visual">
                <img class="core-gif" src="images/confession_script.gif" alt="Confession drawing demo">
              </div>
              <div class="core-text">
                <h3>${t('overview.feature2')}</h3>
                <p>${t('overview.feature2_desc')}</p>
              </div>
            </div>
          </div>
          </div>
        </section>

        <section class="status-section">
          <div class="section-inner">
            <h2>${t('overview.tech_title')}</h2>
            <div class="status-panel">
              <div class="status-panel-header">
                <span class="status-led"></span>
                SECURITY AUDIT: <span class="status-passed">&nbsp;PASSED</span>
              </div>
              <div class="status-panel-body">
                <div class="status-row"><span class="status-tag">[SANDBOX]</span><span class="status-desc">Lua JIT in Container</span><span class="status-badge ok">OK</span></div>
                <div class="status-row"><span class="status-tag">[MEMORY]</span><span class="status-desc">10MB Hard Limit</span><span class="status-badge locked">LOCKED</span></div>
                <div class="status-row"><span class="status-tag">[STORAGE]</span><span class="status-desc">Read-Only (Fixed Path)</span><span class="status-badge ok">OK</span></div>
                <div class="status-row"><span class="status-tag">[IO ACCESS]</span><span class="status-desc">All Requests Blocked</span><span class="status-badge denied">DENIED</span></div>
                <div class="status-row"><span class="status-tag">[OS ACCESS]</span><span class="status-desc">System APIs Hidden</span><span class="status-badge denied">DENIED</span></div>
                <div class="status-row"><span class="status-tag">[DEBUG MODE]</span><span class="status-desc">Production Build</span><span class="status-badge off">OFF</span></div>
                <div class="status-row"><span class="status-tag">[SCRIPTS]</span><span class="status-desc">Static Analysis Only</span><span class="status-badge audited">AUDITED</span></div>
              </div>
            </div>
          </div>
        </section>

        <section class="usecase-section">
          <div class="usecase-inner">
            <h2>${t('overview.usecase_title')}</h2>
            <div class="chat-parkour-inner">
              <div class="chat-panel">
                <div class="chat-panel-header">
                  <span class="chat-panel-title">\ud83d\udcac QQ Bot</span>
                  <span class="chat-panel-status">ESP-Claw \u5728\u7ebf</span>
                </div>
                <div class="chat-panel-body" id="chatBody">
                  <div class="chat-msg-row user">
                    <div class="chat-msg">\u8bf7\u4f60\u5728\u5c4f\u5e55\u4e0a\u7f16\u5199\u4e00\u4e2a\u952e\u9f20\u4ea4\u4e92\u7684\u8dd1\u9177\u6e38\u620f\u5e76\u542f\u52a8</div>
                  </div>
                </div>
                <div class="chat-panel-footer">
                  <input class="chat-input" type="text" placeholder="\u8f93\u5165\u6d88\u606f..." disabled>
                  <button class="chat-send-btn" disabled>\u53d1\u9001</button>
                </div>
              </div>
              <div class="chat-visual">
                <img class="parkour-gif" src="images/parkour_run.gif?v=2" alt="Parkour game running">
              </div>
            </div>
          </div>
        </section>

        <section class="trust-section">
          <div class="trust-box">
            <h3>\u26a0\ufe0f ${t('overview.trust_title')}</h3>
            <p>${t('overview.trust_desc')}</p>
          </div>
        </section>
      `;
    },

    guide() {
      return `
        <div class="page">
          <h2>${t('guide.title')}</h2>

          <p>${t('guide.intro')}</p>

          <ol class="guide-list">
            <li>${t('guide.step1')}</li>
            <li>${t('guide.step2')}</li>
            <li>${t('guide.step3')}</li>
            <li>${t('guide.step4')}</li>
          </ol>

          <p>${t('guide.client_intro')}</p>

          <ol class="guide-list" start="5">
            <li>${t('guide.client_step1')}</li>
            <li>${t('guide.client_step2')}</li>
          </ol>

          <div class="warn"><strong>\u26a0\ufe0f ${t('guide.trust_title')}</strong> ${t('guide.trust_desc')}</div>

          <h3>${t('guide.config_title')}</h3>
          <pre><code>${t('guide.config_example')}</code></pre>
        </div>
      `;
    },

    security() {
      return `
        <div class="page">
          <h2>${t('security.title')}</h2>
          <p class="lead">${t('security.intro')}</p>

          <h3>${t('security.sandbox_title')}</h3>
          <table class="sec-table">
            <thead><tr>
              <th>${t('security.vector')}</th>
              <th>${t('security.status')}</th>
              <th>${t('security.risk')}</th>
            </tr></thead>
            <tbody>
              <tr><td>os.execute() / os.exit()</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udd34 ${t('security.critical')}</td></tr>
              <tr><td>io.open() / io.popen()</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udd34 ${t('security.critical')}</td></tr>
              <tr><td>load() / loadfile() / dofile()</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udfe0 ${t('security.high')}</td></tr>
              <tr><td>debug.* (except traceback)</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udfe0 ${t('security.high')}</td></tr>
              <tr><td>string.dump()</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udfe0 ${t('security.high')}</td></tr>
              <tr><td>package.loadlib()</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udfe0 ${t('security.high')}</td></tr>
              <tr><td>storage \u8def\u5f84\u7a7f\u8d8a</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udd34 ${t('security.critical')}</td></tr>
              <tr><td>capability.call \u5371\u9669\u80fd\u529b</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udd34 ${t('security.critical')}</td></tr>
              <tr><td>\u5185\u5b58\u8017\u5c3d</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udfe1 ${t('security.medium')}</td></tr>
              <tr><td>\u6b7b\u5faa\u73af</td><td class="blocked">${t('security.blocked')}</td><td>\ud83d\udfe2 ${t('security.low')}</td></tr>
            </tbody>
          </table>

          <h3>${t('security.arch_title')}</h3>
          <div class="arch-diagram">
            <div class="arch-layer"><span class="arch-label">${t('security.arch_layer1')}</span></div>
            <div class="arch-layer arch-metatable"><span class="arch-label">${t('security.arch_layer2')}</span></div>
            <div class="arch-layer arch-sanitized"><span class="arch-label">${t('security.arch_layer3')}</span></div>
            <div class="arch-layer arch-lualib"><span class="arch-label">${t('security.arch_layer4')}</span></div>
            <div class="arch-layer arch-chook"><span class="arch-label">${t('security.arch_layer5')}</span></div>
            <div class="arch-layer arch-os"><span class="arch-label">${t('security.arch_layer6')}</span></div>
          </div>

          <p><a href="https://github.com/zz6zz666/crush-claw/blob/master/SANDBOX_SECURITY.md" target="_blank">${t('security.detail_link')}</a></p>

          <div class="warn"><strong>\u26a0\ufe0f ${t('security.trust_title')}</strong> ${t('security.trust_desc')}</div>
        </div>
      `;
    },

    faq() {
      const items = [
        { q: t('faq.q1'), a: t('faq.a1') },
        { q: t('faq.q2'), a: t('faq.a2') },
        { q: t('faq.q3'), a: t('faq.a3') },
        { q: t('faq.q4'), a: t('faq.a4') },
      ];
      return `
        <div class="page">
          <h2>${t('faq.title')}</h2>
          <div class="faq-list">
            ${items.map(item => `
              <div class="faq-item open">
                <div class="faq-q" onclick="this.parentElement.classList.toggle('open')">${item.q}</div>
                <div class="faq-a">${item.a}</div>
              </div>
            `).join('')}
          </div>
        </div>
      `;
    },
  };

  /* ---- navigation ---- */

  document.addEventListener('click', e => {
    const tabEl = e.target.closest('[data-tab]');
    if (tabEl) {
      render(tabEl.dataset.tab);
    }
  });

  document.getElementById('langBtn').addEventListener('click', () => {
    lang = lang === 'zh' ? 'en' : 'zh';
    localStorage.setItem(STORAGE_KEY, lang);
    const activeTab = document.querySelector('.nav-links .active');
    render(activeTab ? activeTab.dataset.tab : 'overview');
  });

  document.getElementById('navToggle').addEventListener('click', () => {
    document.getElementById('navLinks').classList.toggle('open');
  });

  /* ---- lobster svg ---- */
  function loadLobsterSvg() {
    const el = document.getElementById('lobsterSvg');
    if (!el || el.dataset.loaded) return;
    fetch('images/claw-animation.svg?v=4')
      .then(r => { if (!r.ok) throw new Error('fetch failed'); return r.text(); })
      .then(svg => {
        const match = svg.match(/<svg[\s\S]*?<\/svg>/i);
        if (match) {
          el.innerHTML = match[0];
          el.dataset.loaded = '1';
        }
      })
      .catch(() => { el.textContent = '\ud83e\udd9e'; });
  }

  /* ---- window time ---- */
  function updateTaskbarTime() {
    const el = document.querySelector('.mock-taskbar-time');
    if (el) {
      const now = new Date();
      el.textContent = now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    }
  }
  setInterval(updateTaskbarTime, 30000);

  /* ---- init ---- */
  loadI18n().then(() => {
    render('overview');
    updateTaskbarTime();
  });
})();
