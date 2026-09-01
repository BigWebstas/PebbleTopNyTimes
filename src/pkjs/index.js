var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var DEFAULT_SECTION = 'home';
var MAX_ARTICLES = 15;

// Fallback key so the app works before the settings page is opened.
// Override it any time in the Pebble app's settings for this watchapp.
var FALLBACK_API_KEY = 'hR22xKV2euZaAs8zpMbLSMU60A7umk9028l9IQODl4dUffOn';

function getSettings() {
  try {
    return JSON.parse(localStorage.getItem('clay-settings')) || {};
  } catch (e) {
    return {};
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
// fetch below fails. Clay stores select values as strings; coerce explicitly
// so the watch always gets a real int32 AppMessage tuple.
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

// Re-apply after the settings page is closed (e.g. API key, section, or
// backlight mode changed).
Pebble.addEventListener('webviewclosed', function (e) {
  if (e && e.response) {
    setTimeout(function () {
      sendBacklightMode(fetchTopStories);
    }, 300);
  }
});
