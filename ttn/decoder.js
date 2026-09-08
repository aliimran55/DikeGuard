//  In die TTN-Console unter End device -> Payload formatters ->
//  Uplink -> "Custom Javascript formatter" einfuegen.
function decodeUplink(input) {
  var SAMPLE_LEN = 10;    // DATA_SAMPLE_LEN
  var MAX_SAMPLES = 5;    // DATA_MAX_SAMPLES
  var POS_MARKER = 0xFF;  // LORA_POS_MARKER
  var bytes = input.bytes;
  var warnings = [];

  if (bytes.length === 0) {
    return { data: {}, warnings: warnings, errors: ["Leere Payload"] };
  }
  if (bytes.length % SAMPLE_LEN !== 0) {
    return {
      data: {},
      warnings: warnings,
      errors: ["Payload-Laenge " + bytes.length + " ist kein Vielfaches von " + SAMPLE_LEN]
    };
  }

  var count = bytes.length / SAMPLE_LEN;
  if (count > MAX_SAMPLES) {
    warnings.push("Mehr als " + MAX_SAMPLES + " Datensaetze (" + count + ") im Paket");
  }

  var samples = [];
  var positions = [];
  for (var i = 0; i < count; i++) {
    var o = i * SAMPLE_LEN;

    if (bytes[o] === POS_MARKER) {
      var lat = bytes[o + 2] | (bytes[o + 3] << 8) | (bytes[o + 4] << 16) | (bytes[o + 5] << 24);
      var lon = bytes[o + 6] | (bytes[o + 7] << 8) | (bytes[o + 8] << 16) | (bytes[o + 9] << 24);

      positions.push({
        node:      bytes[o + 1],
        latitude:  lat / 1e6,
        longitude: lon / 1e6
      });
      continue;
    }

    var tempRaw = bytes[o + 1] | (bytes[o + 2] << 8);
    if (tempRaw & 0x8000) tempRaw -= 0x10000;

    samples.push({
      node:        bytes[o],
      temperature: tempRaw / 100,
      humidity:    (bytes[o + 3] | (bytes[o + 4] << 8)) / 100,
      battery:     (bytes[o + 5] | (bytes[o + 6] << 8)) / 100,
      moisture:    bytes[o + 7] | (bytes[o + 8] << 8),
      vibration:   bytes[o + 9] !== 0
    });
  }

  return {
    data: { count: count, samples: samples, positions: positions },
    warnings: warnings,
    errors: []
  };
}
