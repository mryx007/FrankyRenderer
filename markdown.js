import MarkdownIt from './vendor/markdown-it-15.0.3.min.mjs';
import { headingSlug, wikiUrl } from './search.js?v=4';

const markdown = new MarkdownIt({ html: true });
const expectedTag = /<\/?(?:details|summary)>|<a id="([\p{L}\p{N}_-]+)">|<\/a>/gu;

// Wiki HTML is limited to named anchors and collapsible sections. Escape everything else.
function renderWikiTags(tokens, index) {
  const source = tokens[index].content;
  const escape = markdown.utils.escapeHtml;
  let result = '';
  let last = 0;
  for (const tag of source.matchAll(expectedTag)) {
    result += escape(source.slice(last, tag.index));
    result += tag[1] ? `<a id="doc-${tag[1]}">` : tag[0];
    last = tag.index + tag[0].length;
  }
  return result + escape(source.slice(last));
}

markdown.renderer.rules.html_block = renderWikiTags;
markdown.renderer.rules.html_inline = renderWikiTags;

markdown.renderer.rules.heading_open = (tokens, index, options, env, renderer) => {
  const text = tokens[index + 1].children?.map(token => token.content || '').join('') ?? '';
  const slug = headingSlug(text, env.slugs);
  if (slug) tokens[index].attrSet('id', `doc-${slug}`);
  return renderer.renderToken(tokens, index, options);
};

export function renderMarkdown(source) {
  const content = source.trim()
    .replace(/^(?:\[English\]|\*\*English\*\*)[^\n]*\n+/, '')
    .replace(/^\[(?:Home|Startseite)\]\([^)]+\) · \[(?:Using the menu|Menü bedienen)\][^\n]*\n+/, '');
  return markdown.render(content, { slugs: new Map() });
}

export function resolveWikiLink(href, pageId, knownPages) {
  const local = href.startsWith(wikiUrl) ? href.slice(wikiUrl.length) : href;
  const [name, encoded] = local.split('#', 2);
  const target = name || pageId;
  if (!knownPages.has(target)) return null;
  let anchor = '';
  try { anchor = decodeURIComponent(encoded || ''); } catch { return null; }
  return { id: target, anchor };
}
