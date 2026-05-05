(function() {
  'use strict';

  const STORAGE_KEY = 'crush-lang';
  let lang = localStorage.getItem(STORAGE_KEY) || 'zh';
  let i18nData = {};

  /* ---- i18n data ---- */
  const i18nSources = {
    en: null,
    zh: null,
  };

  async function loadI18n() {
    try {
      const enResp = await fetch('i18n/en.json');
      const zhResp = await fetch('i18n/zh.json');
      i18nSources.en = await enResp.json();
      i18nSources.zh = await zhResp.json();
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
    document.getElementById('langBtn').textContent = lang === 'zh' ? 'EN' : '中文';
  }

  /* ---- content templates ---- */

  const pages = {
    overview(ctx) {
      return `
        <div class="hero">
          <h2>${t('overview.title')}</h2>
          <p class="subtitle">${t('overview.subtitle')}</p>
        </div>

        <div class="features">
          <div class="feature"><div class="icon">🦞</div><h4>${t('overview.feature1')}</h4><p>${t('overview.feature1_desc')}</p></div>
          <div class="feature"><div class="icon">🎨</div><h4>${t('overview.feature2')}</h4><p>${t('overview.feature2_desc')}</p></div>
          <div class="feature"><div class="icon">🛡️</div><h4>${t('overview.feature3')}</h4><p>${t('overview.feature3_desc')}</p></div>
          <div class="feature"><div class="icon">⌨️</div><h4>${t('overview.feature4')}</h4><p>${t('overview.feature4_desc')}</p></div>
          <div class="feature"><div class="icon">🪟</div><h4>${t('overview.feature5')}</h4><p>${t('overview.feature5_desc')}</p></div>
          <div class="feature"><div class="icon">💻</div><h4>${t('overview.feature6')}</h4><p>${t('overview.feature6_desc')}</p></div>
        </div>

        <h3>${t('overview.how_title')}</h3>
        <div class="flow">${t('overview.flow')}</div>

        <div class="warn"><strong>⚠️ ${t('overview.trust_title')}</strong> ${t('overview.trust_desc')}</div>
      `;
    },

    guide(ctx) {
      return `
        <h2>${t('guide.title')}</h2>

        <h3>${t('guide.server_title')}</h3>
        <div class="steps">
          <div class="step">${t('guide.step1')}</div>
          <div class="step">${t('guide.step2')}</div>
          <div class="step">${t('guide.step3')}</div>
          <div class="step">${t('guide.step4')}</div>
        </div>

        <h3>${t('guide.client_title')}</h3>
        <div class="steps">
          <div class="step">${t('guide.client_step1')}</div>
          <div class="step">${t('guide.client_step2')}</div>
          <div class="step">${t('guide.client_step3')}</div>
        </div>

        <h3>${t('guide.config_title')}</h3>
        <pre><code>{
  "llm": {
    "api_key": "sk-your-key",
    "model": "gpt-4o",
    "profile": "openai"
  },
  "channels": {
    "telegram": {
      "enabled": true,
      "bot_token": "your-bot-token"
    }
  }
}</code></pre>
      `;
    },

    security(ctx) {
      return `
        <h2>${t('security.title')}</h2>
        <p>${t('security.intro')}</p>

        <h3>${t('security.sandbox_title')}</h3>
        <table class="sec-table">
          <thead><tr><th>${t('security.vector')}</th><th>${t('security.status')}</th><th>${t('security.risk')}</th></tr></thead>
          <tbody>
            <tr><td>os.execute() / os.exit()</td><td class="blocked">${t('security.blocked')}</td><td>🔴 ${t('security.critical')}</td></tr>
            <tr><td>io.open() / io.popen()</td><td class="blocked">${t('security.blocked')}</td><td>🔴 ${t('security.critical')}</td></tr>
            <tr><td>load() / loadfile() / dofile()</td><td class="blocked">${t('security.blocked')}</td><td>🟠 ${t('security.high')}</td></tr>
            <tr><td>debug.* (except traceback)</td><td class="blocked">${t('security.blocked')}</td><td>🟠 ${t('security.high')}</td></tr>
            <tr><td>string.dump()</td><td class="blocked">${t('security.blocked')}</td><td>🟠 ${t('security.high')}</td></tr>
            <tr><td>package.loadlib()</td><td class="blocked">${t('security.blocked')}</td><td>🟠 ${t('security.high')}</td></tr>
            <tr><td>storage 路径穿越</td><td class="blocked">${t('security.blocked')}</td><td>🔴 ${t('security.critical')}</td></tr>
            <tr><td>capability.call 危险能力</td><td class="blocked">${t('security.blocked')}</td><td>🔴 ${t('security.critical')}</td></tr>
            <tr><td>内存耗尽</td><td class="blocked">${t('security.blocked')}</td><td>🟡 ${t('security.medium')}</td></tr>
            <tr><td>死循环</td><td class="blocked">${t('security.blocked')}</td><td>🟢 ${t('security.low')}</td></tr>
          </tbody>
        </table>

        <h3>${t('security.arch_title')}</h3>
        <div class="flow">${t('security.arch')}</div>

        <p><a href="https://github.com/zz6zz666/esp-agent/blob/master/SANDBOX_SECURITY.md" target="_blank">${t('security.detail_link')}</a></p>

        <div class="warn"><strong>⚠️ ${t('security.trust_title')}</strong> ${t('security.trust_desc')}</div>
      `;
    },

    faq(ctx) {
      const items = [
        { q: t('faq.q1'), a: t('faq.a1') },
        { q: t('faq.q2'), a: t('faq.a2') },
        { q: t('faq.q3'), a: t('faq.a3') },
        { q: t('faq.q4'), a: t('faq.a4') },
      ];
      return `
        <h2>${t('faq.title')}</h2>
        ${items.map((item, i) => `
          <div class="faq-item">
            <div class="faq-q" onclick="this.parentElement.classList.toggle('open')">${item.q}</div>
            <div class="faq-a">${item.a}</div>
          </div>
        `).join('')}
      `;
    },
  };

  /* ---- render ---- */
  function render(tab) {
    const container = document.getElementById('content');
    container.innerHTML = pages[tab] ? pages[tab]() : pages.overview();

    document.querySelectorAll('.nav-item').forEach(el => {
      el.classList.toggle('active', el.dataset.tab === tab);
    });

    applyI18n();
  }

  /* ---- nav ---- */
  document.getElementById('nav').addEventListener('click', e => {
    const item = e.target.closest('.nav-item');
    if (item) {
      render(item.dataset.tab);
    }
  });

  /* ---- lang switch ---- */
  document.getElementById('langBtn').addEventListener('click', () => {
    lang = lang === 'zh' ? 'en' : 'zh';
    localStorage.setItem(STORAGE_KEY, lang);
    const currentTab = document.querySelector('.nav-item.active');
    render(currentTab ? currentTab.dataset.tab : 'overview');
  });

  /* ---- init ---- */
  loadI18n().then(() => {
    render('overview');
  });
})();
