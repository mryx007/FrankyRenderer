import { findResults, loadWiki, wikiUrl } from './search.js';

const language = new URLSearchParams(location.search).get('lang') === 'en' ? 'en' : 'de';
const labels = {
  de: {
    heading: 'Wiki durchsuchen', query: 'Suchbegriff', example: 'z. B. Debanding oder Dolby Vision',
    submit: 'Suchen', loading: 'Wiki wird geladen …', noMatches: 'Keine Treffer gefunden.',
    error: 'Das Wiki konnte nicht geladen werden.', incomplete: 'Einige Wiki-Seiten konnten nicht geladen werden.',
    retry: 'Erneut laden', count: (shown, total) => shown < total ? `${shown} von ${total} Treffern` : `${total} Treffer`,
  },
  en: {
    heading: 'Search the wiki', query: 'Search term', example: 'e.g. Debanding or Dolby Vision',
    submit: 'Search', loading: 'Loading the wiki…', noMatches: 'No results found.',
    error: 'Could not load the wiki.', incomplete: 'Some wiki pages could not be loaded.',
    retry: 'Try again', count: (shown, total) => shown < total ? `${shown} of ${total} results` : `${total} ${total === 1 ? 'result' : 'results'}`,
  },
}[language];

const byId = id => document.getElementById(id);
const query = byId('query');
const status = byId('status');
const results = byId('results');
const retry = byId('retry');
const switchLanguage = byId('switch-language');
let index;

document.documentElement.lang = language;
document.title = `${labels.heading} · FrankyRenderer`;
byId('heading').textContent = labels.heading;
byId('query-label').textContent = labels.query;
query.placeholder = labels.example;
byId('submit').textContent = labels.submit;
retry.textContent = labels.retry;
if (language === 'en') {
  byId('wiki-link').href = `${wikiUrl}Home`;
  switchLanguage.href = './';
  switchLanguage.textContent = 'Deutsch';
  switchLanguage.lang = 'de';
}

function showResults() {
  if (!index) return;
  results.replaceChildren();
  const term = query.value.trim();
  const warning = index.incomplete ? labels.incomplete : '';
  if (!term) {
    status.textContent = warning;
    return;
  }
  const matches = findResults(index.records, term, language);
  const shown = matches.slice(0, 12);
  const summary = shown.length ? labels.count(shown.length, matches.length) : labels.noMatches;
  status.textContent = [summary, warning].filter(Boolean).join(' ');
  for (const record of shown) {
    const item = document.createElement('li');
    item.lang = record.page.language;
    const link = document.createElement('a');
    link.href = `${wikiUrl}${record.page.id}${record.anchor ? `#${encodeURIComponent(record.anchor)}` : ''}`;
    link.textContent = record.title;
    const meta = document.createElement('p');
    meta.className = 'meta';
    meta.textContent = `${record.page.title} · ${record.page.language === 'de' ? 'Deutsch' : 'English'}`;
    const excerpt = document.createElement('p');
    excerpt.textContent = record.excerpt;
    item.append(link, meta, excerpt);
    results.append(item);
  }
}

async function load() {
  retry.hidden = true;
  status.textContent = labels.loading;
  try {
    index = await loadWiki();
    retry.hidden = !index.incomplete;
    showResults();
  } catch {
    status.textContent = labels.error;
    retry.hidden = false;
  }
}

query.addEventListener('input', showResults);
byId('search').addEventListener('submit', event => {
  event.preventDefault();
  showResults();
});
retry.addEventListener('click', load);
load();
