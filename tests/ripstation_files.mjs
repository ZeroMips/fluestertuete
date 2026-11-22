// Writes the station's card files for fixed settings into a folder, for tests/ripstation_check.py.
//   node tests/ripstation_files.mjs <dir> [<tarball>]
import { readFileSync, writeFileSync } from "node:fs";
import * as rc from "../web/ripstation-config.js";

const dir = process.argv[2];
const code = process.argv[3] ? readFileSync(process.argv[3]).toString("base64") : Buffer.from("x").toString("base64");
const s = JSON.parse(readFileSync(new URL("./ripstation_vector.json", import.meta.url), "utf8"));
const problems = rc.validate(s);
if (problems.length) { console.error("unexpected problems:", problems); process.exit(1); }
writeFileSync(`${dir}/user-data`, rc.userData(s, code));
writeFileSync(`${dir}/network-config`, rc.networkConfig(s));
writeFileSync(`${dir}/meta-data`, rc.metaData("ripstation-test"));

// validate() catches the usual mistakes
const bad = rc.validate({ ...s, nas_type: "nfs", nas_url: "//nas/share", ssh_key: "AAAA", wifi_password: "short" });
const want = ["nas_url:nfs_form", "wifi_password:short", "ssh_key:ssh_form"];
const got = bad.map((p) => `${p.field}:${p.problem}`);
if (JSON.stringify(got) !== JSON.stringify(want)) { console.error("validate:", got); process.exit(1); }
console.log("ripstation files written");
