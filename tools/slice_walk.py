#!/usr/bin/env python3
"""slice_walk.py - take a group of lambdas out of runWalk() (src/app/walk.cpp) into a class in src/game/walk/<header>.

The lambdas of runWalk() become the methods of a class; whatever they use from the rest of the function (variables, other lambdas) comes in through an
`Env` struct holding references and std::functions under the same names, so the bodies read exactly as before. In runWalk() each moved lambda is left as a
small forwarder (same name), so the code that stays does not change.

    python tools/slice_walk.py spec.json [--dry]

spec.json:
  { "class": "ItemScreen",                 # the class (the header defines <class>Env and <class>)
    "header": "walk_items.hpp",            # file in src/game/walk/
    "lambdas": ["buildFiltered", ...],     # lambdas to move, by name (any order: they are kept in the order they are in walk.cpp)
    "assigns": ["svc.enterRoom"],          # statements 'target = [&](...) {...};' to move too (target stays a std::function the rest of the code calls)
    "types":  ["HoleFill"],                # local structs of runWalk() to move to the header first (their text is copied as is)
    "env":    { "name": "type" },          # types that the tool cannot find out (it asks the compiler for the rest, see below)
    "defaults": { "sfx": "int id, int vol = 255" }   # a helper with default arguments: it is kept as a method with those parameters
  }

How the Env is found: the tool generates the class with an empty Env, compiles (-fsyntax-only) and reads the "'x' was not declared" errors; for every such name it looks
the declaration up in runWalk() (a variable: its type; a lambda: the signature the compiler prints for it), adds it to Env and repeats until the compile is clean or
nothing new can be found (then it prints what is left, for the spec's "env"). It edits walk.cpp in place: use git to take it back.

Caveat: the class is built after the last moved lambda, so a forwarder must not be CALLED by code that runs before that line (a printout at setup, say): move such a call
below the construction (the game then dies at start with exit code 3: a call through the empty pointer).
"""
import json, re, subprocess, sys, os

NL = chr(10)
CR = chr(13)
SRC = 'src/app/walk.cpp'
INC = ['-Isrc/engine', '-Isrc/loaders', '-Isrc/game', '-Ithird_party', '-ID:/libs/sdl2/x86_64-w64-mingw32/include']
CXX = ['g++', '-std=c++17', '-w', '-fsyntax-only'] + INC


def read(p):
    return open(p, encoding='utf-8', errors='replace', newline='').read().replace(CR + NL, NL)


def write(p, text):
    open(p, 'w', encoding='utf-8', newline='').write(text)


def statements(L, first, last):
    """top-level (indent 8) statements of runWalk between lines first..last: (start, end, header-line)"""
    out = []
    i = first
    while i < last:
        l = L[i]
        if l.startswith('        ') and not l.startswith('         ') and l.strip() and not l.strip().startswith('//') and l.strip() not in ('{', '}', '};'):
            j = i
            depth = 0
            # a statement ends at the first line at indent 8 that ends with ';' or '}' (lambda) once braces balance
            while True:
                depth += L[j].count('{') - L[j].count('}')
                if depth <= 0 and (L[j].rstrip().endswith(';') or L[j].rstrip().endswith('};') or L[j].rstrip().endswith('}')) and j >= i:
                    break
                j += 1
            out.append((i, j, l))
            i = j + 1
        else:
            i += 1
    return out


def lambda_header(l):
    l = re.sub(r'\s*//.*$', '', l)
    m = re.match(r'^        (?:(?:const )?auto (\w+)|([\w\.]+)) = \[(&|&[^\]]*)\]\s*(\(.*\))?\s*(->\s*[\w:<>\*& ,]+?)?\s*$', l)
    if not m:
        return None
    return (m.group(1) or m.group(2), m.group(4) or '()', (m.group(5) or '').strip())


def run_walk_range(L):
    s = next(i for i, l in enumerate(L) if l.startswith('int runWalk('))
    e = next(i for i in range(s, len(L)) if L[i] == '}')
    return s, e


def convert(name, params, ret, body_lines, mname):
    rtype = 'auto' if not ret else ret.lstrip('-> ').strip()
    hdr = '    %s %s%s' % (rtype, mname, params)
    out = []
    for x in body_lines:
        out.append(x[4:] if x.startswith('    ') else x)
    if out and out[-1].strip() == '};':
        out[-1] = '    }'
    fixed = []
    for x in out:
        t = x.strip()
        if len(out) == 1 and t.startswith('{') and t.endswith('};') and len(t) > 2:  # the whole body on one line
            x = '    ' + t[:-1]
        fixed.append(x)
    return NL.join([hdr] + fixed)


def main():
    spec = json.load(open(sys.argv[1]))
    dry = '--dry' in sys.argv
    L = read(SRC).split(NL)
    rs, re_ = run_walk_range(L)
    st = statements(L, rs + 1, re_)
    want = set(spec['lambdas'])
    assigns = set(spec.get('assigns', []))
    found = {}
    for (a, b, hline) in st:
        h = lambda_header(hline)
        if h and (h[0] in want or h[0] in assigns):
            found[h[0]] = (a, b, h)
    missing = [n for n in list(want) + list(assigns) if n not in found]
    if missing:
        raise SystemExit('not found as lambdas at the top level of runWalk: ' + ', '.join(missing))
    order = sorted(found.items(), key=lambda kv: kv[1][0])
    last_pos = order[-1][1][1]

    # types to move
    type_texts = []
    type_ranges = []
    for t in spec.get('types', []):
        s0 = next(i for i in range(rs, re_) if L[i].strip().startswith('struct ' + t) and L[i].startswith('        struct'))
        c = s0
        while L[c - 1].strip().startswith('//'):
            c -= 1
        e0 = next(i for i in range(s0, re_) if L[i].startswith('        };'))
        type_texts.append(NL.join(x[8:] if x.startswith('        ') else x for x in L[c:e0 + 1]))
        type_ranges.append((c, e0))

    methods = []
    methods_names = []
    assign_info = []
    for name, (a, b, (n2, params, ret)) in order:
        comments = []
        c = a
        while L[c - 1].strip().startswith('//'):
            c -= 1
        comments = ['    ' + x.strip() for x in L[c:a]]
        mname = name
        if name in assigns:
            mname = name.replace('.', '_') + 'Run'
            assign_info.append((name, mname, params))
        else:
            methods_names.append(name)
        body = L[a + 1:b + 1]
        methods.append(NL.join(comments + [convert(name, params, ret, body, mname)]))

    cls = spec['class']
    env_types = dict(spec.get('env', {}))
    defaults = spec.get('defaults', {})
    cache_file = 'build/slice_env_%s.json' % cls
    env = {}
    if os.path.exists(cache_file):
        env = json.load(open(cache_file))
    env.update(env_types)

    def gen_header():
        h = []
        h.append('// %s - sliced out of runWalk() by tools/slice_walk.py: the lambdas of walk.cpp listed in the tool\'s spec are the methods of %s.' % (spec['header'], cls))
        h.append('// What they need from the rest of the walk session comes in through %sEnv, under the names they had, so the bodies read as before.' % cls)
        h.append('#pragma once')
        h.append('#include <functional>')
        h.append('#include <map>')
        h.append('#include <memory>')
        h.append('#include <vector>')
        h.append('')
        for t in type_texts:
            h.append(t)
            h.append('')
        h.append('struct %sEnv' % cls)
        h.append('{')
        for n, t in sorted(env.items()):
            t = t.replace('std::__cxx11::basic_string<char>', 'std::string')
            if t.startswith('fn:'):
                h.append('    std::function<%s> %s;' % (t[3:], n + ('Fn' if n in defaults else '')))
            elif t.startswith('?'):
                raise SystemExit("type of %s is unknown (%s): put it in the spec env" % (n, t))
            else:
                if not t.endswith('&') and not t.endswith('*'):
                    t += ' &'   # a variable of runWalk(): the Env refers to it
                h.append('    %s%s;' % (t if t.endswith('&') or t.endswith('*') else t + ' ', n))
        h.append('};')
        h.append('')
        h.append('class %s : public %sEnv' % (cls, cls))
        h.append('{')
        h.append('public:')
        h.append('    explicit %s(%sEnv env) : %sEnv(std::move(env)) {}' % (cls, cls, cls))
        for n, params in defaults.items():
            args = ', '.join(re.sub(r'\s*=.*', '', p).split()[-1].lstrip('&*') for p in params.split(','))
            h.append('    void %s(%s) { %sFn(%s); }' % (n, params, n, args))
        h.append('')
        h.append((NL + NL).join(methods))
        h.append('};')
        return NL.join(h) + NL

    # ---- new walk.cpp text
    def gen_walk():
        new = list(L)
        remove = []
        ctl = '%sCtl' % (cls[0].lower() + cls[1:])
        # forwarders at each original place; the controller is built after the last one
        repl = {}
        for name, (a, b, (n2, params, ret)) in order:
            c = a
            while L[c - 1].strip().startswith('//'):
                c -= 1
            if name in assigns:
                tgt, mname, prm = [x for x in assign_info if x[0] == name][0]
                names = ', '.join(re.sub(r'\s*=.*', '', p).split()[-1].lstrip('&*') for p in prm.strip('()').split(',') if p.strip())
                ret_kw = 'return ' if False else ''
                repl[(a, b)] = (c, ['        %s = [&]%s { return (*%s).%s(%s); };' % (tgt, prm, ctl, mname, names)])
            else:
                repl[(a, b)] = (c, ['        auto %s = [&](auto &&...a) -> decltype(auto) { return (*%s).%s(std::forward<decltype(a)>(a)...); };' % (name, ctl, name)])
        # build the new line list
        out = []
        i = 0
        spans = sorted([(v[0], k[1], v[1]) for k, v in repl.items()] + [(c, e0, None) for (c, e0) in type_ranges])
        pos = 0
        for (s0, e0, rep) in spans:
            out.extend(L[pos:s0])
            if rep:
                out.extend(rep)
            pos = e0 + 1
            if e0 == last_pos:
                pass
        out.extend(L[pos:])
        # declare the controller pointer before the first extracted lambda and build it after the last
        first_a = min(v[0] for v in repl.values())
        # find indices in `out` by searching the forwarder text
        first_text = [x for x in out if x.startswith('        auto %s = [&](auto &&' % order[0][0]) or x.startswith('        %s = [&]' % order[0][0])]
        idx_first = out.index(first_text[0])
        out.insert(idx_first, '        std::unique_ptr<%s> %s; // built below, once everything it takes is declared (%s)' % (cls, ctl, spec['header']))
        last_name = order[-1][0]
        last_text = [x for x in out if x.startswith('        auto %s = [&](auto &&' % last_name) or x.startswith('        %s = [&]' % last_name)]
        idx_last = out.index(last_text[0])
        init = ['        {',
                '            %sEnv env{%s};' % (cls, ', '.join(sorted(env.keys()))),
                '            %s = std::make_unique<%s>(std::move(env));' % (ctl, cls)]
        init.append('        }')
        out[idx_last + 1:idx_last + 1] = init
        text = NL.join(out)
        inc = '#include "../game/walk/%s"' % spec['header']
        if inc not in text:
            key = '#include "../game/walk/walk_room.hpp"'
            text = text.replace(key, key + NL + inc, 1) if key in text else text
        return text

    return spec, gen_header, gen_walk, env, cache_file, dry, defaults


if __name__ == '__main__' and not (len(sys.argv) > 2 and sys.argv[1] == '--probe'):
    spec, gen_header, gen_walk, env, cache_file, dry, defaults = main()
    hdr_path = 'src/game/walk/' + spec['header']
    write(hdr_path, gen_header())
    write(SRC, gen_walk())
    print('wrote', hdr_path, 'and edited', SRC)


# ---------------------------------------------------------------------------------------------------------------------------------
# python tools/slice_walk.py --probe spec.json name1 name2 ...   asks the compiler for the types of those names (as they are where the last moved lambda is)
def probe(spec_path, names):
    spec = json.load(open(spec_path))
    L = read(SRC).split(NL)
    rs, re_ = run_walk_range(L)
    st = statements(L, rs + 1, re_)
    wanted = set(spec['lambdas']) | set(spec.get('assigns', []))
    positions = [a for (a, b, h) in st if lambda_header(h) and lambda_header(h)[0] in wanted]
    at = max(positions)

    def build(lines_for):
        out = list(L[:at]) + lines_for + list(L[at:])
        out.insert(rs, 'template <class T> struct slice_show;')
        write('src/app/walk_probe_tmp.cpp', NL.join(out))
        r = subprocess.run(CXX + ['src/app/walk_probe_tmp.cpp'], capture_output=True, text=True)
        os.remove('src/app/walk_probe_tmp.cpp')
        return r.stderr

    lines = ['        { slice_show<decltype(%s)> probe_%s; }' % (n, n) for n in names]
    err = build(lines)
    result = {}
    lambdas = []
    for n in names:
        m = re.search(r"slice_show<(.*)> probe_%s\b" % re.escape(n), err)
        if not m:
            result[n] = '?'
            continue
        t = m.group(1).strip()
        if '<lambda(' in t:
            lambdas.append(n)
        else:
            result[n] = t
    if lambdas:
        lines = ['        { slice_show<decltype(&std::remove_reference_t<decltype(%s)>::operator())> probe_%s; }' % (n, n) for n in lambdas]
        err = build(lines)
        for n in lambdas:
            m = re.search(r"slice_show<(.*)> probe_%s\b" % re.escape(n), err)
            if not m:
                result[n] = '?'
                continue
            t = m.group(1).strip()
            mm = re.match(r'^(.*?) \(.*::<lambda\((.*?)\)>::\*\)\((.*?)\) const$', t)
            if not mm:
                result[n] = '? ' + t
                continue
            result[n] = 'fn:%s(%s)' % (mm.group(1).strip(), mm.group(3))
    cache_file = 'build/slice_env_%s.json' % spec['class']
    env = json.load(open(cache_file)) if os.path.exists(cache_file) else {}
    env.update(result)
    json.dump(env, open(cache_file, 'w'), indent=1)
    for n, t in result.items():
        print('%-24s %s' % (n, t))


if len(sys.argv) > 2 and sys.argv[1] == '--probe':
    probe(sys.argv[2], sys.argv[3:])
    sys.exit(0)
