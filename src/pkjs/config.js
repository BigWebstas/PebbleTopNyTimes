module.exports = [
  {
    "type": "heading",
    "defaultValue": "Top Stories"
  },
  {
    "type": "text",
    "defaultValue": "Shows the New York Times most prominent stories. Needs a free API key from developer.nytimes.com with the Top Stories API enabled."
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "NYTimes API"
      },
      {
        "type": "input",
        "messageKey": "ApiKey",
        "label": "API Key",
        "defaultValue": "",
        "attributes": {
          "placeholder": "paste key here",
          "autocorrect": "off",
          "autocapitalize": "off"
        }
      },
      {
        "type": "select",
        "messageKey": "Section",
        "label": "Section",
        "defaultValue": "home",
        "options": [
          { "label": "Top Stories", "value": "home" },
          { "label": "World", "value": "world" },
          { "label": "U.S.", "value": "us" },
          { "label": "Politics", "value": "politics" },
          { "label": "Business", "value": "business" },
          { "label": "Technology", "value": "technology" },
          { "label": "Science", "value": "science" },
          { "label": "Health", "value": "health" },
          { "label": "Sports", "value": "sports" },
          { "label": "Arts", "value": "arts" },
          { "label": "Books", "value": "books" },
          { "label": "Movies", "value": "movies" },
          { "label": "Food", "value": "food" },
          { "label": "Travel", "value": "travel" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Display"
      },
      {
        "type": "select",
        "messageKey": "BacklightMode",
        "label": "Backlight",
        "defaultValue": "0",
        "options": [
          { "label": "System default", "value": "0" },
          { "label": "5 seconds after a button press", "value": "5" },
          { "label": "15 seconds after a button press", "value": "15" },
          { "label": "30 seconds after a button press", "value": "30" },
          { "label": "60 seconds after a button press", "value": "60" },
          { "label": "Always on (uses much more battery)", "value": "-1" }
        ]
      },
      {
        "type": "text",
        "defaultValue": "\"System default\" never touches the backlight - it behaves exactly as your watch's own Settings say. Any other option overrides that while this app is open, relighting the screen on Select or when you scroll the list, and keeping it lit for the chosen duration."
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
