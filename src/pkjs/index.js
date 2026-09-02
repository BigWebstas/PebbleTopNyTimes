var configPage = require('./config-page');

var DEFAULT_SECTION = 'home';
var MAX_ARTICLES = 15;
var SETTINGS_KEY = 'nyt-settings';

// Fallback key so the app works before the settings page is opened.
// Override it any time in the Pebble app's settings for this watchapp.
var FALLBACK_API_KEY = 'hR22xKV2euZaAs8zpMbLSMU60A7umk9028l9IQODl4dUffOn';

function getSettings() {
  try {
    var raw = localStorage.getItem(SETTINGS_KEY);
    if (!raw) {
      // Migrate values saved by the old Clay config page.
      raw = localStorage.getItem('clay-settings');
    }
    return JSON.parse(raw) || {};
  } catch (e) {
    return {};
  }
}

function saveSettings(settings) {
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
  } catch (e) {
    // localStorage full or unavailable - nothing we can do.
  }
}

function truncate(str, n) {
  str = String(str || '');
  return str.length > n ? str.substring(0, n - 1) + '…' : str;
}

function sendError(text) {
  Pebble.sendAppMessage({ AppKeyError: text });
}

// Sent as its own standalone message so it can arrive even if the article
// fetch below fails. The config page stores the select value as a string;
// coerce explicitly so the watch always gets a real int32 AppMessage tuple.
function sendBacklightMode(next) {
  var settings = getSettings();
  var mode = parseInt(settings.BacklightMode, 10);
  if (isNaN(mode)) {
    mode = 0;
  }
  Pebble.sendAppMessage({ BacklightMode: mode }, next, next);
}

function sendArticleAt(results, i) {
  if (i >= results.length) {
    return;
  }
  var r = results[i];
  var msg = {
    AppKeyIndex: i,
    AppKeyTitle: truncate(r.title, 90),
    AppKeyAbstract: truncate(r.abstract, 300),
    AppKeySection: truncate(r.section || '', 20),
    AppKeyByline: truncate(r.byline || '', 50)
  };
  Pebble.sendAppMessage(msg, function () {
    sendArticleAt(results, i + 1);
  }, function () {
    setTimeout(function () { sendArticleAt(results, i); }, 1000);
  });
}

function sendArticles(results) {
  Pebble.sendAppMessage({ AppKeyCount: results.length }, function () {
    sendArticleAt(results, 0);
  }, function () {
    setTimeout(function () { sendArticles(results); }, 1000);
  });
}

function fetchTopStories() {
  var settings = getSettings();
  var apiKey = (settings.ApiKey || '').trim() || FALLBACK_API_KEY;
  var section = settings.Section || DEFAULT_SECTION;

  if (!apiKey) {
    sendError('Set API key in app settings');
    return;
  }

  var url = 'https://api.nytimes.com/svc/topstories/v2/' + section +
    '.json?api-key=' + encodeURIComponent(apiKey);

  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.timeout = 20000;
  xhr.onload = function () {
    if (xhr.status === 401 || xhr.status === 403) {
      sendError('API key rejected');
      return;
    }
    if (xhr.status !== 200) {
      sendError('HTTP ' + xhr.status);
      return;
    }
    var data;
    try {
      data = JSON.parse(xhr.responseText);
    } catch (e) {
      sendError('Bad response');
      return;
    }
    var results = (data.results || []).filter(function (r) {
      return r && r.title;
    }).slice(0, MAX_ARTICLES);
    sendArticles(results);
  };
  xhr.onerror = function () { sendError('Network error'); };
  xhr.ontimeout = function () { sendError('Request timed out'); };
  xhr.send();
}

Pebble.addEventListener('ready', function () {
  sendBacklightMode(fetchTopStories);
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload && typeof e.payload.AppKeyRequest !== 'undefined') {
    fetchTopStories();
  }
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(configPage.buildConfigPageUrl(getSettings()));
});

// Re-apply after the settings page is closed (e.g. API key, section, or
// backlight mode changed).
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) {
    return;  // user backed out without saving
  }

  var next;
  try {
    next = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    try {
      next = JSON.parse(e.response);
    } catch (err2) {
      return;
    }
  }

  var settings = getSettings();
  settings.ApiKey = next.ApiKey || '';
  settings.Section = next.Section || DEFAULT_SECTION;
  settings.BacklightMode = next.BacklightMode || '0';
  saveSettings(settings);

  setTimeout(function () {
    sendBacklightMode(fetchTopStories);
  }, 300);
});
