#!/usr/bin/env python3
"""Builds the modding catalog: modding/GigaPets-Modding.html (one self-contained page).

  python tools/gen_modding_docs.py

Hand-written guide pages are modding/guide/*.md (in file name order). The reference parts are read from the source so they cannot drift:
  - Lua API      : `bind("ns", "name", l_fn)` in src/mods/script_host.hpp, documented by the comment above `static int l_fn(`
  - Hooks        : hookNames() and the "Fires ..." comments in script_host.hpp
  - Systems      : the rows of systemDefs() in src/engine/systems.hpp
  - Record types : the dispatch in applyFile() and the comment above each applyX() in src/mods/mod_loader.hpp
Anything the source does not describe is listed on stderr as "undocumented" (fix it by writing the comment in the source).
"""
import html
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "modding", "GigaPets-Modding.html")
GUIDE = os.path.join(ROOT, "modding", "guide")


def read(p):
    with open(os.path.join(ROOT, p), encoding="utf-8", newline="") as f:
        return f.read().replace("\r\n", "\n")


def esc(s):
    return html.escape(s, quote=False)


# ---------------------------------------------------------------- markdown (the small subset the guide uses)
def inline(s):
    s = esc(s)
    s = re.sub(r"`([^`]+)`", lambda m: "<code>" + m.group(1) + "</code>", s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", s)
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', s)
    return s


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def md(text, base_id=""):
    out, lines, i = [], text.split("\n"), 0
    para = []

    def flush():
        if para:
            out.append("<p>" + inline(" ".join(para)) + "</p>")
            para.clear()

    while i < len(lines):
        ln = lines[i]
        if ln.startswith("```"):
            flush()
            lang = ln[3:].strip()
            i += 1
            code = []
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            out.append('<pre class="code"><code>' + esc("\n".join(code)) + "</code></pre>")
            i += 1
            continue
        m = re.match(r"(#{1,4})\s+(.*)", ln)
        if m:
            flush()
            lvl = len(m.group(1)) + 1  # the page title is h1 of the page itself: guide # -> h2
            out.append('<h%d id="%s">%s</h%d>' % (min(lvl, 5), base_id + slug(m.group(2)), inline(m.group(2)), min(lvl, 5)))
            i += 1
            continue
        if re.match(r"\s*[-*]\s+", ln) or re.match(r"\s*\d+\.\s+", ln):
            flush()
            ordered = bool(re.match(r"\s*\d+\.", ln))
            items = []
            while i < len(lines) and (re.match(r"\s*[-*]\s+", lines[i]) or re.match(r"\s*\d+\.\s+", lines[i]) or (lines[i].startswith("  ") and lines[i].strip() and items)):
                if re.match(r"\s*([-*]|\d+\.)\s+", lines[i]):
                    items.append(re.sub(r"^\s*([-*]|\d+\.)\s+", "", lines[i]))
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            tag = "ol" if ordered else "ul"
            out.append("<%s>%s</%s>" % (tag, "".join("<li>" + inline(x) + "</li>" for x in items), tag))
            continue
        if ln.startswith("|") and i + 1 < len(lines) and re.match(r"\|[\s:|-]+\|?\s*$", lines[i + 1]):
            flush()
            head = [c.strip() for c in ln.strip().strip("|").split("|")]
            i += 2
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            t = "<table><thead><tr>" + "".join("<th>" + inline(h) + "</th>" for h in head) + "</tr></thead><tbody>"
            for r in rows:
                t += "<tr>" + "".join("<td>" + inline(c) + "</td>" for c in r) + "</tr>"
            out.append(t + "</tbody></table>")
            continue
        if not ln.strip():
            flush()
        else:
            para.append(ln.strip())
        i += 1
    flush()
    return "\n".join(out)


# ---------------------------------------------------------------- source readers
def comment_above(lines, idx):
    """The contiguous // comment lines directly above lines[idx], without the //."""
    out = []
    j = idx - 1
    while j >= 0 and lines[j].strip().startswith("//"):
        out.append(re.sub(r"^\s*//\s?", "", lines[j]))
        j -= 1
    return list(reversed(out))


def unc(s):
    return s.encode().decode("unicode_escape") if "\\" in s else s


def lua_api():
    src = read("src/mods/script_host.hpp")
    lines = src.split("\n")
    where = {}
    for i, ln in enumerate(lines):
        m = re.search(r"static int (l_\w+)\(", ln)
        if m:
            where[m.group(1)] = i
    reg = []  # (namespace, name, fn)
    for m in re.finditer(r'bind\("(\w+)",\s*"(\w+)",\s*(l_\w+)\)', src):
        reg.append((m.group(1), m.group(2), m.group(3)))
    for m in re.finditer(r'bindGlobal\("(\w+)",\s*(l_\w+)\)', src):
        reg.append(("", m.group(1), m.group(2)))
    seen = {r[2] for r in reg}
    gfns = []
    for m in re.finditer(r'\{"(\w+)",\s*(l_g\w+)\}', src):
        if (m.group(1), m.group(2)) not in gfns:
            gfns.append((m.group(1), m.group(2)))
    for name, fn in gfns:
        reg.append(("g", name, fn))
    entries = {}
    for ns, name, fn in reg:
        if (ns, name) in {(e[0], e[1]) for e in entries.values()}:
            continue
        c = comment_above(lines, where[fn]) if fn in where else []
        entries[(ns, name)] = (ns, name, c, fn)
    return list(entries.values())


def hook_info():
    src = read("src/mods/script_host.hpp")
    lines = src.split("\n")
    names = {}
    m = re.search(r"static std::vector<std::string> hookNames\(char kind\)(.*?)\n        \}", src, re.S)
    body = m.group(1)
    for km in re.finditer(r"case '(\w)':\s*return \{(.*?)\};", body, re.S):
        names[km.group(1)] = re.findall(r'"(\w+)"', km.group(2))
    dm = re.search(r"default:\s*return \{(.*?)\};", body, re.S)
    names["A"] = re.findall(r'"(\w+)"', dm.group(1))
    docs = {}  # (kind, hook) -> comment lines
    kinds = {"Npc": "N", "Area": "A", "Item": "I", "Game": "G", "Scene": "S"}
    for i, ln in enumerate(lines):
        if re.search(r"\b[\w:<>]+\s+fire\w+\(", ln):
            c = comment_above(lines, i)
            if not c:
                continue
            for hm in re.finditer(r"\b(Game|Npc|Area|Item|Scene)\.hooks\([^)]*\)[.:](on\w+)", " ".join(c)):
                docs.setdefault((kinds[hm.group(1)], hm.group(2)), c)
            # "// onBeforeDig(ctx): ..." lines (the seams): each starts a hook's description, which runs until the next such line
            cur = None
            for ln in c:
                sm = re.match(r"(on\w+)\(", ln)
                if sm:
                    cur = sm.group(1)
                    if ("G", cur) in docs:
                        cur = None
                    else:
                        docs[("G", cur)] = [ln]
                elif cur:
                    docs[("G", cur)].append(ln)
    return names, docs


def extras():
    """modding/extra/*.md: descriptions for what the source has no comment for. `### key` starts one (api:ns.name, hook:K:onName, rec:type)."""
    out, d = {}, os.path.join(ROOT, "modding", "extra")
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            key = None
            for ln in open(os.path.join(d, f), encoding="utf-8", newline="").read().replace("\r\n", "\n").split("\n"):
                m = re.match(r"###\s+(\S+)\s*$", ln)
                if m:
                    key = m.group(1)
                    out[key] = []
                elif key is not None:
                    out[key].append(ln)
    return out


def systems():
    src = read("src/engine/systems.hpp")
    rows = []
    lit = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'
    for m in re.finditer(r'\{\s*"([a-z_]+\.[a-z_]+)"\s*,\s*' + lit + r',\s*' + lit + r'\}', src):
        def join(s):
            return "".join(unc(x) for x in re.findall(r'"((?:[^"\\]|\\.)*)"', s))
        rows.append((m.group(1), join(m.group(2)), join(m.group(3))))
    return rows


def records():
    src = read("src/mods/mod_loader.hpp")
    lines = src.split("\n")
    disp = {}
    for m in re.finditer(r'type == "(\w+)"\)\s*\n\s*apply(\w+)\(', src):
        disp.setdefault(m.group(1), m.group(2))
    out = []
    for typ, fn in disp.items():
        idx = next((i for i, ln in enumerate(lines) if re.search(r"inline void apply%s\(" % fn, ln)), None)
        if idx is None:
            continue
        c = comment_above(lines, idx)
        # a type whose other modes are written by a second function (its comment is the rest of the description)
        for more in {"object": ["applyObjectEdit"], "area": ["applyAreaPatch"], "objtype": ["applyObjTypeShadowPatch"], "gate": ["findObjectById"]}.get(typ, []):
            j = next((i for i, ln in enumerate(lines) if re.search(r"inline \S+ %s\(" % more, ln)), None)
            if j is not None:
                extra = comment_above(lines, j)
                if extra and extra != c:
                    c = c + ([""] if c else []) + extra
        allowed = []
        window = "\n".join(lines[idx: idx + 60])
        am = re.search(r"allowed\s*\{(.*?)\};", window, re.S)
        if am:
            allowed = re.findall(r'"(\w+)"', am.group(1))
        out.append((typ, c, allowed))
    return out


# ---------------------------------------------------------------- page
def entry(eid, title, sig, body_html, kind=""):
    return ('<section class="entry" id="%s" data-kind="%s"><h4>%s</h4>%s%s</section>' %
            (eid, kind, title, ('<pre class="sig"><code>%s</code></pre>' % esc(sig)) if sig else "", body_html))


def comment_html(c, drop_first=False):
    c = c[1:] if drop_first and c else c
    # paragraphs separated by blank lines; indented lines keep their shape as code
    blocks, cur, code = [], [], []
    for ln in c + [""]:
        if ln.startswith("  ") and ln.strip():
            if cur:
                blocks.append("<p>" + inline(" ".join(cur)) + "</p>")
                cur = []
            code.append(ln)
        else:
            if code:
                blocks.append('<pre class="code"><code>' + esc("\n".join(code)) + "</code></pre>")
                code = []
            if ln.strip():
                cur.append(ln.strip())
            elif cur:
                blocks.append("<p>" + inline(" ".join(cur)) + "</p>")
                cur = []
    return "".join(blocks)


def build():
    EX = extras()
    nav, body = [], []
    missing = []

    def group(title, gid):
        nav.append('<div class="nav-group" data-g="%s"><div class="nav-title">%s</div>' % (gid, esc(title)))

    # ---- guide
    guide_files = sorted(f for f in os.listdir(GUIDE) if f.endswith(".md"))
    group("Guide", "guide")
    for f in guide_files:
        text = open(os.path.join(GUIDE, f), encoding="utf-8", newline="").read().replace("\r\n", "\n")
        first = re.match(r"#\s+(.*)", text)
        title = first.group(1) if first else f
        pid = "g-" + slug(title)
        nav.append('<a href="#%s" class="nav-item">%s</a>' % (pid, esc(title)))
        text = re.sub(r"^#\s+.*\n", "", text, count=1)
        if "{{FE_STRINGS}}" in text:
            fe = read("src/game/frontend/fe_strings.hpp")
            rows = re.findall(r'\{"(\w+)",\s*"((?:[^"\\]|\\.)*)"\}', fe)
            text = text.replace("{{FE_STRINGS}}", "| Key (`base:str/fe/...`) | Default |\n|---|---|\n" + "\n".join("| `%s` | %s |" % (k, v) for k, v in rows))
        body.append('<article class="page" id="%s"><h2>%s</h2>%s</article>' % (pid, esc(title), md(text, pid + "-")))
    nav.append("</div>")

    # ---- Lua API
    api = lua_api()
    spaces = {}
    for ns, name, c, fn in api:
        spaces.setdefault(ns, []).append((name, c, fn))
    order = ["", "Game", "Npc", "Area", "Item", "Scene", "System", "player", "npc", "quest", "objective", "counter", "object", "area", "shop", "world", "camera", "save", "profile", "options", "engine", "sound", "rand", "text", "tutorial", "scene", "g"]
    for ns in sorted(spaces, key=lambda n: (order.index(n) if n in order else 99, n)):
        pass
    group("Lua API", "api")
    page = ['<article class="page" id="api"><h2>Lua API</h2><p>Every function a script can call. <code>g</code> is the drawing object handed to <code>onDraw</code>, <code>onHud</code> and <code>onPostDraw</code>.</p>']
    for ns in sorted(spaces, key=lambda n: (order.index(n) if n in order else 99, n)):
        label = ns if ns else "globals"
        nav.append('<a href="#api-%s" class="nav-item">%s</a>' % (slug(label), esc(label)))
        page.append('<h3 id="api-%s">%s</h3>' % (slug(label), esc(label)))
        for name, c, fn in sorted(spaces[ns], key=lambda x: x[0]):
            full = (ns + "." if ns and ns != "g" else ("g:" if ns == "g" else "")) + name
            sig = ""
            desc = c
            if c and re.match(r"[\w.:]*%s\s*\(" % re.escape(name), c[0]):
                sig, desc = c[0], c[1:]
            if not c and ("api:" + full) in EX:
                ex = [x for x in EX["api:" + full]]
                while ex and not ex[0].strip():
                    ex.pop(0)
                if ex and re.match(r"[\w.:]*%s\s*\(" % re.escape(name), ex[0]):
                    sig, desc = ex[0], ex[1:]
                else:
                    desc = ex
            elif not c:
                missing.append("Lua API: " + full + " (" + fn + ")")
                desc = ["(no description yet)"]
            page.append(entry("api-%s-%s" % (slug(label), slug(name)), esc(full), sig, comment_html(desc), "api"))
    page.append("</article>")
    body.append("".join(page))
    nav.append("</div>")

    # ---- hooks
    names, docs = hook_info()
    group("Hooks", "hooks")
    kinds = [("G", "Game.hooks()", "The game-wide table: <code>local G = Game.hooks()</code> then <code>function G:onLoad(ctx) ... end</code>."),
             ("N", "Npc.hooks(id)", "One NPC: <code>Npc.hooks(\"mymod:npc/bob\")</code>."),
             ("A", "Area.hooks(id)", "One area: <code>Area.hooks(\"base:area/3\")</code>."),
             ("I", "Item.hooks(id)", "One item: <code>Item.hooks(\"mymod:item/cake\")</code>."),
             ("S", "Scene.hooks(id)", "A scene a mod opens (a full-screen script): <code>Scene.hooks(\"mymod:scene/shop\")</code>."),
             ("Y", "System.hooks(name)", "An engine system: <code>before</code>, <code>replace</code>, <code>after</code> (see Systems).")]
    page = ['<article class="page" id="hooks"><h2>Hooks</h2><p>A hook is a function a mod puts on a table; the game calls it when something happens. All of them get one argument, <code>ctx</code>, with what is known about the moment. Hooks run as coroutines, so <code>say</code>, <code>ask</code> and <code>wait</code> work inside them.</p>']
    for k, title, intro in kinds:
        if k == "Y":
            continue
        nav.append('<a href="#hooks-%s" class="nav-item">%s</a>' % (k, esc(title)))
        page.append('<h3 id="hooks-%s">%s</h3><p>%s</p>' % (k, esc(title), intro))
        for h in names.get(k, []):
            c = docs.get((k, h)) or EX.get("hook:%s:%s" % (k, h))
            if c:
                text = list(c)
                sig = ""
                page.append(entry("hook-%s-%s" % (k, h), esc(h), sig, comment_html(text), "hook"))
            else:
                missing.append("Hook: %s %s" % (title, h))
                page.append(entry("hook-%s-%s" % (k, h), esc(h), "", "<p>(no description yet)</p>", "hook"))
    page.append("</article>")
    body.append("".join(page))
    nav.append("</div>")

    # ---- systems
    sysrows = systems()
    group("Systems", "systems")
    page = ['<article class="page" id="systems"><h2>Systems</h2><p>%d parts of the engine a mod can wrap or replace with <code>System.hooks(name)</code>. '
            'See the guide page on systems for <code>before</code> / <code>replace</code> / <code>after</code>. The fields are what <code>ctx</code> holds; change them in <code>before</code> and the engine reads them back.</p>' % len(sysrows)]
    areas = {}
    for n, d, f in sysrows:
        areas.setdefault(n.split(".")[0], []).append((n, d, f))
    for a in sorted(areas):
        nav.append('<a href="#sys-%s" class="nav-item">%s.*</a>' % (a, esc(a)))
        page.append('<h3 id="sys-%s">%s</h3>' % (a, esc(a)))
        for n, d, f in areas[a]:
            fields = "<p><strong>ctx:</strong> " + inline(f) + "</p>" if f.strip() else "<p><strong>ctx:</strong> (nothing)</p>"
            page.append(entry("sys-" + slug(n), esc(n), "", "<p>" + inline(d) + "</p>" + fields, "system"))
    page.append("</article>")
    body.append("".join(page))
    nav.append("</div>")

    # ---- records
    recs = records()
    group("Data records", "records")
    page = ['<article class="page" id="records"><h2>Data records</h2><p>Records are JSON objects in <code>mods/&lt;id&gt;/data/*.json</code> (each file is a list). Each has a <code>type</code>, usually a <code>mode</code> and an <code>id</code>. '
            'The ids a mod makes start with its own prefix (<code>mymod:</code>).</p>']
    for typ, c, allowed in sorted(recs):
        nav.append('<a href="#rec-%s" class="nav-item">%s</a>' % (typ, esc(typ)))
        if not c and ("rec:" + typ) in EX:
            c = EX["rec:" + typ]
        if not c:
            missing.append("Record: " + typ)
        fields = ("<p><strong>Fields:</strong> " + ", ".join("<code>%s</code>" % esc(a) for a in allowed) + "</p>") if allowed else ""
        page.append(entry("rec-" + typ, esc(typ), "", (comment_html(c) if c else "<p>(no description yet)</p>") + fields, "record"))
    page.append("</article>")
    body.append("".join(page))
    nav.append("</div>")

    return "\n".join(nav), "\n".join(body), missing


CSS = """
:root{--bg:#fafaf7;--fg:#1d1f24;--mut:#5d6370;--line:#dcdcd4;--side:#f0efe8;--acc:#2a6f4e;--code:#eceae1;--sig:#e4efe9}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#17191d;--fg:#e4e6ea;--mut:#9aa1ad;--line:#2b2f37;--side:#1e2126;--acc:#6fcf9c;--code:#23272e;--sig:#1f2e27}}
:root[data-theme=dark]{--bg:#17191d;--fg:#e4e6ea;--mut:#9aa1ad;--line:#2b2f37;--side:#1e2126;--acc:#6fcf9c;--code:#23272e;--sig:#1f2e27}
*{box-sizing:border-box}html{scroll-behavior:smooth}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.55 system-ui,Segoe UI,sans-serif}
.side{position:fixed;top:0;bottom:0;left:0;width:272px;background:var(--side);border-right:1px solid var(--line);overflow:auto;padding:16px}
.main{margin-left:272px;padding:24px 40px 80px;max-width:60rem}
.brand{font-weight:700;font-size:1.05rem;margin:0 0 10px}
#q{width:100%;padding:8px 10px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);font:inherit;margin-bottom:12px}
.nav-title{font-size:.72rem;letter-spacing:.08em;text-transform:uppercase;color:var(--mut);margin:14px 0 4px}
.nav-item{display:block;padding:2px 8px;border-radius:4px;color:var(--fg);text-decoration:none;font-size:.92rem}
.nav-item:hover,.nav-item.on{background:var(--sig);color:var(--acc)}
h2{font-size:1.7rem;margin:2.2rem 0 .6rem;padding-top:.4rem;border-top:1px solid var(--line)}
.page:first-of-type h2{border-top:0;margin-top:0}
h3{font-size:1.2rem;margin:1.8rem 0 .4rem}h4{font:600 1rem ui-monospace,Consolas,monospace;margin:0 0 .3rem;color:var(--acc)}
p,li{max-width:46rem}a{color:var(--acc)}
code{font:.9em ui-monospace,Consolas,monospace;background:var(--code);padding:.08em .35em;border-radius:4px}
pre{background:var(--code);padding:10px 12px;border-radius:6px;overflow:auto;margin:.5rem 0}pre code{background:none;padding:0;font-size:.86rem;line-height:1.45}
pre.sig{background:var(--sig);border-left:3px solid var(--acc)}
.entry{border:1px solid var(--line);border-radius:8px;padding:12px 14px;margin:.7rem 0}
.entry p{margin:.35rem 0}
table{border-collapse:collapse;margin:.6rem 0;font-size:.92rem}th,td{border:1px solid var(--line);padding:4px 10px;text-align:left;vertical-align:top}th{background:var(--side)}
.hit{outline:2px solid var(--acc)}.hide{display:none}
#count{font-size:.8rem;color:var(--mut);margin:-6px 0 8px;min-height:1em}
@media(max-width:800px){.side{position:static;width:auto;border:0}.main{margin:0;padding:16px}}
"""

JS = """
(function(){
var q=document.getElementById('q'),count=document.getElementById('count');
var entries=[].slice.call(document.querySelectorAll('.entry'));
var texts=entries.map(function(e){return e.textContent.toLowerCase()});
q.addEventListener('input',function(){
  var v=q.value.trim().toLowerCase(),n=0;
  entries.forEach(function(e,i){var ok=!v||texts[i].indexOf(v)>=0;e.classList.toggle('hide',!ok);if(ok&&v)n++});
  count.textContent=v?(n+' match'+(n==1?'':'es')):'';
});
var links=[].slice.call(document.querySelectorAll('.nav-item'));
function mark(){var h=location.hash;links.forEach(function(a){a.classList.toggle('on',a.getAttribute('href')===h)})}
window.addEventListener('hashchange',mark);mark();
document.addEventListener('keydown',function(e){if(e.key==='/'&&document.activeElement!==q){e.preventDefault();q.focus()}});
})();
"""


def main():
    nav, body, missing = build()
    page = ('<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>GigaPets Modding Catalog</title><style>%s</style></head><body>'
            '<nav class="side"><p class="brand">GigaPets modding</p><input id="q" type="search" placeholder="Search the reference ( / )"><div id="count"></div>%s</nav>'
            '<main class="main">%s</main><script>%s</script></body></html>') % (CSS, nav, body, JS)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    print("wrote", os.path.relpath(OUT, ROOT), "(%d KB)" % (len(page) // 1024))
    if missing:
        print("undocumented (%d):" % len(missing), file=sys.stderr)
        for m in missing:
            print("  " + m, file=sys.stderr)


if __name__ == "__main__":
    main()
