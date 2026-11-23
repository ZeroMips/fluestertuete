// Writes the German texts of web/home.js into web/index.html, so search engines and link previews (Mastodon, Reddit,
// messengers) see the page without running JavaScript. home.js stays the only source of the texts; it switches
// languages in the browser exactly as before. Run by CI before publishing:  node tools/prerender.mjs [in.html] [out.html]
// Fails if index.html names a text that home.js does not have.
import { readFileSync, writeFileSync } from "node:fs";

const web = new URL("../web/", import.meta.url);
const src = process.argv[2] || new URL("index.html", web);
const dst = process.argv[3] || src;

// TEXTS is a plain object literal at the top of home.js; the rest of the file needs a browser.
const js = readFileSync(new URL("home.js", web), "utf8");
const start = js.indexOf("const TEXTS = {");
const end = js.indexOf("\n};\n", start);
if (start < 0 || end < 0) throw new Error("home.js: TEXTS not found");
const TEXTS = new Function(js.slice(start, end + 3) + "\nreturn TEXTS;")();
const de = TEXTS.de;

const esc = (s) => s.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
const missing = new Set();
const text = (key) => {
  if (de[key] === undefined) missing.add(key);
  return de[key] ?? "";
};

let html = readFileSync(src, "utf8");
// <tag ... data-t="key" ...></tag>: the text may contain simple markup (it goes in as HTML in the browser too).
html = html.replace(/(<([a-z0-9]+)\b[^>]*\sdata-t="([a-z0-9_]+)"[^>]*>)(<\/\2>)/g,
  (_, open, _tag, key, close) => open + text(key) + close);
// Image descriptions
html = html.replace(/(<img\b[^>]*\sdata-alt="([a-z0-9_]+)")([^>]*>)/g,
  (m, head, key, rest) => (/\salt="/.test(m) ? head.replace(/\salt="[^"]*"/, "") : head) + ` alt="${esc(text(key))}"` + rest);
html = html.replace(/<title>[^<]*<\/title>/, `<title>${esc(text("page_title"))}</title>`);

const left = (html.match(/data-t="[a-z0-9_]+"[^>]*><\//g) || []).length;
if (missing.size || left) {
  console.error(`prerender: missing in home.js: ${[...missing].join(", ") || "-"}; still empty: ${left}`);
  process.exit(1);
}
writeFileSync(dst, html);
console.log(`prerender: ${String(dst).replace(/^file:\/\//, "")}`);
