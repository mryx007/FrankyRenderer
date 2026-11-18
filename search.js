export const wikiUrl = 'https://github.com/mryx007/FrankyRenderer/wiki/';
const sourceUrl = 'https://raw.githubusercontent.com/wiki/mryx007/FrankyRenderer/';
const anchorPattern = /<a id="([\p{L}\p{N}_-]+)"><\/a>/gu;

export function pagesFromSidebar(markdown) {
  const pages = new Map();
  for (const line of markdown.split(/\r?\n/)) {
    if (!line.startsWith('- ')) continue;
    for (const [, title, href] of line.matchAll(/\[([^\]]+)\]\(([^)]+)\)/g)) {
      if (!href.startsWith(wikiUrl)) continue;
      const id = href.slice(wikiUrl.length);
      if (!/^[\w-]+$/.test(id)) continue;
      pages.set(id, { id, title, language: id.startsWith('DE-') ? 'de' : 'en' });
    }
  }
  return [...pages.values()];
}

function plain(text) {
  return text.replace(/!?\[([^\]]*)\]\([^)]+\)/g, '$1')
    .replace(/<[^>]+>/g, '')
    .replace(/(^|\s)[-+*]\s+/, '$1')
    .replace(/[*`]|__/g, '')
    .replace(/\s+/g, ' ').trim();
}

export function indexPage(page, markdown) {
  const records = [];
  const slugs = new Map();
  let section = { title: page.title, anchor: '', lines: [] };
  let current = section;
  let pendingAnchor = '';

  function add(record) {
    records.push(record);
    return record;
  }

  add(section);
  for (const raw of markdown.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line === '---') continue;
    const anchors = [...line.matchAll(anchorPattern)].map(match => match[1]);
    const bare = line.replace(anchorPattern, '').trim();
    const heading = bare.match(/^#{1,6}\s+(.+)/);

    if (heading) {
      const title = plain(heading[1]);
      const slug = title.toLowerCase().replace(/[^\p{L}\p{N}\s_-]/gu, '').replace(/\s/g, '-');
      const duplicate = slugs.get(slug) ?? 0;
      slugs.set(slug, duplicate + 1);
      section = add({ title, anchor: anchors.at(-1) || pendingAnchor || (duplicate ? `${slug}-${duplicate}` : slug), lines: [] });
      current = section;
      pendingAnchor = '';
    } else if (!bare && anchors.length) {
      pendingAnchor = anchors.at(-1);
    } else if (bare.startsWith('|')) {
      const [title, ...cells] = bare.slice(1, -1).split(/(?<!\\)\|/).map(plain);
      if (!cells.length || /^[: -]+$/.test(title)) continue;
      if (anchors.length) {
        add({ title, anchor: anchors.at(-1), lines: [cells.join(' · ')] });
      } else {
        current.lines.push([title, ...cells].join(' · '));
      }
    } else if (bare.startsWith('<summary>') && !bare.includes('Sources and implementation references')) {
      current = add({ title: plain(bare), anchor: section.anchor, lines: [] });
    } else if (bare === '</details>') {
      current = section;
    } else if (!bare.startsWith('<') && !/^(- )?\[.*?\]\(#/.test(bare) && !bare.startsWith('[')) {
      const text = plain(bare);
      if (text && !['On this page', 'Auf dieser Seite'].includes(text)) current.lines.push(text);
    }
  }
  return records.filter(record => record.lines.length).map(record => ({ ...record, page }));
}

async function readSource(id) {
  const response = await fetch(`${sourceUrl}${id}.md`, { signal: AbortSignal.timeout(12000) });
  if (!response.ok) throw new Error(`Wiki source ${id} returned ${response.status}`);
  return response.text();
}

export async function loadWiki() {
  const pages = pagesFromSidebar(await readSource('_Sidebar'));
  if (!pages.length) throw new Error('The wiki sidebar contains no pages');
  const results = await Promise.allSettled(pages.map(async page => indexPage(page, await readSource(page.id))));
  const records = results.flatMap(result => result.status === 'fulfilled' ? result.value : []);
  if (!records.length) throw new Error('No wiki pages could be read');
  return { records, incomplete: results.some(result => result.status === 'rejected') };
}

function normalized(value) {
  return value.toLowerCase().normalize('NFKD').replace(/\p{M}/gu, '').replace(/ß/g, 'ss');
}

function excerpt(line, terms) {
  const length = 200;
  if (line.length <= length) return line;
  const position = Math.min(...terms.map(term => normalized(line).indexOf(term)).filter(index => index >= 0));
  const start = Number.isFinite(position) ? Math.max(0, position - 55) : 0;
  const end = start + length;
  return `${start ? '…' : ''}${line.slice(start, end).trim()}${end < line.length ? '…' : ''}`;
}

export function findResults(records, query, language) {
  const phrase = normalized(query.trim()).replace(/\s+/g, ' ');
  if (!phrase) return [];
  const terms = phrase.split(' ');
  const matches = [];
  for (const record of records) {
    const title = normalized(record.title);
    const page = normalized(record.page.title);
    const lines = record.lines.map(normalized);
    if (!terms.every(term => title.includes(term) || page.includes(term) || lines.some(line => line.includes(term)))) continue;

    let index = 0;
    let best = -1;
    for (const [i, line] of lines.entries()) {
      const score = terms.filter(term => line.includes(term)).length + (line.includes(phrase) ? 2 : 0);
      if (score > best) {
        best = score;
        index = i;
      }
    }
    const score = (title === phrase || title.startsWith(`${phrase} — `) ? 25 : title.includes(phrase) ? 14 : 0)
      + (page === phrase ? (record.anchor ? 2 : 12) : 0)
      + terms.filter(term => title.includes(term)).length * 3
      + best + (record.page.language === language ? 1 : 0);
    matches.push({ ...record, excerpt: excerpt(record.lines[index], terms), score });
  }
  matches.sort((a, b) => b.score - a.score);
  const seen = new Set();
  return matches.filter(match => {
    const key = `${match.page.id}#${match.anchor}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}
