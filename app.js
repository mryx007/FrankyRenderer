import { findResults, loadPages, loadWiki, readPage, wikiUrl } from './search.js?v=3';
import { renderMarkdown, resolveWikiLink } from './markdown.js?v=3';

const translations = {
  de: {
    home: 'Startseite', chapters: 'Kapitel', sections: 'Auf dieser Seite', menu: 'Kapitel öffnen',
    skip: 'Zum Inhalt', language: 'Sprache', search: 'Wiki durchsuchen', close: 'Suche schließen',
    shortcut: 'Strg K', query: 'Suchbegriff', example: 'z. B. Debanding oder Dolby Vision',
    loading: 'Wiki wird geladen …', noMatches: 'Keine Treffer gefunden.',
    loadError: 'Das Wiki konnte nicht geladen werden.', pageError: 'Dieses Kapitel konnte nicht geladen werden.',
    missing: 'Dieses Kapitel gibt es nicht.', incomplete: 'Einige Wiki-Seiten konnten nicht geladen werden.',
    retry: 'Erneut laden', more: 'Mehr Treffer anzeigen', source: 'Quellseite auf GitHub',
    intro: 'Die Anleitung für Bildqualität, HDR, Skalierung und Wiedergabe.',
    menuGuide: 'Menü bedienen', filterGuide: 'Filter auswählen',
    groupNames: ['Einstieg', 'Bild und Wiedergabe', 'Einrichtung und Hilfe'],
    count: (shown, total) => shown < total ? `${shown} von ${total} Treffern` : `${total} Treffer`,
  },
  en: {
    home: 'Home', chapters: 'Chapters', sections: 'On this page', menu: 'Open chapters',
    skip: 'Skip to content', language: 'Language', search: 'Search the wiki', close: 'Close search',
    shortcut: 'Ctrl K', query: 'Search term', example: 'e.g. Debanding or Dolby Vision',
    loading: 'Loading the wiki…', noMatches: 'No results found.',
    loadError: 'Could not load the wiki.', pageError: 'Could not load this chapter.',
    missing: 'This chapter does not exist.', incomplete: 'Some wiki pages could not be loaded.',
    retry: 'Try again', more: 'Show more results', source: 'Source page on GitHub',
    intro: 'The guide to picture quality, HDR, scaling and playback.',
    menuGuide: 'Using the menu', filterGuide: 'Choosing a filter',
    groupNames: ['Getting started', 'Picture and playback', 'Setup and help'],
    count: (shown, total) => shown < total ? `${shown} of ${total} results` : `${total} ${total === 1 ? 'result' : 'results'}`,
  },
};
const groups = [
  ['Home', 'Using-the-menu', 'General'],
  ['Processing', 'Black-Bars-and-Zoom', 'Scaling', 'Color-Adjustment', 'Color-Management', 'HDR-and-Color-Mapping', 'Shaders'],
  ['Profiles', 'Shortcuts', 'Troubleshooting', 'Sources'],
];
const colors = {
  Home: 'violet', 'Using-the-menu': 'blue', General: 'violet', Processing: 'teal',
  'Black-Bars-and-Zoom': 'rose', Scaling: 'blue', 'Color-Adjustment': 'amber', 'Color-Management': 'green',
  'HDR-and-Color-Mapping': 'violet', Shaders: 'rose', Profiles: 'blue', Shortcuts: 'amber',
  Troubleshooting: 'teal', Sources: 'green',
};

const byId = id => document.getElementById(id);
const appPath = new URL('./', location.href).pathname;
const article = byId('article');
const content = byId('content');
const layout = content.parentElement;
const breadcrumb = byId('breadcrumb');
const chapters = byId('chapters');
const sections = byId('sections');
const sectionPanel = byId('section-panel');
const menu = byId('menu');
const dialog = byId('search-dialog');
const query = byId('query');
const results = byId('results');
const status = byId('status');
const retry = byId('retry');
const more = byId('more');
const pageSize = 12;

let pages = new Map();
let active = readRoute();
let displayedId;
let routeVersion = 0;
let index;
let indexRequest;
let limit = pageSize;

function chapterId(id) {
  return id.replace(/^DE-/, '');
}

function languageOf(id) {
  return id.startsWith('DE-') ? 'de' : 'en';
}

function readRoute() {
  const params = new URLSearchParams(location.search);
  const requested = params.get('page') || 'Home';
  const language = requested.startsWith('DE-') || params.get('lang') !== 'en' ? 'de' : 'en';
  const id = language === 'de' ? `DE-${chapterId(requested)}` : requested;
  let anchor = '';
  try { anchor = decodeURIComponent(location.hash.substring(1)); } catch { /* leave a malformed fragment empty */ }
  return { language, id, anchor };
}

function routeHref(id, anchor = '') {
  const params = new URLSearchParams();
  if (languageOf(id) === 'en') params.set('lang', 'en');
  if (chapterId(id) !== 'Home') params.set('page', chapterId(id));
  return `${appPath}${params.size ? `?${params}` : ''}${anchor ? `#${encodeURIComponent(anchor)}` : ''}`;
}

function element(tag, text, className) {
  const node = document.createElement(tag);
  if (text) node.textContent = text;
  if (className) node.className = className;
  return node;
}

function routeLink(label, id, anchor = '') {
  const link = element('a', label);
  link.href = routeHref(id, anchor);
  link.dataset.route = '';
  return link;
}

function setLanguage() {
  const locale = translations[active.language];
  document.documentElement.lang = active.language;
  byId('home-link').href = routeHref(active.language === 'de' ? 'DE-Home' : 'Home');
  byId('skip').textContent = locale.skip;
  byId('language').setAttribute('aria-label', locale.language);
  menu.setAttribute('aria-label', locale.menu);
  chapters.setAttribute('aria-label', locale.chapters);
  sections.setAttribute('aria-label', locale.sections);
  byId('search-caption').textContent = locale.search;
  byId('search-heading').textContent = locale.search;
  byId('close-search').setAttribute('aria-label', locale.close);
  byId('shortcut').textContent = locale.shortcut;
  byId('query-label').textContent = locale.query;
  query.placeholder = locale.example;
  retry.textContent = locale.retry;
  more.textContent = locale.more;

  for (const language of ['de', 'en']) {
    const link = byId(`lang-${language}`);
    const target = (language === 'de' ? 'DE-' : '') + chapterId(active.id);
    link.href = routeHref(pages.has(target) ? target : (language === 'de' ? 'DE-Home' : 'Home'), active.anchor);
    if (language === active.language) link.setAttribute('aria-current', 'true');
    else link.removeAttribute('aria-current');
  }
}

function buildNavigation() {
  const prefix = active.language === 'de' ? 'DE-' : '';
  const available = [...pages.values()].filter(page => page.language === active.language);
  const assigned = new Set(groups.flat());
  const categories = [groups[0], groups[1], [...groups[2], ...available.map(page => chapterId(page.id)).filter(id => !assigned.has(id))]];
  chapters.replaceChildren();

  categories.forEach((ids, group) => {
    const wrapper = element('section', '', 'nav-group');
    const title = element('h2', translations[active.language].groupNames[group]);
    const list = element('ul');
    for (const slug of ids) {
      const page = pages.get(prefix + slug);
      if (!page) continue;
      const item = element('li');
      const link = routeLink(page.title, page.id);
      link.dataset.color = colors[slug] || 'violet';
      if (page.id === active.id) link.setAttribute('aria-current', 'page');
      item.append(link);
      list.append(item);
    }
    if (list.childElementCount) {
      wrapper.append(title, list);
      chapters.append(wrapper);
    }
  });
}

function convertHome(doc, page) {
  const locale = translations[page.language];
  const prefix = page.language === 'de' ? 'DE-' : '';
  const hero = element('div', '', 'hero');
  const actions = element('div', '', 'hero-actions');
  actions.append(
    routeLink(locale.menuGuide, prefix + 'Using-the-menu'),
    routeLink(locale.filterGuide, prefix + 'Scaling', 'choosing-a-filter'),
  );
  hero.append(element('h1', 'FrankyRenderer Wiki'), element('p', locale.intro), actions);
  const initial = doc.firstElementChild;
  if (initial?.tagName === 'P' && /^(?:Welcome|Willkommen)/.test(initial.textContent)) initial.remove();
  if (doc.firstElementChild?.tagName === 'P') doc.firstElementChild.className = 'home-intro';

  const table = doc.querySelector('table');
  const cards = element('div', '', 'cards');
  for (const row of table?.querySelectorAll('tbody tr') ?? []) {
    const [name, description] = row.cells;
    const link = name?.querySelector('a[data-route]');
    if (!link || !description) continue;
    const tile = element('div', '', 'card');
    const url = new URL(link.href, location.href);
    tile.dataset.color = colors[url.searchParams.get('page')] || 'violet';
    link.className = 'card-title';
    const copy = element('p');
    copy.append(...description.childNodes);
    tile.append(link, copy);
    cards.append(tile);
  }
  if (cards.childElementCount) table.replaceWith(cards);
  doc.prepend(hero);
}

function renderChapter(page, source) {
  const doc = document.createElement('template');
  doc.innerHTML = renderMarkdown(source);
  for (const link of doc.content.querySelectorAll('a[href]')) {
    const target = resolveWikiLink(link.getAttribute('href'), page.id, pages);
    if (!target) continue;
    link.href = routeHref(target.id, target.anchor);
    link.dataset.route = '';
  }

  const home = chapterId(page.id) === 'Home';
  if (home) convertHome(doc.content, page);
  else doc.content.prepend(element('h1', page.title));

  for (const table of doc.content.querySelectorAll('table')) {
    const wrapper = element('div', '', 'table-wrap');
    table.replaceWith(wrapper);
    wrapper.append(table);
  }

  const sourceLink = element('a', translations[page.language].source);
  sourceLink.href = wikiUrl + page.id;
  const footer = element('p', '', 'article-source');
  footer.append(sourceLink);
  article.replaceChildren(doc.content, footer);
  layout.classList.toggle('home', home);
  breadcrumb.hidden = home;
  breadcrumb.replaceChildren(routeLink(translations[page.language].home, page.language === 'de' ? 'DE-Home' : 'Home'), document.createTextNode(' / ' + page.title));
  buildSectionList(home);
}

function buildSectionList(home) {
  const titles = [...article.querySelectorAll('h2[id^="doc-"]')];
  sectionPanel.hidden = home || !titles.length;
  sections.replaceChildren();
  if (sectionPanel.hidden) return;
  const label = translations[active.language].sections;
  const inlineTitle = [...article.children].find(node => node.tagName === 'P' && node.textContent.trim() === label);
  const inlineList = inlineTitle?.nextElementSibling;
  if (inlineList?.tagName === 'UL') {
    inlineTitle.classList.add('inline-toc');
    inlineList.classList.add('inline-toc');
    const list = inlineList.cloneNode(true);
    list.classList.remove('inline-toc');
    sections.append(element('h2', label), list);
    return;
  }
  const list = element('ul');
  for (const heading of titles) {
    const item = element('li');
    item.append(routeLink(heading.textContent, active.id, heading.id.slice(4)));
    list.append(item);
  }
  sections.append(element('h2', label), list);
}

function jump(anchor, focus) {
  if (anchor) {
    const target = byId(`doc-${anchor}`);
    if (target) {
      for (let detail = target.closest('details'); detail; detail = detail.parentElement?.closest('details')) detail.open = true;
      target.scrollIntoView({ block: 'start' });
      if (focus) {
        target.tabIndex = -1;
        target.focus({ preventScroll: true });
      }
      return;
    }
  }
  window.scrollTo(0, 0);
  if (focus) article.focus({ preventScroll: true });
}

function message(text, handler) {
  article.replaceChildren(element('p', text, 'loading'));
  if (handler) {
    const button = element('button', translations[active.language].retry, 'secondary-button');
    button.type = 'button';
    button.addEventListener('click', handler);
    article.append(button);
  }
  content.setAttribute('aria-busy', 'false');
}

async function showRoute(focus = false) {
  active = readRoute();
  setLanguage();
  buildNavigation();
  const id = active.id;
  const version = ++routeVersion;
  const locale = translations[active.language];
  const page = pages.get(id);
  document.title = page && chapterId(id) !== 'Home' ? `${page.title} · FrankyRenderer Wiki` : 'FrankyRenderer Wiki';
  if (id === displayedId) {
    jump(active.anchor, focus);
    return;
  }
  displayedId = undefined;
  breadcrumb.hidden = true;
  sectionPanel.hidden = true;
  if (!page) {
    message(pages.size ? locale.missing : locale.loadError, pages.size ? undefined : init);
    return;
  }

  content.setAttribute('aria-busy', 'true');
  article.replaceChildren(element('p', locale.loading, 'loading'));
  try {
    const source = await readPage(id);
    if (version !== routeVersion) return;
    renderChapter(page, source);
    displayedId = id;
    content.setAttribute('aria-busy', 'false');
    jump(active.anchor, focus);
  } catch {
    if (version === routeVersion) message(locale.pageError, () => showRoute(focus));
  }
}

function showResults() {
  if (!index) return;
  const locale = translations[active.language];
  const term = query.value.trim();
  results.replaceChildren();
  more.hidden = true;
  const warning = index.incomplete ? locale.incomplete : '';
  if (!term) {
    status.textContent = warning;
    return;
  }
  const matches = findResults(index.records, term, active.language);
  const shown = matches.slice(0, limit);
  more.hidden = shown.length === matches.length;
  const summary = shown.length ? locale.count(shown.length, matches.length) : locale.noMatches;
  status.textContent = [summary, warning].filter(Boolean).join(' ');
  for (const record of shown) {
    const item = element('li');
    item.lang = record.page.language;
    const language = record.page.language === 'de' ? 'Deutsch' : 'English';
    item.append(
      routeLink(record.title, record.page.id, record.anchor),
      element('p', `${record.page.title} · ${language}`, 'meta'),
      element('p', record.excerpt),
    );
    results.append(item);
  }
}

async function prepareSearch() {
  if (indexRequest) return indexRequest;
  retry.hidden = true;
  status.textContent = translations[active.language].loading;
  indexRequest = loadWiki(pages.size ? [...pages.values()] : undefined);
  try {
    index = await indexRequest;
    retry.hidden = !index.incomplete;
    showResults();
  } catch {
    status.textContent = translations[active.language].loadError;
    retry.hidden = false;
  } finally {
    indexRequest = undefined;
  }
}

function openSearch() {
  if (!dialog.open) dialog.showModal();
  query.focus();
  if (!index) prepareSearch();
  else showResults();
}

function toggleMenu(open) {
  byId('sidebar').classList.toggle('open', open);
  menu.setAttribute('aria-expanded', String(open));
}

document.addEventListener('click', event => {
  const link = event.target.closest('a[data-route]');
  if (!link || event.defaultPrevented || event.button !== 0 || event.altKey || event.ctrlKey || event.shiftKey || event.metaKey) return;
  event.preventDefault();
  toggleMenu(false);
  if (dialog.open) dialog.close();
  if (link.href !== location.href) history.pushState(null, '', link.href);
  showRoute(true);
});
window.addEventListener('popstate', () => {
  if (dialog.open) dialog.close();
  showRoute();
});
menu.addEventListener('click', () => toggleMenu(menu.getAttribute('aria-expanded') !== 'true'));
byId('open-search').addEventListener('click', openSearch);
byId('close-search').addEventListener('click', () => dialog.close());
document.addEventListener('keydown', event => {
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'k') {
    event.preventDefault();
    openSearch();
  } else if (event.key === 'Escape') toggleMenu(false);
});
query.addEventListener('input', () => {
  limit = pageSize;
  showResults();
});
byId('search').addEventListener('submit', event => {
  event.preventDefault();
  showResults();
});
retry.addEventListener('click', prepareSearch);
more.addEventListener('click', () => {
  const previousLimit = limit;
  limit += pageSize;
  showResults();
  results.children[previousLimit]?.querySelector('a')?.focus();
});

async function init() {
  setLanguage();
  article.replaceChildren(element('p', translations[active.language].loading, 'loading'));
  try {
    pages = new Map((await loadPages()).map(page => [page.id, page]));
    await showRoute();
  } catch {
    message(translations[active.language].loadError, init);
  }
}

init();
if (new URLSearchParams(location.search).has('search')) openSearch();
