/*
 * Builds the settings page opened from showConfiguration.
 *
 * Self-contained (no external hosting, no Clay dependency) and served as a
 * data: URL. Follows the phone's light/dark setting via a `color-scheme` meta
 * tag plus a `prefers-color-scheme` media query, the same approach as
 * PebbleSuperProductivity and PebbleHourlyChime.
 */
'use strict';

var SECTIONS = [
  ['home', 'Top Stories'],
  ['world', 'World'],
  ['us', 'U.S.'],
  ['politics', 'Politics'],
  ['business', 'Business'],
  ['technology', 'Technology'],
  ['science', 'Science'],
  ['health', 'Health'],
  ['sports', 'Sports'],
  ['arts', 'Arts'],
  ['books', 'Books'],
  ['movies', 'Movies'],
  ['food', 'Food'],
  ['travel', 'Travel']
];

var BACKLIGHT_MODES = [
  ['0', 'System default'],
  ['5', '5 seconds after a button press'],
  ['15', '15 seconds after a button press'],
  ['30', '30 seconds after a button press'],
  ['60', '60 seconds after a button press'],
  ['-1', 'Always on (uses much more battery)']
];

function escapeHtmlAttr(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

function optionList(pairs, selected) {
  return pairs.map(function (p) {
    var sel = p[0] === selected ? ' selected' : '';
    return '<option value="' + escapeHtmlAttr(p[0]) + '"' + sel + '>' + p[1] + '</option>';
  }).join('');
}

function buildConfigPageUrl(settings) {
  settings = settings || {};
  var apiKey = settings.ApiKey || '';
  var section = settings.Section || 'home';
  var backlightMode = String(
    typeof settings.BacklightMode === 'undefined' ? '0' : settings.BacklightMode
  );

  var html = '<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">' +
    '<meta name="color-scheme" content="light dark">' +
    '<title>Top Stories</title><style>' +
    ':root{--bg:#efeff4;--grp:#fff;--border:#d1d1d6;--fg:#1c1c1e;--hint:#6c6c70;' +
    '--field-bg:#e9e9eb;--field-fg:#1c1c1e;--accent:#007aff}' +
    '@media (prefers-color-scheme:dark){:root{--bg:#1c1c1e;--grp:#2c2c2e;--border:#3a3a3c;' +
    '--fg:#f2f2f7;--hint:#8e8e93;--field-bg:#3a3a3c;--field-fg:#fff;--accent:#0a84ff}}' +
    'body{font-family:-apple-system,Roboto,Helvetica,sans-serif;margin:0;background:var(--bg);color:var(--fg)}' +
    'h1{font-size:19px;font-weight:600;padding:18px 16px 4px}' +
    'h2{font-size:13px;font-weight:600;text-transform:uppercase;letter-spacing:.04em;' +
    'color:var(--hint);padding:18px 16px 6px}' +
    '.grp{background:var(--grp);margin:0;border-top:1px solid var(--border);border-bottom:1px solid var(--border)}' +
    '.row{display:flex;align-items:center;justify-content:space-between;padding:12px 16px;border-bottom:1px solid var(--border)}' +
    '.row:last-child{border-bottom:0}' +
    '.row.stack{display:block}' +
    '.row label{flex:1;font-size:16px}' +
    '.row.stack label{display:block;margin-bottom:8px}' +
    'select{font-size:16px;background:var(--field-bg);color:var(--field-fg);border:0;border-radius:8px;padding:6px 8px;max-width:60%}' +
    'input[type=text]{width:100%;box-sizing:border-box;font-size:16px;background:var(--field-bg);' +
    'color:var(--field-fg);border:0;border-radius:8px;padding:10px}' +
    '.hint{font-size:12px;color:var(--hint);padding:6px 16px 14px}' +
    '.hint a{color:var(--accent)}' +
    'button{width:calc(100% - 32px);margin:20px 16px 40px;padding:14px;font-size:17px;font-weight:600;' +
    'border:0;border-radius:12px;background:var(--accent);color:#fff}' +
    '</style></head><body>' +

    '<h1>Top Stories</h1>' +
    '<div class="hint">Shows the New York Times most prominent stories. ' +
    'Needs a free API key from ' +
    '<a href="https://developer.nytimes.com" target="_blank" rel="noopener">developer.nytimes.com</a> ' +
    'with the Top Stories API enabled.</div>' +

    '<h2>NYTimes API</h2>' +
    '<div class="grp">' +
      '<div class="row stack">' +
        '<label for="apiKey">API Key</label>' +
        '<input type="text" id="apiKey" autocorrect="off" autocapitalize="off" ' +
        'spellcheck="false" placeholder="paste key here" value="' + escapeHtmlAttr(apiKey) + '">' +
      '</div>' +
      '<div class="row">' +
        '<label for="section">Section</label>' +
        '<select id="section">' + optionList(SECTIONS, section) + '</select>' +
      '</div>' +
    '</div>' +

    '<h2>Display</h2>' +
    '<div class="grp">' +
      '<div class="row">' +
        '<label for="backlight">Backlight</label>' +
        '<select id="backlight">' + optionList(BACKLIGHT_MODES, backlightMode) + '</select>' +
      '</div>' +
    '</div>' +
    '<div class="hint">"System default" never touches the backlight - it behaves ' +
    'exactly as your watch\'s own Settings say. Any other option overrides that ' +
    'while this app is open, relighting the screen on Select or when you scroll ' +
    'the list, and keeping it lit for the chosen duration.</div>' +

    '<button id="save">Save</button>' +

    '<script>' +
    'function gi(id){return document.getElementById(id);}' +
    'gi("save").addEventListener("click",function(){' +
      'var out={' +
        'ApiKey:gi("apiKey").value.trim(),' +
        'Section:gi("section").value,' +
        'BacklightMode:gi("backlight").value' +
      '};' +
      'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(out));' +
    '});' +
    '</script></body></html>';

  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

module.exports = {
  buildConfigPageUrl: buildConfigPageUrl
};
