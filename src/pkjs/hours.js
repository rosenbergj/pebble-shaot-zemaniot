// An hour of the day as the settings page shows it, following the watch's own
// 12- or 24-hour setting.
function hourLabel(h, h24) {
  if (h24) return (h < 10 ? "0" : "") + h + ":00";
  if (h === 0) return "midnight";
  if (h === 12) return "noon";
  return (h % 12) + (h < 12 ? "am" : "pm");
}

module.exports = { hourLabel: hourLabel };
