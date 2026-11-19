// The website and tools/make_config.py must write identical settings blocks. Run: node tests/config_vector.mjs
import { readFileSync } from "node:fs";
import { packConfig } from "../web/config.js";

const v = JSON.parse(readFileSync(new URL("./config_vector.json", import.meta.url), "utf8"));
const got = Buffer.from(packConfig(v.settings)).toString("hex");
if (got !== v.hex) {
  console.error(`config vector mismatch\n  want ${v.hex}\n  got  ${got}`);
  process.exit(1);
}
console.log("config vector ok");
