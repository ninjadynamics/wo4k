'use strict';
const catalog = JSON.parse(document.querySelector('#catalog').textContent);
const textures = catalog.textures;
const choices = new Map(textures.map(item => [item.id, item.defaultVersion]));
const storageKey = `wo4k-texture-choices-v${catalog.catalogRevision}`;
try {
  const saved = JSON.parse(localStorage.getItem(storageKey) || '[]');
  if (Array.isArray(saved)) for (const [id, version] of saved) {
    if (choices.has(id) && (version === 'ps1' || version === 'pc')) choices.set(id, version);
  }
} catch (_) { /* The export still works when browser storage is unavailable. */ }

const grid = document.querySelector('#textures');
const selectors = new Map();
const cards = new Map();
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
function updateSummary() {
  const ps1 = [...choices.values()].filter(value => value === 'ps1').length;
  const changed = textures.filter(item => choices.get(item.id) !== item.defaultVersion).length;
  document.querySelector('#header-status').textContent = `${ps1} PS1 · ${textures.length - ps1} PC · ${changed} changed`;
  document.querySelector('#summary').replaceChildren();
  const stats = [[textures.length, 'textures'], [ps1, 'PS1 selected'], [textures.length - ps1, 'PC selected'], [changed, 'changed']];
  for (const [count, label] of stats) {
    const node = document.createElement('span'); node.className = 'stat';
    const strong = document.createElement('strong'); strong.textContent = count;
    node.append(strong, document.createTextNode(label)); document.querySelector('#summary').append(node);
  }
}
function updateChoice(item) {
  const version = choices.get(item.id);
  selectors.get(item.id).value = version;
  selectors.get(item.id).className = `${version}-choice`;
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
    const show = (!group || item.group === group) && (!query || `${item.id} ${item.title} ${item.group}`.toLowerCase().includes(query)) && (!changedOnly || choices.get(item.id) !== item.defaultVersion);
    cards.get(item.id).hidden = !show;
    if (show) ++visible;
  }
  document.querySelector('#result-count').textContent = `${visible} of ${textures.length} textures`;
  document.querySelector('#empty').hidden = visible !== 0;
}
for (const group of [...new Set(textures.map(item => item.group))]) {
  const option = new Option(group, group); document.querySelector('#group').append(option);
}
for (const [index, item] of textures.entries()) {
  const card = document.createElement('article'); card.className = 'card'; card.dataset.textureId = item.id;
  const heading = document.createElement('div'); heading.className = 'card-title';
  const title = document.createElement('h2'); title.textContent = item.title;
  const frame = document.createElement('span'); frame.className = 'frame'; frame.textContent = `#${String(item.frame).padStart(4, '0')} · ${item.width}×${item.height}`;
  heading.append(title, frame);
  const path = document.createElement('div'); path.className = 'asset-path'; path.textContent = item.source; path.title = item.source;
  const compare = document.createElement('div'); compare.className = 'compare';
  const pc = new Image(); pc.src = item.pc; pc.alt = `PC ${item.id}`; pc.loading = 'lazy'; pc.draggable = false;
  const ps1Layer = document.createElement('div'); ps1Layer.className = 'ps1-layer';
  const ps1 = new Image(); ps1.src = item.ps1; ps1.alt = `PS1 ${item.id}`; ps1.loading = 'lazy'; ps1.draggable = false;
  ps1Layer.append(ps1);
  const ps1Label = document.createElement('span'); ps1Label.className = 'side ps1'; ps1Label.textContent = 'PS1';
  const pcLabel = document.createElement('span'); pcLabel.className = 'side pc'; pcLabel.textContent = 'PC';
  const divider = document.createElement('span'); divider.className = 'divider';
  const handle = document.createElement('span'); handle.className = 'handle'; handle.textContent = '↔'; divider.append(handle);
  const slider = document.createElement('input'); slider.type = 'range'; slider.min = 0; slider.max = 100; slider.value = 50;
  slider.setAttribute('aria-label', `PS1 left / PC right comparison for ${item.id}`);
  slider.setAttribute('aria-valuetext', '50% PS1, 50% PC');
  slider.addEventListener('input', () => {
    compare.style.setProperty('--split', `${slider.value}%`);
    slider.setAttribute('aria-valuetext', `${slider.value}% PS1, ${100 - Number(slider.value)}% PC`);
  });
  compare.append(pc, ps1Layer, ps1Label, pcLabel, divider, slider);
  const bottom = document.createElement('div'); bottom.className = 'card-bottom';
  const note = document.createElement('div'); note.className = 'texture-note';
  note.textContent = item.defaultVersion === 'pc' ? 'Byte-identical originals · PC reused' : item.group === 'Roads' ? 'PS1 colours · recovered from PC tile data' : item.pixelsIdentical ? 'Same RGB pixels · native data differs · PS1 preferred' : 'PS1 original · native data differs';
  const row = document.createElement('div'); row.className = 'choice-row';
  const label = document.createElement('label'); label.htmlFor = `choice-${index}`; label.textContent = 'Keep';
  const marker = document.createElement('span'); marker.className = 'edited-marker'; marker.textContent = 'Changed'; label.append(marker);
  const select = document.createElement('select'); select.id = `choice-${index}`; select.setAttribute('aria-label', `Version to keep for ${item.id}`);
  select.append(new Option('PS1', 'ps1'), new Option('PC', 'pc'));
  select.addEventListener('change', () => {
    choices.set(item.id, select.value); updateChoice(item); persist(); updateSummary(); applyFilters();
  });
  row.append(label, select); bottom.append(note, row); card.append(heading, path, compare, bottom); grid.append(card);
  selectors.set(item.id, select); cards.set(item.id, card); updateChoice(item);
}
for (const id of ['search', 'group', 'changed']) document.getElementById(id).addEventListener(id === 'search' ? 'input' : 'change', applyFilters);
document.querySelector('#reset').addEventListener('click', () => {
  for (const item of textures) { choices.set(item.id, item.defaultVersion); updateChoice(item); }
  persist(); updateSummary(); applyFilters(); notify('All choices reset to the current selection.');
});
function selectionDocument() {
  return {
    format: 'wo4k.texture-selection', version: 1, catalog_revision: catalog.catalogRevision,
    exported_at: new Date().toISOString(),
    choices: textures.map(item => ({id: item.id, source: item.source, frame: item.frame, kind: item.group, version: choices.get(item.id)}))
  };
}
document.querySelector('#export').addEventListener('click', () => {
  const documentData = selectionDocument();
  const blob = new Blob([JSON.stringify(documentData, null, 2) + '\n'], {type: 'application/json;charset=utf-8'});
  const url = URL.createObjectURL(blob); const link = document.createElement('a');
  link.href = url; link.download = `wo4k-texture-choices-${new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)}.json`;
  document.body.append(link); link.click(); link.remove(); setTimeout(() => URL.revokeObjectURL(url), 5000);
  notify(`Exported all ${textures.length} texture choices.`);
});
updateSummary(); applyFilters();
