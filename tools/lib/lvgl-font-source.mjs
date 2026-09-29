// Reads glyph metrics from an lv_font_conv C font (format0/sparse tiny cmaps),
// so tests can compare glyph boxes without compiling the font.
export function lvglFontSource(source) {
  source = source.replace(/\r\n/g, '\n');
  const glyphs = [...source.matchAll(
    /\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}/g,
  )].map((m) => ({
    bitmapIndex: +m[1], advW: +m[2], boxW: +m[3], boxH: +m[4], ofsX: +m[5], ofsY: +m[6],
  }));
  const lists = new Map([...source.matchAll(/static const uint16_t (\w+)\[\] = \{([^}]*)\}/g)]
    .map((m) => [m[1], m[2].split(',').map((v) => v.trim()).filter(Boolean).map(Number)]));
  const cmaps = [...source.matchAll(
    /\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*\.unicode_list = (\w+), \.glyph_id_ofs_list = (\w+), \.list_length = (\d+), \.type = (\w+)/g,
  )].map((m) => ({
    start: +m[1], length: +m[2], glyphStart: +m[3],
    unicodeList: m[4] === 'NULL' ? null : lists.get(m[4]), type: m[7],
  }));
  const glyphId = (codepoint) => {
    for (const cmap of cmaps) {
      const offset = codepoint - cmap.start;
      if (offset < 0 || offset >= cmap.length) continue;
      if (cmap.type === 'LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY') return cmap.glyphStart + offset;
      if (cmap.type === 'LV_FONT_FMT_TXT_CMAP_SPARSE_TINY') {
        const index = cmap.unicodeList.indexOf(offset);
        if (index >= 0) return cmap.glyphStart + index;
      }
    }
    return 0;
  };
  return {
    lineHeight: Number(source.match(/\.line_height = (\d+),/)[1]),
    baseLine: Number(source.match(/\.base_line = (-?\d+),/)[1]),
    fallback: source.match(/\.fallback = &?(\w+),/)[1],
    glyph: (codepoint) => glyphs[glyphId(codepoint)] || null,
  };
}
