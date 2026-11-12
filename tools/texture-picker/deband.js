'use strict';
/* Deband comparison: the original on the left, a debanded version on the right.
 * Each texture keeps one of: original, v1 (smoother) or v2 (keeps details). */
const catalog = JSON.parse(document.querySelector('#catalog').textContent);
const textures = catalog.textures;
const versions = ['original', 'v1', 'v2'];
const names = {original: 'Original', v1: 'Deband v1', v2: 'Deband v2'};
const choices = new Map(textures.map(item => [item.id, item.defaultVersion]));
const storageKey = `wo4k-deband-choices-${catalog.catalogRevision}`;
try {
  const saved = JSON.parse(localStorage.getItem(storageKey) || '[]');
  if (Array.isArray(saved)) for (const [id, version] of saved) {
    if (choices.has(id) && versions.includes(version)) choices.set(id, version);
  }
} catch (_) { /* The export still works when browser storage is unavailable. */ }

const grid = document.querySelector('#textures');
const selectors = new Map();
const cards = new Map();
const shown = new Map();
let noticeTimer;
function notify(message) {
  const node = document.querySelector('#notice');
  node.textContent = message;
  node.classList.add('visible');
  clearTimeout(noticeTimer);
  noticeTimer = setTimeout(() => node.classList.remove('visible'), 5000);
}
function persist() {
  try { localStorage.setItem(storageKey, JSON.stringify([...choices])); }
  catch (_) { notify('Browser storage is unavailable. Export JSON to keep your choices.'); }
}
function count(version) { return [...choices.values()].filter(value => value === version).length; }
function updateSummary() {
  const changed = textures.filter(item => choices.get(item.id) !== item.defaultVersion).length;
  document.querySelector('#header-status').textContent =
    `${count('original')} original · ${count('v1')} v1 · ${count('v2')} v2 · ${changed} changed`;
  document.querySelector('#summary').replaceChildren();
  const stats = [[textures.length, 'textures'], [count('original'), 'original kept'], [count('v1'), 'deband v1'],
                 [count('v2'), 'deband v2'], [changed, 'changed']];
  for (const [value, label] of stats) {
    const node = document.createElement('span'); node.className = 'stat';
    const strong = document.createElement('strong'); strong.textContent = value;
    node.append(strong, document.createTextNode(label)); document.querySelector('#summary').append(node);
  }
}
/* The right-hand side shows the chosen debanded version; with the original
 * kept it stays on the version last looked at. */
function updateChoice(item) {
  const version = choices.get(item.id);
  selectors.get(item.id).value = version;
  selectors.get(item.id).className = version === 'original' ? 'ps1-choice' : 'pc-choice';
  const view = shown.get(item.id);
  if (version !== 'original') view.version = version;
  view.image.src = item[view.version];
  view.label.textContent = names[view.version];
  view.note.textContent = item.identical[view.version] ? `${names[view.version]} left this texture unchanged`
    : `${names[view.version]} changed this texture`;
  const edited = version !== item.defaultVersion;
  cards.get(item.id).classList.toggle('edited', edited);
  cards.get(item.id).querySelector('.edited-marker').hidden = !edited;
}
function applyFilters() {
  const query = document.querySelector('#search').value.trim().toLowerCase();
  const group = document.querySelector('#group').value;
  const changedOnly = document.querySelector('#changed').checked;
  let visible = 0;
  for (const item of textures) {
    const show = (!group || item.group === group) &&
      (!query || `${item.id} ${item.title} ${item.group}`.toLowerCase().includes(query)) &&
      (!changedOnly || choices.get(item.id) !== item.defaultVersion);
    cards.get(item.id).hidden = !show;
    if (show) ++visible;
  }
  document.querySelector('#result-count').textContent = `${visible} of ${textures.length} textures`;
  document.querySelector('#empty').hidden = visible !== 0;
}
for (const group of [...new Set(textures.map(item => item.group))]) {
  document.querySelector('#group').append(new Option(group, group));
}
for (const [index, item] of textures.entries()) {
  const card = document.createElement('article'); card.className = 'card'; card.dataset.textureId = item.id;
  const heading = document.createElement('div'); heading.className = 'card-title';
  const title = document.createElement('h2'); title.textContent = item.title;
  const frame = document.createElement('span'); frame.className = 'frame';
  frame.textContent = `#${String(item.frame).padStart(4, '0')} · ${item.width}×${item.height}`;
  heading.append(title, frame);
  const path = document.createElement('div'); path.className = 'asset-path'; path.textContent = item.source; path.title = item.source;
  const compare = document.createElement('div'); compare.className = 'compare';
  const right = new Image(); right.alt = `Debanded ${item.id}`; right.loading = 'lazy'; right.draggable = false;
  const leftLayer = document.createElement('div'); leftLayer.className = 'ps1-layer';
  const left = new Image(); left.src = item.original; left.alt = `Original ${item.id}`; left.loading = 'lazy'; left.draggable = false;
  leftLayer.append(left);
  const leftLabel = document.createElement('span'); leftLabel.className = 'side ps1'; leftLabel.textContent = 'Original';
  const rightLabel = document.createElement('span'); rightLabel.className = 'side pc';
  const divider = document.createElement('span'); divider.className = 'divider';
  const handle = document.createElement('span'); handle.className = 'handle'; handle.textContent = '↔'; divider.append(handle);
  const slider = document.createElement('input'); slider.type = 'range'; slider.min = 0; slider.max = 100; slider.value = 50;
  slider.setAttribute('aria-label', `Original left / debanded right comparison for ${item.id}`);
  slider.addEventListener('input', () => compare.style.setProperty('--split', `${slider.value}%`));
  compare.append(right, leftLayer, leftLabel, rightLabel, divider, slider);
  const bottom = document.createElement('div'); bottom.className = 'card-bottom';
  const note = document.createElement('div'); note.className = 'texture-note';
  const row = document.createElement('div'); row.className = 'choice-row';
  const label = document.createElement('label'); label.htmlFor = `choice-${index}`; label.textContent = 'Keep';
  const marker = document.createElement('span'); marker.className = 'edited-marker'; marker.textContent = 'Changed'; label.append(marker);
  const select = document.createElement('select'); select.id = `choice-${index}`;
  select.setAttribute('aria-label', `Version to keep for ${item.id}`);
  for (const version of versions) select.append(new Option(names[version], version));
  select.addEventListener('change', () => {
    choices.set(item.id, select.value); updateChoice(item); persist(); updateSummary(); applyFilters();
  });
  row.append(label, select); bottom.append(note, row); card.append(heading, path, compare, bottom); grid.append(card);
  selectors.set(item.id, select); cards.set(item.id, card);
  shown.set(item.id, {version: item.defaultVersion === 'original' ? 'v1' : item.defaultVersion, image: right, label: rightLabel, note});
  updateChoice(item);
}
for (const id of ['search', 'group', 'changed']) {
  document.getElementById(id).addEventListener(id === 'search' ? 'input' : 'change', applyFilters);
}
document.querySelector('#reset').addEventListener('click', () => {
  for (const item of textures) { choices.set(item.id, item.defaultVersion); updateChoice(item); }
  persist(); updateSummary(); applyFilters(); notify('All choices reset to deband v1.');
});
document.querySelector('#export').addEventListener('click', () => {
  const documentData = {
    format: 'wo4k.deband-selection', version: 1, catalog_revision: catalog.catalogRevision,
    exported_at: new Date().toISOString(),
    choices: textures.map(item => ({id: item.id, file: item.file, source: item.source, frame: item.frame,
                                    kind: item.group, version: choices.get(item.id)}))
  };
  const blob = new Blob([JSON.stringify(documentData, null, 2) + '\n'], {type: 'application/json;charset=utf-8'});
  const url = URL.createObjectURL(blob); const link = document.createElement('a');
  link.href = url; link.download = `wo4k-deband-choices-${new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)}.json`;
  document.body.append(link); link.click(); link.remove(); setTimeout(() => URL.revokeObjectURL(url), 5000);
  notify(`Exported all ${textures.length} choices.`);
});
updateSummary(); applyFilters();
