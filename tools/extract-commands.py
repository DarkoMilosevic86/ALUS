#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# Extracts the command table ALUS inherits into ALUS data files
# (architecture.md §5.4). The source is the upstream screen reader's Python
# source tree, read and never executed; the licence and the attribution are in
# vision.md §6.
#
#   data/scripts.csv           one row per script: name, description, category,
#                              gestures per layout, behaviour flags, v1.0 scope
#   data/gestures-builtin.ini  the built-in gesture layer, in NVDA
#                              `gestures.ini` format and with NVDA's own
#                              `module.Class` section names, so that a user's
#                              NVDA gestures.ini overlays it unchanged (H3)
#   data/key-labels.ini        key name -> spoken label, from NVDA keyLabels.py
#                              ("capsLock" -> "caps lock"); what input help and
#                              "speak command keys" say out loud
#   data/locale/<lang>/gestures.ini
#                              NVDA's own per-language key overrides, copied
#                              verbatim: the Serbian one remaps the laptop
#                              mouse and flow commands onto `š` and `đ` (H8),
#                              and the format is identical, so it is data, not
#                              code to be rewritten
#
# WHY AST AND NOT IMPORT. The plan said "import the NVDA modules". That is
# impossible off Windows: `globalCommands` imports `comtypes`, `winUser`,
# `winInputHook` and `ctypes.windll` transitively, none of which exist on Linux,
# and stubbing them out would mean trusting ~40 fake modules to behave like the
# real ones. This reads the source with `ast` instead: no NVDA code is executed,
# the extraction runs on any machine, and it is the same source of truth.
#
# What it understands, because NVDA writes bindings in exactly these four ways:
#   1. @script(description=…, category=…, gestures=(…)) on `def script_<name>`
#   2. a class-level `__gestures = {"kb:x": "scriptName"}` dict
#   3. the docstring of a `script_<name>` method (pre-decorator style)
#   4. browseMode's programmatic quick nav: `qn(...)` / `_addQuickNavHeading`
#
# Nothing here is hand-transcribed; every string comes out of NVDA's source.

from __future__ import annotations

import argparse
import ast
import csv
import datetime
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# What to read. Order matters only for reproducible output.
# ---------------------------------------------------------------------------

#: Modules scanned for scripts, as (path relative to NVDA/source, module name).
SCRIPT_MODULES: list[tuple[str, str]] = [
    ("globalCommands.py", "globalCommands"),
    ("browseMode.py", "browseMode"),
    ("cursorManager.py", "cursorManager"),
    ("documentBase.py", "documentBase"),
    ("editableText.py", "editableText"),
    ("virtualBuffers/__init__.py", "virtualBuffers"),
    ("treeInterceptorHandler.py", "treeInterceptorHandler"),
    ("review.py", "review"),
    ("brailleInput.py", "brailleInput"),
]

#: Modules read only for the constants the ones above refer to
#: (`inputCore.SCRCAT_BROWSEMODE`, `sayAll.CURSOR.CARET`, …).
CONSTANT_MODULES: list[tuple[str, str]] = [
    ("inputCore.py", "inputCore"),
    ("scriptHandler.py", "scriptHandler"),
    ("textInfos/__init__.py", "textInfos"),
]

#: Script categories outside ALUS v1.0 (vision.md §5, binding list). The
#: scripts stay in the CSV, marked `deferred`, and are left out of the built-in
#: gesture layer: a key bound to a script that does not exist would be dead
#: weight in the map and a lie in the gesture dialog.
DEFERRED_CATEGORIES = {
    "Magnifier",
    "Touch screen",
    "Vision",
    "Remote Access",
    # MathML interaction needs MathCAT or an equivalent — v1.1.
    "Math navigation",
}

#: Individual scripts outside v1.0 even though their CATEGORY is in scope. A
#: settings dialog for a subsystem ALUS does not have is still a command the
#: gesture dialog would offer and ALUS could not run, so it goes too.
DEFERRED_SCRIPTS = {
    # OCR / content recognition: portal screencast + tesseract pipeline, v1.1.
    "recognizeWithUwpOcr",
    "activateWindowsOCRSettings",
    "cycleOcrLanguage",
    "toggleAutomaticRefreshOfRecogResult",
    # Windows-only plumbing with no Linux meaning.
    "activateWindowsLiveCaptions",
    "toggleWindowsLiveCaptionsAutomaticTranscription",
    # Braille and speech viewers are GUI windows of their own (phase 9 at the
    # earliest).
    "toggleBrailleViewer",
    "toggleSpeechViewer",
    # Settings dialogs for v1.0 non-goals (vision.md §5).
    "activateMagnifierSettingsDialog",
    "activateVisionSettingsDialog",
    "activateRemoteAccessSettings",
    # Screen curtain: the Wayland equivalent is not clear (v1.2).
    "toggleScreenCurtain",
    "reportScreenCurtainState",
    # Touch screen (`ts:` gestures) is a v1.2 non-goal; these two exist only to
    # cycle the touch element type.
    "nextBrowseElement",
    "prevBrowseElement",
    # MathML interaction needs MathCAT or an equivalent (v1.1), so the quick
    # navigation for formulas has nothing to reach.
    "nextMath",
    "previousMath",
}

#: Gesture sources ALUS v1.0 does not have. `ts:` is a non-goal; `bk:`/`br:`
#: belong to phase 7 and are kept.
DEFERRED_GESTURE_SOURCES = ("ts:", "ts(")


# ---------------------------------------------------------------------------
# String resolution. NVDA writes every user-visible string through `_()`,
# `pgettext()` or `npgettext()`, often concatenated and sometimes `.format()`ed.
# ---------------------------------------------------------------------------


class Resolver:
    """Resolves AST expressions to strings using module-level constants."""

    def __init__(self) -> None:
        #: module name -> {constant name -> value}
        self.modules: dict[str, dict[str, str]] = {}
        #: the module currently being resolved
        self.current: str = ""
        #: class-level constants of the class currently being resolved
        self.class_env: dict[str, str] = {}
        #: module name -> {local name -> (source module, name)}
        self.imports: dict[str, dict[str, tuple[str, str]]] = {}

    def collect(self, module: str, tree: ast.Module) -> None:
        env = self.modules.setdefault(module, {})
        # `from inputCore import SCRCAT_BROWSEMODE` — without this, a category
        # referenced by its bare name in another module cannot be resolved.
        for node in tree.body:
            if isinstance(node, ast.ImportFrom) and node.module:
                for alias in node.names:
                    self.imports.setdefault(module, {})[alias.asname or alias.name] = (
                        node.module,
                        alias.name,
                    )
        for node in tree.body:
            if isinstance(node, (ast.Assign, ast.AnnAssign)):
                targets = node.targets if isinstance(node, ast.Assign) else [node.target]
                value = self.resolve(node.value, module=module) if node.value else None
                if value is None:
                    continue
                for target in targets:
                    if isinstance(target, ast.Name):
                        env[target.id] = value

    def resolve(self, node: ast.AST | None, module: str | None = None) -> str | None:
        """Best-effort string value of `node`, or None if it is not a string."""
        module = module or self.current
        if node is None:
            return None
        if isinstance(node, ast.Constant):
            return node.value if isinstance(node.value, str) else None
        if isinstance(node, ast.Name):
            if node.id in self.class_env:
                return self.class_env[node.id]
            local = self.modules.get(module, {}).get(node.id)
            if local is not None:
                return local
            imported = self.imports.get(module, {}).get(node.id)
            if imported is not None:
                source, name = imported
                return self.modules.get(source, {}).get(name)
            return None
        if isinstance(node, ast.Attribute):
            # inputCore.SCRCAT_BROWSEMODE, config.conf…
            if isinstance(node.value, ast.Name):
                return self.modules.get(node.value.id, {}).get(node.attr)
            return None
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            left = self.resolve(node.left, module)
            right = self.resolve(node.right, module)
            if left is not None and right is not None:
                return left + right
            return None
        if isinstance(node, ast.JoinedStr):
            parts: list[str] = []
            for value in node.values:
                if isinstance(value, ast.Constant) and isinstance(value.value, str):
                    parts.append(value.value)
                elif isinstance(value, ast.FormattedValue):
                    # Keep the placeholder; callers that know the value (quick
                    # nav heading levels) substitute it themselves.
                    name = ast.unparse(value.value)
                    parts.append("{%s}" % name)
                else:
                    return None
            return "".join(parts)
        if isinstance(node, ast.Call):
            return self._resolve_call(node, module)
        return None

    def _resolve_call(self, node: ast.Call, module: str) -> str | None:
        func = node.func
        name = None
        if isinstance(func, ast.Name):
            name = func.id
        elif isinstance(func, ast.Attribute):
            name = func.attr

        # _("text")  /  gettext("text")
        if name in ("_", "gettext"):
            return self.resolve(node.args[0], module) if node.args else None
        # pgettext("context", "text")  /  npgettext(...)
        if name in ("pgettext", "npgettext"):
            return self.resolve(node.args[1], module) if len(node.args) > 1 else None
        # "…{i}…".format(i=i)   — keep the template, substitution is the caller's
        if name == "format" and isinstance(func, ast.Attribute):
            return self.resolve(func.value, module)
        return None


# ---------------------------------------------------------------------------
# The extracted model
# ---------------------------------------------------------------------------


@dataclass
class Script:
    module: str
    cls: str
    name: str
    description: str = ""
    category: str = ""
    #: normalized-ish gesture identifiers exactly as NVDA writes them
    gestures: list[str] = field(default_factory=list)
    can_propagate: bool = False
    bypass_input_help: bool = False
    allow_in_sleep_mode: bool = False
    speak_on_demand: bool = False
    resume_say_all_mode: str = ""
    #: where this row came from: decorator / docstring / quicknav
    origin: str = "decorator"

    @property
    def location(self) -> str:
        return f"{self.module}.{self.cls}"

    @property
    def key(self) -> tuple[str, str, str]:
        return (self.module, self.cls, self.name)


def split_gestures(gestures: list[str]) -> tuple[list[str], list[str], list[str], list[str]]:
    """Split into (layout agnostic, desktop only, laptop only, touch).

    A gesture written as `kb:x` is active in BOTH layouts — §5.4. Only
    `kb(desktop):` / `kb(laptop):` are layout exclusive. Touch gestures get
    their own bucket because touch is a v1.0 non-goal and must not leak into a
    keyboard column.
    """
    agnostic, desktop, laptop, touch = [], [], [], []
    for gesture in gestures:
        lowered = gesture.lower()
        if lowered.startswith("ts"):
            touch.append(gesture)
        elif lowered.startswith("kb(desktop):"):
            desktop.append(gesture)
        elif lowered.startswith("kb(laptop):"):
            laptop.append(gesture)
        else:
            agnostic.append(gesture)
    return agnostic, desktop, laptop, touch


# ---------------------------------------------------------------------------
# Extraction
# ---------------------------------------------------------------------------


class Extractor:
    def __init__(self, source_root: Path) -> None:
        self.source_root = source_root
        self.resolver = Resolver()
        self.scripts: list[Script] = []
        #: location -> {gesture -> script name or None for an unbind}
        self.gesture_map: dict[str, dict[str, str | None]] = {}
        #: "module.Class" -> base classes, as "module.Class" where resolvable
        self.class_bases: dict[str, list[str]] = {}
        #: "module.Class" -> its own `scriptCategory`, if it sets one
        self.class_category: dict[str, str] = {}
        #: script name -> locations that define it
        self.definitions: dict[str, set[str]] = {}
        self.warnings: list[str] = []

    # -- pass 1: constants ------------------------------------------------

    def collect_constants(self) -> None:
        for relative, module in CONSTANT_MODULES + SCRIPT_MODULES:
            path = self.source_root / relative
            if not path.exists():
                self.warnings.append(f"missing module: {relative}")
                continue
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
            self.resolver.collect(module, tree)

    # -- pass 2: scripts --------------------------------------------------

    def extract(self) -> None:
        self.collect_constants()
        for relative, module in SCRIPT_MODULES:
            path = self.source_root / relative
            if not path.exists():
                continue
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
            self.resolver.current = module
            for node in ast.walk(tree):
                if isinstance(node, ast.ClassDef):
                    self._class(module, node)
            if module == "browseMode":
                self._quick_nav(module, tree)
        self._inherit_categories()
        self._merge_dict_gestures()

    def _inherit_categories(self) -> None:
        """A script without a category inherits the class attribute.

        `scriptCategory` is set on a base class (`BrowseModeTreeInterceptor`,
        `CursorManager`) and inherited, so a script read off a subclass has to
        walk the same chain or it would come out uncategorised and vanish from
        the gesture dialog.
        """
        for script in self.scripts:
            if script.category:
                continue
            for location in self._class_chain(script.location):
                category = self.class_category.get(location)
                if category:
                    script.category = category
                    break
            else:
                # NVDA's own fallback (inputCore.py:921-924): no script
                # category and no class `scriptCategory` means "Miscellaneous".
                script.category = self.resolver.modules.get("inputCore", {}).get(
                    "SCRCAT_MISC",
                    "Miscellaneous",
                )

    def _merge_dict_gestures(self) -> None:
        """Fold `__gestures` bindings into the script rows.

        The binding lives on the class whose dict holds it, which may be a
        subclass of the class that defines the script; the gesture map keeps
        that distinction, the CSV does not, because the CSV answers "which key
        runs this command".
        """
        by_key = {(s.location, s.name): s for s in self.scripts}
        for location, entries in self.gesture_map.items():
            for gesture, script_name in entries.items():
                if script_name is None:
                    continue
                defining = self._defining_location(location, script_name)
                if defining is None:
                    continue
                script = by_key.get((defining, script_name))
                if script is not None and gesture not in script.gestures:
                    script.gestures.append(gesture)

    def _class_chain(self, location: str) -> list[str]:
        """`location` followed by its base classes, breadth first, no repeats."""
        chain: list[str] = []
        queue = [location]
        while queue:
            current = queue.pop(0)
            if current in chain:
                continue
            chain.append(current)
            queue.extend(self.class_bases.get(current, []))
        return chain

    def _defining_location(self, location: str, script_name: str) -> str | None:
        """Where the script bound in `location` is actually defined, if anywhere.

        A `__gestures` dict in a subclass routinely binds a script defined on a
        base class. Checking only the subclass would silently drop the binding.
        """
        for candidate in self._class_chain(location):
            if candidate in self.definitions.get(script_name, set()):
                return candidate
        return None

    def _class(self, module: str, cls: ast.ClassDef) -> None:
        location = f"{module}.{cls.name}"
        bases: list[str] = []
        for base in cls.bases:
            text = ast.unparse(base)
            if not re.fullmatch(r"[A-Za-z_][\w.]*", text):
                continue
            bases.append(text if "." in text else f"{module}.{text}")
        self.class_bases[location] = bases
        class_env: dict[str, str] = {}
        for node in cls.body:
            if isinstance(node, ast.Assign):
                value = self.resolver.resolve(node.value, module)
                if value is None:
                    continue
                for target in node.targets:
                    if isinstance(target, ast.Name):
                        class_env[target.id] = value
        self.resolver.class_env = class_env
        default_category = class_env.get("scriptCategory", "")
        if default_category:
            self.class_category[location] = default_category

        # class-level __gestures = {"kb:x": "scriptName"}
        for node in cls.body:
            if not isinstance(node, ast.Assign):
                continue
            if not isinstance(node.value, ast.Dict):
                continue
            names = [t.id for t in node.targets if isinstance(t, ast.Name)]
            if not any(n.endswith("__gestures") or n == "_gestureMap" for n in names):
                continue
            for key_node, value_node in zip(node.value.keys, node.value.values):
                gesture = self.resolver.resolve(key_node, module)
                script_name = self.resolver.resolve(value_node, module)
                if gesture is None:
                    # `None: "kb:h"` style unbind keys are written the other way
                    # round in gestures.ini; NVDA never uses them in __gestures.
                    continue
                self._bind(f"{module}.{cls.name}", gesture, script_name)

        for node in cls.body:
            if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                continue
            if not node.name.startswith("script_"):
                continue
            script = self._script_from_function(module, cls.name, node, default_category)
            if script is not None:
                self.scripts.append(script)
                self.definitions.setdefault(script.name, set()).add(location)
        self.resolver.class_env = {}

    def _script_from_function(
        self,
        module: str,
        cls: str,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        default_category: str,
    ) -> Script | None:
        name = node.name[len("script_") :]
        script = Script(module=module, cls=cls, name=name, category=default_category)

        docstring = ast.get_docstring(node, clean=True)
        decorator = None
        for dec in node.decorator_list:
            if isinstance(dec, ast.Call):
                func = dec.func
                func_name = func.id if isinstance(func, ast.Name) else getattr(func, "attr", "")
                if func_name == "script":
                    decorator = dec
                    break

        if decorator is None:
            script.origin = "docstring"
            if docstring:
                script.description = " ".join(docstring.split())
            return script

        for keyword in decorator.keywords:
            arg = keyword.arg
            value = keyword.value
            if arg == "description":
                resolved = self.resolver.resolve(value, module)
                if resolved is None:
                    self.warnings.append(
                        f"{module}.{cls}.{name}: unresolved description "
                        f"({ast.unparse(value)[:60]})",
                    )
                else:
                    script.description = " ".join(resolved.split())
            elif arg == "category":
                resolved = self.resolver.resolve(value, module)
                if resolved is None:
                    self.warnings.append(
                        f"{module}.{cls}.{name}: unresolved category "
                        f"({ast.unparse(value)[:60]})",
                    )
                else:
                    script.category = resolved
            elif arg == "gesture":
                resolved = self.resolver.resolve(value, module)
                if resolved:
                    script.gestures.append(resolved)
            elif arg == "gestures":
                if isinstance(value, (ast.Tuple, ast.List, ast.Set)):
                    for element in value.elts:
                        resolved = self.resolver.resolve(element, module)
                        if resolved:
                            script.gestures.append(resolved)
                        else:
                            self.warnings.append(
                                f"{module}.{cls}.{name}: unresolved gesture "
                                f"({ast.unparse(element)[:60]})",
                            )
                else:
                    self.warnings.append(
                        f"{module}.{cls}.{name}: gestures= is not a literal "
                        f"({ast.unparse(value)[:60]})",
                    )
            elif arg == "canPropagate":
                script.can_propagate = _truthy(value)
            elif arg == "bypassInputHelp":
                script.bypass_input_help = _truthy(value)
            elif arg == "allowInSleepMode":
                script.allow_in_sleep_mode = _truthy(value)
            elif arg == "speakOnDemand":
                script.speak_on_demand = _truthy(value)
            elif arg == "resumeSayAllMode":
                script.resume_say_all_mode = _say_all_mode(value)

        if not script.description and docstring:
            script.description = " ".join(docstring.split())
        return script

    # -- browseMode quick nav --------------------------------------------

    def _quick_nav(self, module: str, tree: ast.Module) -> None:
        """browseMode registers quick nav scripts programmatically (§9.2).

        Each `qn(itemType, key=…, nextDoc=…, prevDoc=…)` call creates two
        scripts, `next<Item>` and `previous<Item>`, bound to `kb:<key>` and
        `kb:shift+<key>`. `_addQuickNavHeading(range(1, 10))` does the same for
        heading levels 1-9, with `{i}` substituted into the descriptions.
        """
        cls = "BrowseModeTreeInterceptor"
        location = f"{module}.{cls}"
        category = self.resolver.modules.get("inputCore", {}).get("SCRCAT_BROWSEMODE", "Browse mode")

        def add_pair(item_type: str, key: str | None, next_doc: str, prev_doc: str) -> None:
            suffix = item_type[0].upper() + item_type[1:]
            for direction, doc in (("next", next_doc), ("previous", prev_doc)):
                name = f"{direction}{suffix}"
                script = Script(
                    module=module,
                    cls=cls,
                    name=name,
                    description=" ".join(doc.split()) if doc else "",
                    category=category,
                    # Quick nav scripts carry resumeSayAllMode = CURSOR.CARET
                    # (addQuickNav sets it on the generated function).
                    resume_say_all_mode="caret",
                    origin="quicknav",
                )
                self.definitions.setdefault(name, set()).add(location)
                if key is not None:
                    gesture = f"kb:{key}" if direction == "next" else f"kb:shift+{key}"
                    script.gestures.append(gesture)
                    self._bind(location, gesture, name)
                self.scripts.append(script)

        for node in ast.walk(tree):
            if not isinstance(node, ast.Call):
                continue
            func = node.func
            func_name = func.id if isinstance(func, ast.Name) else getattr(func, "attr", "")

            if func_name == "qn" and node.args:
                item_type = self.resolver.resolve(node.args[0], module)
                if not item_type:
                    continue
                values = {kw.arg: kw.value for kw in node.keywords}
                key_node = values.get("key")
                key = self.resolver.resolve(key_node, module) if key_node is not None else None
                add_pair(
                    item_type,
                    key,
                    self.resolver.resolve(values.get("nextDoc"), module) or "",
                    self.resolver.resolve(values.get("prevDoc"), module) or "",
                )
            elif func_name == "_addQuickNavHeading":
                levels = _range_args(node.args[0] if node.args else None)
                templates = self._heading_templates(tree, module)
                for level in levels:
                    add_pair(
                        f"heading{level}",
                        str(level),
                        templates[0].replace("{i}", str(level)),
                        templates[1].replace("{i}", str(level)),
                    )

    def _heading_templates(self, tree: ast.Module, module: str) -> tuple[str, str]:
        """The nextDoc/prevDoc templates inside `_addQuickNavHeading`."""
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef) and node.name == "_addQuickNavHeading":
                for call in ast.walk(node):
                    if not isinstance(call, ast.Call):
                        continue
                    name = getattr(call.func, "attr", "") or getattr(call.func, "id", "")
                    if name != "addQuickNav":
                        continue
                    values = {kw.arg: kw.value for kw in call.keywords}
                    return (
                        self.resolver.resolve(values.get("nextDoc"), module) or "",
                        self.resolver.resolve(values.get("prevDoc"), module) or "",
                    )
        return ("", "")

    # -- helpers ----------------------------------------------------------

    def _bind(self, location: str, gesture: str, script_name: str | None) -> None:
        self.gesture_map.setdefault(location, {})[gesture] = script_name


def _truthy(node: ast.AST) -> bool:
    return isinstance(node, ast.Constant) and bool(node.value)


def _say_all_mode(node: ast.AST) -> str:
    """`sayAll.CURSOR.CARET` / `sayAll.CURSOR.REVIEW` -> "caret" / "review"."""
    text = ast.unparse(node)
    match = re.search(r"CURSOR\.?_?([A-Z]+)", text)
    return match.group(1).lower() if match else text


def _range_args(node: ast.AST | None) -> list[int]:
    if not isinstance(node, ast.Call):
        return []
    name = getattr(node.func, "id", "")
    if name != "range":
        return []
    args = []
    for arg in node.args:
        if isinstance(arg, ast.Constant) and isinstance(arg.value, int):
            args.append(arg.value)
        else:
            return []
    return list(range(*args))


# ---------------------------------------------------------------------------
# Scope: which scripts belong to ALUS v1.0
# ---------------------------------------------------------------------------


#: The ONE place where NVDA's own text is edited on the way into ALUS, and the
#: smallest edit that keeps it true: these strings are read out loud to the
#: user ("Shows NVDA's audio settings" from a screen reader that is not NVDA is
#: simply wrong), and "Windows" names the wrong operating system. Phase 9's
#: translations supersede all of this; until then the substitution is visible in
#: the generated CSV, where it can be reviewed.
DESCRIPTION_SUBSTITUTIONS: list[tuple[str, str]] = [
    (r"\bNVDA\b", "ALUS"),
    (r"\bWindows clipboard\b", "system clipboard"),
    (r"\bto Windows\b", "to the application"),
]


def rewrite_description(text: str) -> str:
    for pattern, replacement in DESCRIPTION_SUBSTITUTIONS:
        text = re.sub(pattern, replacement, text)
    return text


#: Scriptable classes ALUS addresses. A locale map section outside this list is
#: dropped: those are Microsoft Word, Excel and Outlook object classes from the
#: upstream Windows implementation, which ALUS can never load, so the bindings
#: are dead weight and the section name is noise.
KNOWN_LOCATION_PREFIXES = (
    "globalCommands.",
    "browseMode.",
    "cursorManager.",
    "documentBase.",
    "editableText.",
    "virtualBuffers.",
    "treeInterceptorHandler.",
    "review.",
    "braille.",
    "brailleInput.",
    # LibreOffice, which ALUS does support (phase 8).
    "appModules.soffice.",
)


def rewrite_script_name(name: str) -> str:
    """ALUS's key is called ALUS in script names too.

    Two braille scripts are named after the modifier key
    (`braille_toggleNVDAKey`). The name is an API — plugins and gesture maps
    bind by it — so it is renamed here rather than left half-and-half. An
    imported map that binds those two by their old name is the one thing this
    does not keep working, and it is written down in docs/history/phase-3.md O14.
    """
    return name.replace("NVDAKey", "AlusKey")


def rewrite_gesture(identifier: str) -> str:
    """Names the screen reader's own key after the screen reader.

    Upstream spells its modifier key `NVDA`; in ALUS the key is `ALUS`, because
    ALUS is the screen reader saying it and writing it. Compatibility is not
    lost: `normalizeGestureIdentifier` accepts the old spelling on the way IN,
    so a user's imported gesture map still matches (vision.md H3). Only the
    source prefix is left alone — `kb(laptop):` is a device, not a key.
    """
    prefix, separator, keys = identifier.partition(":")
    if not separator:
        return identifier
    parts = [
        "ALUS" if part.lower() == "nvda" else part
        for part in keys.split("+")
    ]
    return f"{prefix}:{'+'.join(parts)}"


def scope_of(script: Script) -> tuple[str, str]:
    """Returns (scope, reason) where scope is "v1" or "deferred"."""
    if script.category in DEFERRED_CATEGORIES:
        return "deferred", f"category {script.category!r} is a v1.0 non-goal"
    if script.name in DEFERRED_SCRIPTS:
        return "deferred", "script is a v1.0 non-goal"
    return "v1", ""


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

CSV_COLUMNS = [
    "module",
    "class",
    "script",
    "category",
    "description",
    "gestures",
    "gestures_desktop",
    "gestures_laptop",
    "gestures_touch",
    "can_propagate",
    "bypass_input_help",
    "allow_in_sleep_mode",
    "speak_on_demand",
    "resume_say_all_mode",
    "scope",
    "scope_reason",
    "origin",
]


# A gesture as (layout, key) pairs, so that two gestures can be compared for
# "the same key press". `kb:X` is active in both layouts and therefore yields
# two pairs; the modifiers are sorted, because `ALUS+shift+a` and
# `shift+ALUS+a` are one key press written two ways.
def layout_keys(gesture: str) -> list[tuple[str, str]]:
    lowered = gesture.lower()
    source, _, rest = lowered.partition(":")
    if not rest or not source.startswith("kb"):
        return [(source, rest)]  # br:, bk:, ts: -- not a keyboard layout
    key = "+".join(sorted(rest.split("+")))
    if source == "kb(desktop)":
        return [("desktop", key)]
    if source == "kb(laptop)":
        return [("laptop", key)]
    return [("desktop", key), ("laptop", key)]


# data/gestures-alus.ini -> {(location, script): [gestures]}
#
# The ONE input to the generated map that is not upstream's source. Read the
# file itself for why it exists rather than a new map layer.
def read_alus_gestures(path: Path) -> dict[tuple[str, str], list[str]]:
    if not path.exists():
        print(f"warning: no {path}; ALUS's own key assignments will not be "
              "applied", file=sys.stderr)
        return {}
    out: dict[tuple[str, str], list[str]] = {}
    section = ""
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("["):
            section = line.strip("[]")
            continue
        if "=" not in line:
            print(f"{path}:{number}: no '=' in {line!r} — skipped", file=sys.stderr)
            continue
        if not section:
            print(f"{path}:{number}: {line!r} is outside any section — skipped",
                  file=sys.stderr)
            continue
        name, _, value = line.partition("=")
        out[(section, name.strip())] = [g.strip() for g in value.split(",") if g.strip()]
    return out


# Applies the overrides to the Script objects, BEFORE anything is written, so
# that the map, the CSV and therefore the command reference all say the same
# key. Returns the number applied.
#
# It refuses a collision rather than writing one: two commands on one key in
# one section is resolved by whichever command object step 10 of the lookup
# reaches first, which is not a behaviour anyone chose. Nearly written here by
# hand (ALUS+a is sayAll in the laptop layout), hence the check.
def apply_alus_gestures(
    scripts: list[Script],
    extractor: "Extractor",
    overrides: dict[tuple[str, str], list[str]],
) -> int:
    if not overrides:
        return 0
    by_key = {(s.location, s.name): s for s in scripts}
    applied = 0
    for (location, name), gestures in sorted(overrides.items()):
        script = by_key.get((location, name))
        if script is None:
            print(f"gestures-alus.ini: [{location}] {name} is not a command — skipped",
                  file=sys.stderr)
            continue
        # Every other command in the same section, with the keys it keeps.
        # Compared PER LAYOUT and not by the literal string: `kb:ALUS+a` is
        # active in BOTH layouts (§5.4), so it collides with a
        # `kb(laptop):ALUS+a` that does not look anything like it. The first
        # version of this check compared strings, missed exactly that, and was
        # caught by writing the collision on purpose.
        taken: dict[tuple[str, str], str] = {}
        for other in scripts:
            if other.location != location or other.name == name:
                continue
            for gesture in overrides.get((location, other.name), other.gestures):
                for key in layout_keys(rewrite_gesture(gesture)):
                    taken[key] = other.name
        clash = [
            (g, taken[key])
            for g in gestures
            for key in layout_keys(rewrite_gesture(g))
            if key in taken
        ]
        if clash:
            raise SystemExit(
                f"gestures-alus.ini: [{location}] {name} = "
                + ", ".join(gestures)
                + " collides with "
                + ", ".join(f"{other} on {g}" for g, other in clash)
                + " — two commands on one key in one section is decided by "
                "whichever object the lookup reaches first. Pick another key, "
                "or move the other command here too."
            )
        script.gestures = list(gestures)
        # A command's key can also come from a `__gestures` dict rather than
        # from its decorator, and write_ini reads both. Dropping those here is
        # what makes the override a MOVE and not an addition.
        for entries in extractor.gesture_map.values():
            for gesture, script_name in list(entries.items()):
                if script_name == name:
                    del entries[gesture]
        applied += 1
    return applied


def write_csv(path: Path, scripts: list[Script]) -> None:
    rows = []
    for script in sorted(scripts, key=lambda s: (s.module, s.cls, s.name)):
        gestures = [rewrite_gesture(gesture) for gesture in script.gestures]
        agnostic, desktop, laptop, touch = split_gestures(gestures)
        scope, reason = scope_of(script)
        rows.append(
            {
                "module": script.module,
                "class": script.cls,
                "script": rewrite_script_name(script.name),
                "category": script.category,
                "description": rewrite_description(script.description),
                "gestures": " ".join(agnostic),
                "gestures_desktop": " ".join(desktop),
                "gestures_laptop": " ".join(laptop),
                "gestures_touch": " ".join(touch),
                "can_propagate": int(script.can_propagate),
                "bypass_input_help": int(script.bypass_input_help),
                "allow_in_sleep_mode": int(script.allow_in_sleep_mode),
                "speak_on_demand": int(script.speak_on_demand),
                "resume_say_all_mode": script.resume_say_all_mode,
                "scope": scope,
                "scope_reason": reason,
                "origin": script.origin,
            },
        )
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def write_ini(path: Path, scripts: list[Script], extractor: Extractor, nvda_commit: str) -> int:
    """Writes the built-in gesture layer in NVDA gestures.ini format.

    One section per `module.Class`, one entry per script name, value is a comma
    separated gesture list — exactly what `GestureMap::load()` parses, and
    exactly what a user's own `~/.config/alus/gestures.ini` overrides.
    """
    #: location -> script name -> [gestures]
    sections: dict[str, dict[str, list[str]]] = {}
    deferred = 0

    def add(location: str, script_name: str, gesture: str) -> None:
        gesture = rewrite_gesture(gesture)
        script_name = rewrite_script_name(script_name)
        sections.setdefault(location, {}).setdefault(script_name, [])
        if gesture not in sections[location][script_name]:
            sections[location][script_name].append(gesture)

    in_scope: dict[tuple[str, str], Script] = {}
    for script in scripts:
        scope, _reason = scope_of(script)
        if scope != "v1":
            deferred += 1
            continue
        in_scope[(script.location, script.name)] = script

    for script in scripts:
        if (script.location, script.name) not in in_scope:
            continue
        for gesture in script.gestures:
            if gesture.lower().startswith(DEFERRED_GESTURE_SOURCES):
                continue
            add(script.location, script.name, gesture)

    # Bindings that live in a __gestures dict rather than on the decorator.
    for location, entries in extractor.gesture_map.items():
        for gesture, script_name in entries.items():
            if script_name is None:
                continue
            if gesture.lower().startswith(DEFERRED_GESTURE_SOURCES):
                continue
            defining = extractor._defining_location(location, script_name)
            if defining is None:
                extractor.warnings.append(
                    f"{location}: gesture {gesture} binds unknown script "
                    f"{script_name!r} — dropped",
                )
                continue
            if (defining, script_name) not in in_scope:
                continue
            add(location, script_name, gesture)

    today = datetime.date.today().isoformat()
    lines = [
        "; SPDX-License-Identifier: GPL-2.0-or-later",
        "; ALUS built-in gesture map — GENERATED, DO NOT EDIT.",
        ";",
        f";   tools/extract-commands.py, {today}",
        f";   from upstream commit {nvda_commit} (see vision.md §6 and LICENSE)",
        ";",
        "; The file format and the section names are the ones imported maps use,",
        "; so a user's own gesture map overlays this file unchanged (H3).",
        "; The ALUS key is spelled ALUS here; the imported spelling is accepted on",
        "; load and never written back (KeyNames::kImportedAlusKeyName).",
        "; This is the LOWEST priority layer: user > locale > braille driver > this.",
        ";",
        f"; {len(in_scope)} scripts in v1.0 scope, {deferred} deferred"
        " (see data/scripts.csv, column `scope`).",
        "",
    ]
    for location in sorted(sections):
        lines.append(f"[{location}]")
        for script_name in sorted(sections[location]):
            gestures = sections[location][script_name]
            lines.append(f"\t{script_name} = {', '.join(gestures)}")
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")
    return sum(len(g) for s in sections.values() for g in s.values())


def extract_key_labels(source_root: Path, resolver: Resolver) -> dict[str, str]:
    """NVDA's `keyLabels.localizedKeyLabels`: key name -> spoken label.

    These are what input help and "speak command keys" say out loud, so they
    are user interface, not internals: "capsLock" must be read as "caps lock",
    not spelled. NVDA keys the dict by the LOWER CASED key name.
    """
    path = source_root / "keyLabels.py"
    if not path.exists():
        return {}
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    resolver.collect("keyLabels", tree)
    labels: dict[str, str] = {}
    for node in ast.walk(tree):
        if not isinstance(node, ast.Assign):
            continue
        names = [t.id for t in node.targets if isinstance(t, ast.Name)]
        if "localizedKeyLabels" not in names or not isinstance(node.value, ast.Dict):
            continue
        for key_node, value_node in zip(node.value.keys, node.value.values):
            name = resolver.resolve(key_node, "keyLabels")
            label = resolver.resolve(value_node, "keyLabels")
            if name and label:
                labels[name.lower()] = label
    return labels


def write_key_labels(path: Path, labels: dict[str, str], nvda_commit: str) -> None:
    today = datetime.date.today().isoformat()
    lines = [
        "; SPDX-License-Identifier: GPL-2.0-or-later",
        "; ALUS key labels — GENERATED, DO NOT EDIT.",
        ";",
        f";   tools/extract-commands.py, {today}",
        f";   from upstream commit {nvda_commit}, keyLabels.py"
        "  (see vision.md §6)",
        ";",
        "; Key name (lower case, as it appears in a gesture identifier) = spoken",
        "; label. A key that is missing here is spoken as its own name, which is",
        "; right for letters and digits and wrong for nothing.",
        "",
        "[labels]",
    ]
    for name in sorted(labels):
        lines.append(f"\t{name} = {labels[name]}")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def write_data_strings(path: Path, scripts: list[Script], labels: dict[str, str],
                       nvda_commit: str) -> int:
    """Writes the translatable strings of the generated DATA files as C++.

    WHY THIS FILE HAS TO EXIST. ALUS's command table, its categories and its
    key labels are data and not code, by §5.4 -- that is what makes the
    extractor the source of truth for ~300 commands instead of several thousand
    lines of hand transcription. But xgettext reads source files, not CSV and
    not ini, so a string that lives only in `data/scripts.csv` is a string no
    translator ever sees. NVDA does not have this problem because its command
    table IS Python source.

    So the extractor emits the same strings a second time, as a C++ translation
    unit full of N_() marks. It is never called at run time; it is read by
    xgettext at build time, and by tests/gui/i18n_test.cpp, which asserts that
    every description and category in scripts.csv and every label in
    key-labels.ini appears here -- so the two copies cannot drift apart
    silently.
    """
    categories: dict[str, None] = {}
    descriptions: dict[str, None] = {}
    for script in sorted(scripts, key=lambda s: (s.module, s.cls, s.name)):
        if scope_of(script)[0] != "v1":
            continue
        if script.category:
            categories.setdefault(script.category, None)
        description = rewrite_description(script.description)
        if description:
            descriptions.setdefault(description, None)

    def literal(text: str) -> str:
        escaped = text.replace("\\", "\\\\").replace('"', '\\"')
        return f'N_("{escaped}")'

    lines = [
        "// SPDX-License-Identifier: GPL-2.0-or-later",
        "// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>",
        "//",
        "// This file is part of ALUS (Access Linux Using Speech).",
        "//",
        "// GENERATED by tools/extract-commands.py -- do not edit by hand.",
        f"// Source: upstream commit {nvda_commit}",
        "//",
        "// The translatable strings of data/scripts.csv and data/key-labels.ini,",
        "// as N_() marks, so that xgettext sees them. See write_data_strings() in",
        "// the extractor for why a second copy is the right answer, and",
        "// tests/gui/i18n_test.cpp for what keeps the two copies in step.",
        "//",
        "// Nothing calls this at run time. The arrays are referenced by the test.",
        "",
        '#include "alus/i18n/data_strings.hpp"',
        "",
        '#include "alus/i18n/i18n.hpp"',
        "",
        "#include <array>",
        "",
        "namespace alus::i18n {",
        "namespace {",
        "",
        "// data/scripts.csv, the `category` column: the headings of the gesture",
        "// dialog and of the generated command reference.",
        "constexpr std::array<const char*, %d> kCategories{{" % len(categories),
    ]
    for text in categories:
        lines.append(f"    {literal(text)},")
    lines += [
        "}};",
        "",
        "// data/scripts.csv, the `description` column: what input help speaks and",
        "// what the gesture dialog and the command reference show.",
        "constexpr std::array<const char*, %d> kDescriptions{{" % len(descriptions),
    ]
    for text in descriptions:
        lines.append(f"    {literal(text)},")
    lines += [
        "}};",
        "",
        "// data/key-labels.ini, the `[labels]` values: how a key is SPOKEN.",
        "// Deliberately the values and not the keys: the key is a gesture",
        "// identifier and must never be translated, or a user's gestures.ini",
        "// stops loading in another language.",
        "constexpr std::array<const char*, %d> kKeyLabels{{" % len(labels),
    ]
    for name in sorted(labels):
        lines.append(f"    {literal(labels[name])},")
    lines += [
        "}};",
        "",
        "}  // namespace",
        "",
        "// Spans and not the arrays themselves, so that include/alus/i18n/"
        "data_strings.hpp",
        "// carries no element counts: a count in a hand-written header is a",
        "// number that goes stale the next time the extractor runs.",
        "std::span<const char* const> scriptCategoryStrings() { return kCategories; }",
        "std::span<const char* const> scriptDescriptionStrings() { return kDescriptions; }",
        "std::span<const char* const> keyLabelStrings() { return kKeyLabels; }",
        "",
        "}  // namespace alus::i18n",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")
    return len(categories) + len(descriptions) + len(labels)


def copy_locale_maps(source_root: Path, out_dir: Path) -> list[str]:
    """Copies NVDA's per-language gesture maps.

    These are a derived work ALUS takes over under GPL-2.0-or-later
    (vision.md §6), in the same format ALUS already parses, and they are the
    locale LAYER of the three-layer map (§5.1). Rewriting them by hand would
    only introduce typos in other people's keyboards.
    """
    copied: list[str] = []
    locale_root = source_root / "locale"
    if not locale_root.is_dir():
        return copied
    for path in sorted(locale_root.glob("*/gestures.ini")):
        language = path.parent.name
        target_dir = out_dir / "locale" / language
        target_dir.mkdir(parents=True, exist_ok=True)
        text = path.read_text(encoding="utf-8-sig")
        lines: list[str] = []
        dropped_sections: list[str] = []
        keeping = True
        for line in text.splitlines():
            stripped = line.strip()
            if stripped.startswith("["):
                location = stripped.strip("[]").strip()
                keeping = location.startswith(KNOWN_LOCATION_PREFIXES)
                if not keeping:
                    dropped_sections.append(location)
                    continue
                lines.append(line)
                continue
            if not keeping:
                continue
            if "=" in line and not stripped.startswith((";", "#")):
                name, _, gestures = line.partition("=")
                indent = name[: len(name) - len(name.lstrip())]
                line = (
                    indent
                    + rewrite_script_name(name.strip())
                    + " = "
                    + ", ".join(
                        rewrite_gesture(gesture.strip()) for gesture in gestures.split(",")
                    )
                )
            lines.append(line)
        text = "\n".join(lines)
        # Comment lines carry key names too ("NVDA+shift+- is useful in fr-ca"),
        # and a comment in ALUS's own data file still speaks to an ALUS user.
        text = re.sub(r"\bNVDA(\+)", r"ALUS\1", text)
        dropped_note = (
            f"; {len(dropped_sections)} section(s) dropped: classes ALUS does not have\n"
            if dropped_sections
            else ""
        )
        header = (
            "; SPDX-License-Identifier: GPL-2.0-or-later\n"
            f"; ALUS key overrides for '{language}'. Derived from the upstream screen\n"
            "; reader's data under GPL-2.0-or-later; the attribution and the source are\n"
            "; in vision.md §6 and LICENSE.\n"
            "; GENERATED by tools/extract-commands.py — DO NOT EDIT.\n"
            + dropped_note
            + "; A user's own ~/.config/alus/gestures.ini overrides this file.\n\n"
        )
        (target_dir / "gestures.ini").write_text(header + text + "\n", encoding="utf-8")
        copied.append(language)
    return copied


def source_commit_of(source_root: Path) -> str:
    # A pinned commit file wins over `git rev-parse`, and must: reference/nvda/
    # is a SUBSET of the upstream tree checked in to ALUS's own repository, so
    # asking git about it answers with ALUS's HEAD and would stamp that into
    # every generated file as "upstream commit" -- a provenance line that names
    # the wrong project. The file is written by whoever refreshes the subset.
    pinned = source_root / "UPSTREAM_COMMIT"
    try:
        text = pinned.read_text(encoding="utf-8").strip()
    except OSError:
        text = ""
    if text:
        return text.split()[0][:12]

    try:
        out = subprocess.run(
            ["git", "-C", str(source_root), "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        return out.stdout.strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract the inherited command table into ALUS data files",
    )
    parser.add_argument("--source", "--nvda", dest="source",
                        default="./reference/nvda", type=Path,
                        help="the upstream source checkout to read; the "
                             "checked-in subset in reference/nvda by default")
    parser.add_argument("--out", default="./data", type=Path, help="output directory")
    parser.add_argument("--strings-out", default="./src/i18n/data_strings.cpp", type=Path,
                        help="where to write the N_() marks for the generated data")
    parser.add_argument("--alus-gestures", default="./data/gestures-alus.ini", type=Path,
                        help="ALUS's own key assignments, applied to the generated map")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    source_root = args.source / "source"
    if not source_root.is_dir():
        print(f"not a usable source checkout: {args.source}", file=sys.stderr)
        return 2

    extractor = Extractor(source_root)
    extractor.extract()

    args.out.mkdir(parents=True, exist_ok=True)
    csv_path = args.out / "scripts.csv"
    ini_path = args.out / "gestures-builtin.ini"
    labels_path = args.out / "key-labels.ini"
    commit = source_commit_of(args.source)
    # Before either writer: see apply_alus_gestures.
    overridden = apply_alus_gestures(
        extractor.scripts, extractor, read_alus_gestures(args.alus_gestures))
    write_csv(csv_path, extractor.scripts)
    bindings = write_ini(ini_path, extractor.scripts, extractor, commit)
    labels = extract_key_labels(source_root, extractor.resolver)
    write_key_labels(labels_path, labels, commit)
    locales = copy_locale_maps(source_root, args.out)
    strings = write_data_strings(args.strings_out, extractor.scripts, labels, commit)

    total = len(extractor.scripts)
    deferred = sum(1 for s in extractor.scripts if scope_of(s)[0] != "v1")
    without_description = sum(1 for s in extractor.scripts if not s.description)
    if not args.quiet:
        print(f"{csv_path}: {total} scripts ({total - deferred} in v1.0 scope, {deferred} deferred)")
        print(f"{ini_path}: {bindings} built-in gesture bindings"
              + (f", {overridden} key(s) from {args.alus_gestures}" if overridden else ""))
        print(f"{labels_path}: {len(labels)} key labels")
        print(f"{args.strings_out}: {strings} translatable strings from the generated data")
        print(f"{args.out / 'locale'}: {len(locales)} locale gesture maps "
              f"({'sr included' if 'sr' in locales else 'SERBIAN MISSING'})")
        print(f"scripts without a description: {without_description} "
              "(hidden from the gesture dialog, as upstream hides them)")
        if extractor.warnings:
            print(f"\n{len(extractor.warnings)} warnings:", file=sys.stderr)
            for warning in extractor.warnings:
                print(f"  {warning}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
