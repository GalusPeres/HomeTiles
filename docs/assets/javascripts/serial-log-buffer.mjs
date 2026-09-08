// Retain a bounded tail, including partial lines, without growing a session archive.
export const LOG_LIMITS = Object.freeze({ characters: 128 * 1024, lines: 2000, line: 4096 });

export class SerialLogBuffer {
  constructor() {
    this.clear();
    this.resetStream();
  }

  clear() {
    this.lines = [];
    this.head = 0;
    this.characters = 0;
    this.partial = "";
    this.truncated = false;
  }

  resetStream() {
    this.escape = "";
    this.escapeLength = 0;
    this.previousCR = false;
  }

  append(text) {
    // ESP-IDF color sequences and CRLF can straddle USB reads. Never interpret
    // terminal commands as HTML, and cap malformed escape sequences as well.
    const clean = [];
    for (const character of text) {
      if (this.escape) {
        if (this.escape === "start" && character === "[") {
          this.escape = "csi";
        } else if (this.escape === "start" || /[@-~]/.test(character) || ++this.escapeLength >= 64) {
          this.escape = "";
        }
        continue;
      }
      if (character === "\x1b") {
        this.escape = "start";
        this.escapeLength = 0;
        continue;
      }
      if (character === "\r") clean.push("\n");
      else if (character === "\n") {
        if (!this.previousCR) clean.push("\n");
      } else if (character === "\t" || character >= " " && character !== "\x7f") clean.push(character);
      this.previousCR = character === "\r";
    }

    const parts = clean.join("").split("\n");
    for (let index = 0; index < parts.length; index += 1) {
      this.partial += parts[index];
      if (this.partial.length > LOG_LIMITS.line) {
        this.partial = this.partial.slice(-LOG_LIMITS.line);
        // Do not leave half of a UTF-16 surrogate pair at the trimmed edge.
        const first = this.partial.charCodeAt(0);
        if (first >= 0xdc00 && first <= 0xdfff) this.partial = this.partial.slice(1);
        this.truncated = true;
      }
      if (index < parts.length - 1) {
        const line = `${this.partial}\n`;
        this.lines.push(line);
        this.characters += line.length;
        this.partial = "";
      }
      while (this.lines.length - this.head + (this.partial ? 1 : 0) > LOG_LIMITS.lines ||
             this.characters + this.partial.length > LOG_LIMITS.characters) {
        this.characters -= this.lines[this.head].length;
        this.lines[this.head++] = "";
        this.truncated = true;
      }
      if (this.head >= 1024) {
        this.lines = this.lines.slice(this.head);
        this.head = 0;
      }
    }
  }

  get length() {
    return this.characters + this.partial.length;
  }

  get text() {
    return this.lines.slice(this.head).join("") + this.partial;
  }
}
